#include "PlanSession.h"

#include "MergeConflict.h"

#include <QDir>
#include <QFile>

#include <cstdio>
#include <unistd.h>

namespace {

constexpr QLatin1StringView PlannedPrefix("plan:");

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

QString shellQuote(const QString &arg)
{
    static const QString plain = QStringLiteral("-_./:=@^{}~%+,");
    const bool simple = !arg.isEmpty() && std::all_of(arg.cbegin(), arg.cend(), [](QChar c) {
        return c.isLetterOrNumber() || plain.contains(c);
    });
    if (simple)
        return arg;
    QString quoted = arg;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace

bool PlanSession::begin(const GitRunner &git, QString &error)
{
    m_real = git;
    if (!m_dir.isValid()) {
        error = tr("Could not create a temporary directory.");
        return false;
    }
    if (const QString operation = operationInProgress(git); !operation.isEmpty()) {
        error = tr("git is in the middle of an operation (%1); finish or abort it first.").arg(operation);
        return false;
    }
    if (!git.run({QStringLiteral("ls-files"), QStringLiteral("-u")}).out.isEmpty()) {
        error = tr("Resolve the merge conflicts before planning.");
        return false;
    }
    const GitResult head = git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                    QStringLiteral("-q"), QStringLiteral("HEAD^{commit}")});
    m_baselineHead = m_head = head.ok() ? firstLine(head.out) : QString();

    const GitResult gitPath = git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                       QStringLiteral("index")});
    if (!gitPath.ok()) {
        error = gitPath.errorText();
        return false;
    }
    m_indexPath = QDir(git.workDir()).absoluteFilePath(firstLine(gitPath.out));

    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), indexPath(QStringLiteral("index")));
    m_git = git;
    m_git.setEnvironment(env);
    if (QFile::exists(m_indexPath)) {
        if (!QFile::copy(m_indexPath, indexPath(QStringLiteral("index")))) {
            error = tr("Could not copy the index.");
            return false;
        }
    } else {
        m_git.run({QStringLiteral("read-tree"), QStringLiteral("--empty")});
    }
    QFile::copy(indexPath(QStringLiteral("index")), indexPath(QStringLiteral("baseline.index")));

    const GitResult tree = m_git.run({QStringLiteral("write-tree")});
    if (!tree.ok()) {
        error = tree.errorText();
        return false;
    }
    m_baselineTree = firstLine(tree.out);
    m_active = true;
    return true;
}

StateSource PlanSession::source() const
{
    StateSource source;
    if (!m_active) {
        source.git = m_real;
        return source;
    }
    source.git = m_git;
    source.head = m_head.isEmpty() ? QStringLiteral("HEAD") : m_head;
    source.simulated = true;
    source.baselineHead = m_baselineHead;
    return source;
}

bool PlanSession::add(PlanEntry entry, QString &error)
{
    entry.id = m_nextId++;
    if (entry.kind == PlanEntry::Fold)
        entry.target = stableId(entry.target);
    if (!simulate(entry, error))
        return false;
    m_entries.append(entry);
    return true;
}

void PlanSession::remove(int id)
{
    m_entries.removeIf([id](const PlanEntry &e) { return e.id == id; });

    QFile::remove(indexPath(QStringLiteral("index")));
    QFile::copy(indexPath(QStringLiteral("baseline.index")), indexPath(QStringLiteral("index")));
    m_head = m_baselineHead;
    m_resolved.clear();
    for (PlanEntry &entry : m_entries) {
        entry.error.clear();
        if (!simulate(entry, entry.error) && entry.error.isEmpty())
            entry.error = tr("Does not apply any more.");
    }
}

bool PlanSession::executable() const
{
    return !m_entries.isEmpty()
        && std::none_of(m_entries.cbegin(), m_entries.cend(), [](const PlanEntry &e) { return !e.error.isEmpty(); });
}

bool PlanSession::execute(QString &error)
{
    if (!executable()) {
        error = tr("Remove the entries that no longer apply first.");
        return false;
    }

    // Publish only on top of exactly what the plan started from.
    const GitResult head = m_real.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                       QStringLiteral("-q"), QStringLiteral("HEAD^{commit}")});
    if ((head.ok() ? firstLine(head.out) : QString()) != m_baselineHead) {
        error = tr("HEAD moved since planning began; nothing was changed.");
        return false;
    }
    // Checked on a copy, so we never hold the real index lock ourselves.
    QProcessEnvironment check;
    check.insert(QStringLiteral("GIT_INDEX_FILE"), indexPath(QStringLiteral("check.index")));
    QFile::remove(indexPath(QStringLiteral("check.index")));
    if (QFile::exists(m_indexPath))
        QFile::copy(m_indexPath, indexPath(QStringLiteral("check.index")));
    else
        m_real.run({QStringLiteral("read-tree"), QStringLiteral("--empty")}, {}, check);
    if (firstLine(m_real.run({QStringLiteral("write-tree")}, {}, check).out) != m_baselineTree) {
        error = tr("The index was changed outside of the plan; nothing was changed.");
        return false;
    }

    // git's own protocol: whoever creates index.lock owns the index.
    const QString lockPath = m_indexPath + QStringLiteral(".lock");
    QFile lock(lockPath);
    if (!lock.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = tr("Another git process is using the index (%1 exists); nothing was changed.").arg(lockPath);
        return false;
    }

    if (m_head != m_baselineHead) {
        const GitResult moved = m_real.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                            QStringLiteral("igitt: execute plan"), QStringLiteral("HEAD"),
                                            m_head, m_baselineHead});
        if (!moved.ok()) {
            lock.remove();
            error = tr("Moving HEAD failed; nothing was changed: %1").arg(moved.errorText());
            return false;
        }
    }

    QFile planned(indexPath(QStringLiteral("index")));
    const bool written = planned.open(QIODevice::ReadOnly) && lock.write(planned.readAll()) == planned.size()
                         && lock.flush() && ::fsync(lock.handle()) == 0;
    lock.close();
    if (!written || std::rename(QFile::encodeName(lockPath).constData(),
                                QFile::encodeName(m_indexPath).constData()) != 0) {
        QFile::remove(lockPath);
        error = tr("HEAD was moved, but replacing the index failed. "
                   "`git reset --soft %1` restores the previous HEAD.").arg(m_baselineHead.left(12));
        return false;
    }
    return true;
}

QString PlanSession::publishSummary() const
{
    QStringList lines;
    if (m_head != m_baselineHead) {
        lines << QStringLiteral("$ git update-ref -m 'igitt: execute plan' HEAD %1 %2")
                     .arg(m_head, m_baselineHead.isEmpty() ? QStringLiteral("''") : m_baselineHead);
    }
    lines << tr("Replace %1 with the planned index, under git's index lock.").arg(m_indexPath);
    return lines.join(QLatin1Char('\n'));
}

bool PlanSession::simulate(PlanEntry &entry, QString &error)
{
    // Leave the plan as it was if the entry fails halfway.
    const QString backup = indexPath(QStringLiteral("backup.index"));
    QFile::remove(backup);
    QFile::copy(indexPath(QStringLiteral("index")), backup);
    const QString head = m_head;
    const QHash<QString, QString> resolved = m_resolved;
    entry.log.clear();

    bool ok = false;
    switch (entry.kind) {
    case PlanEntry::StagePaths: {
        if (entry.indexInfo.isEmpty()) {
            // First time: let git add decide, then record what it did, so
            // replays do not depend on the files changing meanwhile.
            const QString before = firstLine(m_git.run({QStringLiteral("write-tree")}).out);
            QStringList args{QStringLiteral("add"), QStringLiteral("-A")};
            if (!entry.paths.isEmpty())
                args << QStringLiteral("--") << entry.paths;
            const GitResult added = run(entry, args);
            const QString after = firstLine(m_git.run({QStringLiteral("write-tree")}).out);
            if (!added.ok()) {
                error = added.errorText();
                break;
            }
            const QList<QByteArray> raw = m_git.run({QStringLiteral("diff-tree"), QStringLiteral("-r"),
                                                     QStringLiteral("-z"), QStringLiteral("--no-renames"),
                                                     before, after}).out.split('\0');
            // :oldmode newmode oldsha newsha status NUL path NUL
            for (qsizetype i = 0; i + 1 < raw.size(); i += 2) {
                const QList<QByteArray> fields = raw[i].split(' ');
                if (fields.size() < 5)
                    break;
                const bool deleted = fields[4] == "D";
                entry.indexInfo += (deleted ? QByteArray("0 0000000000000000000000000000000000000000")
                                            : fields[1] + ' ' + fields[3])
                                   + '\t' + raw[i + 1] + '\0';
            }
            if (entry.indexInfo.isEmpty()) {
                error = tr("Nothing to stage.");
                break;
            }
            ok = true;
        } else {
            const GitResult r = run(entry, {QStringLiteral("update-index"), QStringLiteral("-z"),
                                            QStringLiteral("--index-info")}, entry.indexInfo);
            ok = r.ok();
            if (!ok)
                error = r.errorText();
        }
        break;
    }
    case PlanEntry::UnstagePaths: {
        const QStringList command = m_head.isEmpty()
            ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q"),
                          QStringLiteral("--ignore-unmatch")}
            : QStringList{QStringLiteral("reset"), QStringLiteral("-q"), m_head};
        const GitResult r = run(entry, command + QStringList{QStringLiteral("--")} + entry.paths);
        ok = r.ok();
        if (!ok)
            error = r.errorText();
        break;
    }
    case PlanEntry::UnstageAll: {
        const GitResult r = run(entry, m_head.isEmpty()
                                           ? QStringList{QStringLiteral("read-tree"), QStringLiteral("--empty")}
                                           : QStringList{QStringLiteral("read-tree"), m_head});
        ok = r.ok();
        if (!ok)
            error = r.errorText();
        break;
    }
    case PlanEntry::ApplyPatch: {
        // The patch was made against the plan as it was then; after an
        // earlier entry is removed its context may differ, and --3way merges
        // with the preimage recorded in the patch instead of failing.
        QStringList args{QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--3way"),
                         QStringLiteral("--whitespace=nowarn")};
        if (entry.reverse)
            args.append(QStringLiteral("--reverse"));
        const GitResult r = run(entry, args, entry.patch);
        ok = r.ok();
        if (!ok)
            error = r.errorText();
        break;
    }
    case PlanEntry::Commit:
        ok = simulateCommit(entry, error);
        break;
    case PlanEntry::Fold:
        ok = simulateFold(entry, error);
        break;
    }

    if (!ok) {
        QFile::remove(indexPath(QStringLiteral("index")));
        QFile::copy(backup, indexPath(QStringLiteral("index")));
        m_head = head;
        m_resolved = resolved;
    }
    return ok;
}

bool PlanSession::simulateCommit(PlanEntry &entry, QString &error)
{
    const QString tree = firstLine(m_git.run({QStringLiteral("write-tree")}).out);
    QStringList parents;
    QProcessEnvironment author;
    if (entry.amend) {
        if (m_head.isEmpty()) {
            error = tr("There is no commit to amend.");
            return false;
        }
        parents = QString::fromLatin1(m_git.run({QStringLiteral("rev-parse"), m_head + QStringLiteral("^@")}).out)
                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        // Amending keeps the author, like git commit --amend.
        const QStringList ident = QString::fromUtf8(m_git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                                               QStringLiteral("--format=%an%n%ae%n%ad"),
                                                               QStringLiteral("--date=raw"), m_head}).out)
                                      .split(QLatin1Char('\n'));
        author.insert(QStringLiteral("GIT_AUTHOR_NAME"), ident.value(0));
        author.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), ident.value(1));
        author.insert(QStringLiteral("GIT_AUTHOR_DATE"), QLatin1Char('@') + ident.value(2));
    } else {
        const QString headTree = m_head.isEmpty()
            ? firstLine(m_git.run({QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"),
                                   QStringLiteral("--stdin")}).out)
            : firstLine(m_git.run({QStringLiteral("rev-parse"), m_head + QStringLiteral("^{tree}")}).out);
        if (tree == headTree) {
            error = tr("Nothing is staged to commit.");
            return false;
        }
        if (!m_head.isEmpty())
            parents = {m_head};
    }

    // What git commit does around its editor: clean up, then let the hooks
    // check the index and adjust the message.
    const QString messageFile = indexPath(QStringLiteral("COMMIT_EDITMSG"));
    {
        QFile file(messageFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = tr("Could not write the commit message.");
            return false;
        }
        file.write(m_git.run({QStringLiteral("stripspace")}, entry.message.toUtf8()).out);
    }
    const QList<QStringList> hooks{
        {QStringLiteral("pre-commit")},
        {QStringLiteral("prepare-commit-msg"), QStringLiteral("--"), messageFile, QStringLiteral("message")},
        {QStringLiteral("commit-msg"), QStringLiteral("--"), messageFile},
    };
    for (const QStringList &hook : hooks) {
        const GitResult r = run(entry, QStringList{QStringLiteral("hook"), QStringLiteral("run"),
                                                   QStringLiteral("--ignore-missing")} + hook);
        if (!r.ok()) {
            error = tr("The %1 hook failed: %2").arg(hook.first(), r.errorText());
            return false;
        }
    }
    QFile file(messageFile);
    const QByteArray message = file.open(QIODevice::ReadOnly)
        ? m_git.run({QStringLiteral("stripspace")}, file.readAll()).out : QByteArray();
    if (message.trimmed().isEmpty()) {
        error = tr("The commit message is empty.");
        return false;
    }

    QStringList args{QStringLiteral("commit-tree"), tree};
    for (const QString &parent : std::as_const(parents))
        args << QStringLiteral("-p") << parent;
    const GitResult created = run(entry, args, message, author);
    if (!created.ok()) {
        error = created.errorText();
        return false;
    }
    const QString sha = firstLine(created.out);
    if (entry.amend)
        rewrite(m_head, sha);
    else
        m_resolved.insert(PlannedPrefix + QString::number(entry.id), sha);
    m_head = sha;
    return true;
}

bool PlanSession::simulateFold(PlanEntry &entry, QString &error)
{
    const QString target = resolve(entry.target);
    if (target.startsWith(PlannedPrefix)) {
        error = tr("The commit to fold into is not part of the plan any more.");
        return false;
    }
    const FoldResult result = FoldEngine(m_git).fold(target, entry.patch, entry.base,
                                                     m_head.isEmpty() ? QStringLiteral("HEAD") : m_head, false);
    entry.log += QStringLiteral("$ igitt fold into %1 (merge-tree and commit-tree per rewritten commit)\n")
                     .arg(target.left(12));
    entry.log += QString::fromUtf8(entry.patch);
    if (!result.ok || !result.error.isEmpty()) {
        error = result.error;
        if (!result.conflicts.isEmpty())
            error += QLatin1Char(' ') + tr("Conflicting files: %1").arg(result.conflicts.join(QStringLiteral(", ")));
        return false;
    }
    for (const auto &[oldId, newId] : result.rewrites) {
        entry.log += QStringLiteral("  %1 -> %2\n").arg(oldId.left(12), newId.left(12));
        rewrite(oldId, newId);
    }
    m_head = result.newHead;
    return true;
}

GitResult PlanSession::run(PlanEntry &entry, const QStringList &args, const QByteArray &input,
                           const QProcessEnvironment &env)
{
    QStringList quoted;
    for (const QString &arg : args)
        quoted.append(shellQuote(arg));
    entry.log += QStringLiteral("$ git ") + quoted.join(QLatin1Char(' ')) + QLatin1Char('\n');
    if (!input.isEmpty() && !args.contains(QStringLiteral("--index-info")))
        entry.log += QString::fromUtf8(input) + (input.endsWith('\n') ? QString() : QStringLiteral("\n"));
    return m_git.run(args, input, env);
}

void PlanSession::rewrite(const QString &oldId, const QString &newId)
{
    bool tracked = false;
    for (auto it = m_resolved.begin(); it != m_resolved.end(); ++it) {
        if (it.value() == oldId) {
            it.value() = newId;
            tracked = true;
        }
    }
    if (!tracked)
        m_resolved.insert(oldId, newId);
}

QString PlanSession::resolve(const QString &id) const
{
    return m_resolved.value(id, id);
}

QString PlanSession::stableId(const QString &sha) const
{
    for (auto it = m_resolved.cbegin(); it != m_resolved.cend(); ++it) {
        if (it.value() == sha)
            return it.key();
    }
    return sha;
}

QString PlanSession::indexPath(const QString &name) const
{
    return m_dir.filePath(name);
}
