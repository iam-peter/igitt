#include "PatchCommit.h"

#include "DiffParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("PatchCommit", text);
}

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out).section(QLatin1Char('\n'), 0, 0).trimmed();
}

} // namespace

QStringList patchPaths(const QByteArray &patch)
{
    QStringList paths;
    for (const FileDiff &file : parseDiff(patch)) {
        for (const QString &path : {file.oldPath, file.newPath}) {
            if (!path.isEmpty() && !paths.contains(path))
                paths.append(path);
        }
    }
    return paths;
}

QString treeWithPatch(const GitRunner &git, const QString &base, const QByteArray &patch, QString &error,
                      bool threeWay)
{
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        error = tr("Cannot make a scratch index: %1").arg(scratch.errorString());
        return {};
    }
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), QDir(scratch.path()).filePath(QStringLiteral("index")));
    const GitResult read = git.run(base.isEmpty() ? QStringList{QStringLiteral("read-tree"), QStringLiteral("--empty")}
                                                  : QStringList{QStringLiteral("read-tree"), base},
                                   {}, env);
    if (!read.ok()) {
        error = read.errorText();
        return {};
    }
    QStringList apply{QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--whitespace=nowarn")};
    if (threeWay)
        apply.append(QStringLiteral("--3way"));
    const GitResult applied = git.run(apply, patch, env);
    if (!applied.ok()) {
        error = applied.errorText();
        return {};
    }
    const GitResult tree = git.run({QStringLiteral("write-tree")}, {}, env);
    if (!tree.ok()) {
        error = tree.errorText();
        return {};
    }
    return firstLine(tree.out);
}

QString commitPatch(const GitRunner &git, const QByteArray &patch, const QString &message, QString &error)
{
    if (patch.isEmpty()) {
        error = tr("There are no changes to commit.");
        return {};
    }
    // git commit would make the patch the merge's or cherry-pick's result
    const QList<std::pair<const char *, QString>> operations{
        {"MERGE_HEAD", tr("a merge")}, {"CHERRY_PICK_HEAD", tr("a cherry-pick")}, {"REVERT_HEAD", tr("a revert")}};
    for (const auto &[ref, what] : operations) {
        if (git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                     QString::fromLatin1(ref)}).ok()) {
            error = tr("Finish %1 first: committing now would conclude it with these changes alone.").arg(what);
            return {};
        }
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        error = tr("Cannot make a scratch index: %1").arg(scratch.errorString());
        return {};
    }
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), QDir(scratch.path()).filePath(QStringLiteral("index")));
    const bool unborn = !git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                                  QStringLiteral("HEAD")}).ok();
    const GitResult read = git.run(unborn ? QStringList{QStringLiteral("read-tree"), QStringLiteral("--empty")}
                                          : QStringList{QStringLiteral("read-tree"), QStringLiteral("HEAD")},
                                   {}, env);
    if (!read.ok()) {
        error = read.errorText();
        return {};
    }
    if (const GitResult r = git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                     QStringLiteral("--whitespace=nowarn")}, patch, env);
        !r.ok()) {
        error = r.errorText();
        return {};
    }
    if (const GitResult r = git.run({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-F"),
                                     QStringLiteral("-")}, message.toUtf8() + '\n', env);
        !r.ok()) {
        error = r.errorText();
        return {};
    }
    const QString sha = firstLine(git.run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).out);
    // Without this, the index would still hold the files as they were, and
    // the next commit from it would take the patch back
    const GitResult reset = git.run(QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"),
                                                QStringLiteral("--")} + patchPaths(patch));
    if (!reset.ok())
        error = tr("Committed, but the index of its files could not follow: %1").arg(reset.errorText());
    return sha;
}
