#include "ChangeClusters.h"

#include "PatchBuilder.h"
#include "PatchCommit.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QSet>

#include <algorithm>

namespace {

// The palette views pick their colors from, by Cluster::color.
constexpr int kColors = 8;

bool isChange(const DiffLine &line)
{
    return line.kind == DiffLine::Added || line.kind == DiffLine::Removed;
}

QString hash(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16));
}

QString decode(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes);
}

QString title(const QString &message)
{
    return message.section(QLatin1Char('\n'), 0, 0).trimmed();
}

} // namespace

ChangeClusters::ChangeClusters(QObject *parent)
    : QObject(parent)
{
}

void ChangeClusters::notifyChanged()
{
    // Before changed(): QML reads the lists as soon as it is emitted,
    // before any slot connected to it could build them anew
    m_hunkList = buildHunks();
    // Clusters and files carry their pieces themselves, so a view needs no
    // lookup that might read the pieces of before
    QHash<QString, QVariant> pieces;
    for (const QVariant &piece : std::as_const(m_hunkList))
        pieces.insert(piece.toMap().value(QStringLiteral("id")).toString(), piece);
    m_clusterList = buildClusters(pieces);
    m_fileList = buildFiles(pieces);
    emit changed();
}

QVariantList ChangeClusters::hunks() const
{
    return m_hunkList;
}

QVariantList ChangeClusters::clusters() const
{
    return m_clusterList;
}

QVariantList ChangeClusters::files() const
{
    return m_fileList;
}

void ChangeClusters::setPath(const QString &path)
{
    if (m_git.workDir() == path)
        return;
    m_git.setWorkDir(path);
    m_clusters.clear();
    m_clusterOf.clear();
    m_expected.clear();
    emit pathChanged();
    reload();
}

void ChangeClusters::setBase(const QString &base)
{
    const QString value = base.isEmpty() ? QStringLiteral("HEAD") : base;
    if (m_base == value)
        return;
    m_base = value;
    emit baseChanged();
    reload();
}

void ChangeClusters::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void ChangeClusters::reload()
{
    m_diff.clear();
    m_hunks.clear();
    // Nothing of an earlier reading stays, should this one fail; whether
    // the commits changed is told once the reading is done
    const QVariantList commitsBefore = m_commitList;
    const auto tellCommits = qScopeGuard([this, commitsBefore] {
        if (m_commitList != commitsBefore)
            emit commitsChanged();
    });
    m_commits.clear();
    m_commitList.clear();
    if (m_git.workDir().isEmpty()) {
        notifyChanged();
        return;
    }

    // Everything in the working tree against the base, staged or not and
    // untracked files too: clusters take the place of the staging area.
    // Read through an index of its own holding the working tree, which
    // also holds when the base is a planned commit that the real index
    // knows nothing of.
    QTemporaryDir scratch;
    const QString scratchIndex = QDir(scratch.path()).filePath(QStringLiteral("index"));
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), scratchIndex);
    const bool unborn = !m_git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                                    m_base + QStringLiteral("^{commit}")}).ok();
    // A copy of the real index holds what git knows of every file (its
    // stat data), so adding the working tree to it only reads the files
    // that changed; against the base, its own content does not matter
    const QString realIndex = QDir(m_git.workDir()).absoluteFilePath(
        QString::fromUtf8(m_git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                     QStringLiteral("index")}).out.trimmed()));
    // The copy keeps the index's time: git trusts the stat data only of
    // files older than the index, and a fresh time would make a file
    // rewritten in the same second, at the same size, look unchanged
    if (QFile::exists(realIndex) && QFile::copy(realIndex, scratchIndex)) {
        QFile copy(scratchIndex);
        if (copy.open(QIODevice::ReadWrite))
            copy.setFileTime(QFileInfo(realIndex).lastModified(), QFileDevice::FileModificationTime);
    }
    const GitResult added = m_git.run({QStringLiteral("add"), QStringLiteral("-A")}, {}, env);
    QStringList diffArgs{QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--no-color"),
                         QStringLiteral("--no-ext-diff"), QStringLiteral("--no-renames"),
                         QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/")};
    if (!unborn)
        diffArgs.append(m_base);
    const GitResult diff = m_git.run(diffArgs, {}, env);
    if (!scratch.isValid() || !added.ok() || !diff.ok()) {
        setError(!added.ok() ? added.errorText() : diff.errorText());
        notifyChanged();
        return;
    }
    const QByteArray text = diff.out;
    m_diff = parseDiff(text);
    const QList<QByteArray> head = m_git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                              QStringLiteral("--format=%h%n%s"), m_base}).out.split('\n');
    m_headSha = QString::fromUtf8(head.value(0));
    m_headSubject = QString::fromUtf8(head.value(1));

    for (int f = 0; f < m_diff.size(); ++f) {
        const FileDiff &file = m_diff[f];
        if (file.isBinary)
            continue;
        // A line is known by its file, its change and how often the same
        // change came before it in the file; a hunk by its lines' keys
        QHash<QByteArray, int> seen;
        QHash<QString, int> hunksSeen;
        const QByteArray path = file.path().toUtf8();
        for (int h = 0; h < file.hunks.size(); ++h) {
            Hunk hunk;
            hunk.file = f;
            hunk.index = h;
            QByteArray keys = path;
            const QList<DiffLine> &lines = file.hunks[h].lines;
            for (int l = 0; l < lines.size(); ++l) {
                if (!isChange(lines[l]))
                    continue;
                const QByteArray content = char(lines[l].kind) + lines[l].content;
                const int occurrence = seen[content]++;
                Line line;
                line.index = l;
                line.key = lineKey(file.path(), content, occurrence);
                line.cluster = m_clusterOf.value(line.key, -1);
                if (clusterIndex(line.cluster) < 0)
                    line.cluster = -1;
                keys += line.key.toLatin1();
                hunk.lines.append(line);
            }
            const QString key = hash(keys);
            const int occurrence = hunksSeen[key]++;
            hunk.id = occurrence == 0 ? key : key + u'-' + QString::number(occurrence);
            m_hunks.append(hunk);
        }
    }
    readCommits();
    setError({});
    notifyChanged();
}

void ChangeClusters::readCommits()
{
    // What the branch is merged into: the remote's default branch, or the
    // upstream; without either, every commit down to the last merge
    const QString branch = QString::fromUtf8(m_git.run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                                                        QStringLiteral("--short"), QStringLiteral("HEAD")}).out)
                               .trimmed();
    QString remote = branch.isEmpty() ? QString()
        : QString::fromUtf8(m_git.run({QStringLiteral("config"), QStringLiteral("--get"),
                                       QStringLiteral("branch.%1.remote").arg(branch)}).out).trimmed();
    if (remote.isEmpty() || remote == u'.')
        remote = QStringLiteral("origin");
    const auto exists = [this](const QString &ref) {
        return m_git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                          ref + QStringLiteral("^{commit}")}).ok();
    };
    QString merged = QStringLiteral("refs/remotes/%1/HEAD").arg(remote);
    if (!exists(merged))
        merged = exists(QStringLiteral("@{upstream}")) ? QStringLiteral("@{upstream}") : QString();

    // The commits and their changes in one reading, each record after a
    // separator of its own: the ids, parents and subject, then the diff
    const auto limited = [&](QStringList args) {
        args.append(m_base);
        if (!merged.isEmpty())
            args << QStringLiteral("--not") << merged;
        return args;
    };
    const GitResult log = m_git.run(limited({QStringLiteral("log"), QStringLiteral("--first-parent"),
                                             QStringLiteral("--max-count=%1").arg(MaxCommits), QStringLiteral("-p"),
                                             QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                                             QStringLiteral("--no-renames"), QStringLiteral("--src-prefix=a/"),
                                             QStringLiteral("--dst-prefix=b/"),
                                             QStringLiteral("--format=%x1e%H%x1f%h%x1f%P%x1f%at%x1f%s")}));
    if (!log.ok())
        return;
    // The same commits, those no remote-tracking branch has
    QStringList onNoRemote{QStringLiteral("rev-list"), QStringLiteral("--first-parent"),
                           QStringLiteral("--max-count=%1").arg(MaxCommits), m_base, QStringLiteral("--not"),
                           QStringLiteral("--remotes")};
    if (!merged.isEmpty())
        onNoRemote.append(merged);
    const GitResult unpublished = m_git.run(onNoRemote);
    const QSet<QByteArray> local = [&] {
        const QList<QByteArray> ids = unpublished.out.split('\n');
        return QSet<QByteArray>(ids.cbegin(), ids.cend());
    }();
    for (const QByteArray &record : log.out.split('\x1e')) {
        const qsizetype end = record.indexOf('\n');
        const QList<QByteArray> fields = record.left(end).split('\x1f');
        if (fields.size() < 5)
            continue;
        // A merge cannot be folded into, nor anything before it
        if (fields[2].count(' ') > 0)
            break;
        Commit commit;
        commit.sha = QString::fromLatin1(fields[0]);
        commit.shortSha = QString::fromLatin1(fields[1]);
        commit.time = fields[3].toLongLong();
        // The subject is last, as it may hold the separator itself
        commit.subject = decode(record.left(end).split('\x1f').mid(4).join('\x1f'));
        commit.published = !local.contains(fields[0]);
        commit.diff = end < 0 ? QList<FileDiff>() : parseDiff(record.mid(end + 1));
        m_commits.prepend(commit);
    }
    m_commitList = buildCommitList();
}

QList<qreal> ChangeClusters::commitTimes() const
{
    QList<qreal> times;
    for (auto it = m_commits.crbegin(); it != m_commits.crend(); ++it)
        times.append(qreal(it->time));
    return times;
}

bool ChangeClusters::isPublished(const QString &sha) const
{
    const Commit *c = commit(sha);
    return c && c->published;
}

const ChangeClusters::Commit *ChangeClusters::commit(const QString &sha) const
{
    for (const Commit &commit : m_commits) {
        if (commit.sha == sha)
            return &commit;
    }
    return nullptr;
}

std::pair<int, int> ChangeClusters::commitHunk(const QString &hunkId)
{
    const QStringList parts = hunkId.split(u':');
    if (parts.size() != 3)
        return {-1, -1};
    return {parts[1].toInt(), parts[2].toInt()};
}

QByteArray ChangeClusters::selectedPatch(const FileDiff &file, int hunk, const QList<int> &lines)
{
    if (hunk < 0 || hunk >= file.hunks.size())
        return {};
    LineSelection selection(file.hunks.size());
    for (int h = 0; h < file.hunks.size(); ++h)
        selection[h] = QList<bool>(file.hunks[h].lines.size(), false);
    bool any = false;
    const QList<DiffLine> &diffLines = file.hunks[hunk].lines;
    for (int l = 0; l < diffLines.size(); ++l) {
        if (isChange(diffLines[l]) && (lines.isEmpty() || lines.contains(l))) {
            selection[hunk][l] = true;
            any = true;
        }
    }
    return any ? buildPatch(file, selection, PatchDirection::Forward) : QByteArray();
}

QByteArray ChangeClusters::linesPatch(const QString &hunkId, const QList<int> &lines) const
{
    const Hunk *h = hunk(hunkId);
    if (!h || lines.isEmpty())
        return {};
    return selectedPatch(m_diff[h->file], h->index, lines);
}

QByteArray ChangeClusters::commitPatch(const QString &sha, const QString &hunkId, const QList<int> &lines) const
{
    const Commit *c = commit(sha);
    const auto [file, index] = commitHunk(hunkId);
    if (!c || file < 0 || file >= c->diff.size())
        return {};
    return selectedPatch(c->diff[file], index, lines);
}

QHash<QByteArray, int> ChangeClusters::linesOf(const QString &path) const
{
    QHash<QByteArray, int> lines;
    for (const Hunk &hunk : m_hunks) {
        if (m_diff[hunk.file].path() == path) {
            for (const Line &line : hunk.lines)
                ++lines[lineIdentity(hunk, line)];
        }
    }
    return lines;
}

QByteArray ChangeClusters::lineIdentity(const Hunk &hunk, const Line &line) const
{
    // Where the line is in the working tree, which a move between commits
    // and the working tree does not touch: an added line's own number, a
    // removed one's the number of the line it comes before
    const QList<DiffLine> &lines = m_diff[hunk.file].hunks[hunk.index].lines;
    int at = m_diff[hunk.file].hunks[hunk.index].newStart;
    for (int l = 0; l < line.index; ++l) {
        if (lines[l].newLine > 0)
            at = lines[l].newLine + 1;
    }
    if (lines[line.index].newLine > 0)
        at = lines[line.index].newLine;
    return QByteArray::number(at) + '\0' + lineContent(hunk, line);
}

void ChangeClusters::expect(const QString &sha, const QString &hunkId, const QList<int> &lines, int cluster)
{
    const Commit *c = commit(sha);
    const auto [file, index] = commitHunk(hunkId);
    if (!c || file < 0 || file >= c->diff.size() || index < 0 || index >= c->diff[file].hunks.size())
        return;
    Expected expected;
    expected.path = c->diff[file].path();
    expected.cluster = cluster;
    expected.before = linesOf(expected.path);
    const QList<DiffLine> &diffLines = c->diff[file].hunks[index].lines;
    for (int l = 0; l < diffLines.size(); ++l) {
        if (isChange(diffLines[l]) && (lines.isEmpty() || lines.contains(l)))
            expected.contents.append(char(diffLines[l].kind) + diffLines[l].content);
    }
    m_expected.append(expected);
}

void ChangeClusters::placeExpected()
{
    bool any = false;
    for (Expected &expected : m_expected) {
        if (clusterIndex(expected.cluster) < 0)
            continue;
        // Only lines that were not there before the move are its own
        QHash<QByteArray, int> before = expected.before;
        for (Hunk &hunk : m_hunks) {
            if (m_diff[hunk.file].path() != expected.path)
                continue;
            for (Line &line : hunk.lines) {
                int &known = before[lineIdentity(hunk, line)];
                if (known > 0) {
                    --known;
                    continue;
                }
                if (line.cluster < 0 && expected.contents.removeOne(lineContent(hunk, line))) {
                    setLineCluster(line, expected.cluster);
                    any = true;
                }
            }
        }
    }
    m_expected.clear();
    if (any)
        notifyChanged();
}

void ChangeClusters::dropExpected()
{
    m_expected.clear();
}

QVariantList ChangeClusters::commits() const
{
    return m_commitList;
}

QVariantList ChangeClusters::buildCommitList() const
{
    QVariantList list;
    for (const Commit &commit : m_commits) {
        QVariantList pieces;
        QStringList files;
        int added = 0;
        int removed = 0;
        for (int f = 0; f < commit.diff.size(); ++f) {
            const FileDiff &file = commit.diff[f];
            if (file.isBinary)
                continue;
            files.append(file.path());
            for (int h = 0; h < file.hunks.size(); ++h) {
                const DiffHunk &diff = file.hunks[h];
                QVariantList lines;
                int hunkAdded = 0;
                int hunkRemoved = 0;
                int first = -1;
                for (int l = 0; l < diff.lines.size(); ++l) {
                    const DiffLine &line = diff.lines[l];
                    if (!isChange(line))
                        continue;
                    hunkAdded += line.kind == DiffLine::Added;
                    hunkRemoved += line.kind == DiffLine::Removed;
                    if (first < 0)
                        first = line.newLine > 0 ? line.newLine : line.oldLine;
                    lines.append(QVariantMap{
                        {QStringLiteral("index"), l},
                        {QStringLiteral("kind"), QString(QChar::fromLatin1(line.kind))},
                        {QStringLiteral("text"), decode(line.content)},
                    });
                }
                const QString id = QStringLiteral("%1:%2:%3").arg(commit.sha.left(12)).arg(f).arg(h);
                pieces.append(QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("hunk"), id},
                    {QStringLiteral("commit"), commit.sha},
                    {QStringLiteral("path"), file.path()},
                    {QStringLiteral("isNew"), file.isNew},
                    {QStringLiteral("isDeleted"), file.isDeleted},
                    {QStringLiteral("header"), decode(diff.header())},
                    {QStringLiteral("section"), decode(diff.section).trimmed()},
                    {QStringLiteral("line"), first > 0 ? first : diff.newStart},
                    {QStringLiteral("added"), hunkAdded},
                    {QStringLiteral("removed"), hunkRemoved},
                    {QStringLiteral("lines"), lines},
                    {QStringLiteral("split"), false},
                    {QStringLiteral("changedLines"), lines.size()},
                    {QStringLiteral("cluster"), -1},
                });
                added += hunkAdded;
                removed += hunkRemoved;
            }
        }
        list.append(QVariantMap{
            {QStringLiteral("sha"), commit.sha},
            {QStringLiteral("shortSha"), commit.shortSha},
            {QStringLiteral("time"), commit.time},
            {QStringLiteral("subject"), commit.subject},
            {QStringLiteral("published"), commit.published},
            {QStringLiteral("added"), added},
            {QStringLiteral("removed"), removed},
            {QStringLiteral("files"), files},
            {QStringLiteral("pieces"), pieces},
        });
    }
    return list;
}

int ChangeClusters::clusterIndex(int id) const
{
    for (int i = 0; i < m_clusters.size(); ++i) {
        if (m_clusters[i].id == id)
            return i;
    }
    return -1;
}

ChangeClusters::Hunk *ChangeClusters::hunk(const QString &id)
{
    for (Hunk &hunk : m_hunks) {
        if (hunk.id == id)
            return &hunk;
    }
    return nullptr;
}

const ChangeClusters::Hunk *ChangeClusters::hunk(const QString &id) const
{
    return const_cast<ChangeClusters *>(this)->hunk(id);
}

QList<int> ChangeClusters::clustersOf(const Hunk &hunk)
{
    QList<int> clusters;
    for (const Line &line : hunk.lines) {
        if (!clusters.contains(line.cluster))
            clusters.append(line.cluster);
    }
    return clusters;
}

QString ChangeClusters::pieceId(const QString &hunkId, int cluster)
{
    return hunkId + u'/' + (cluster < 0 ? QStringLiteral("none") : QString::number(cluster));
}

std::pair<QString, int> ChangeClusters::parsePiece(const QString &pieceId)
{
    const qsizetype slash = pieceId.lastIndexOf(u'/');
    if (slash < 0)
        return {pieceId, -2}; // a whole hunk
    const QString cluster = pieceId.mid(slash + 1);
    return {pieceId.left(slash), cluster == QLatin1String("none") ? -1 : cluster.toInt()};
}

QVariantList ChangeClusters::buildHunks() const
{
    QVariantList list;
    for (const Hunk &hunk : m_hunks) {
        const FileDiff &file = m_diff[hunk.file];
        const DiffHunk &diff = file.hunks[hunk.index];
        const QList<int> pieces = clustersOf(hunk);
        for (int cluster : pieces) {
            int added = 0;
            int removed = 0;
            int first = -1;
            QVariantList lines;
            for (const Line &line : hunk.lines) {
                if (line.cluster != cluster)
                    continue;
                const DiffLine &diffLine = diff.lines[line.index];
                added += diffLine.kind == DiffLine::Added;
                removed += diffLine.kind == DiffLine::Removed;
                if (first < 0)
                    first = diffLine.newLine > 0 ? diffLine.newLine : diffLine.oldLine;
                lines.append(QVariantMap{
                    {QStringLiteral("index"), line.index},
                    {QStringLiteral("kind"), QString(QChar::fromLatin1(diffLine.kind))},
                    {QStringLiteral("text"), decode(diffLine.content)},
                });
            }
            const QString id = pieceId(hunk.id, cluster);
            list.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("hunk"), hunk.id},
                {QStringLiteral("path"), file.path()},
                {QStringLiteral("isNew"), file.isNew},
                {QStringLiteral("isDeleted"), file.isDeleted},
                {QStringLiteral("header"), decode(diff.header())},
                {QStringLiteral("section"), decode(diff.section).trimmed()},
                {QStringLiteral("line"), first > 0 ? first : diff.newStart},
                {QStringLiteral("added"), added},
                {QStringLiteral("removed"), removed},
                {QStringLiteral("lines"), lines},
                {QStringLiteral("split"), pieces.size() > 1},
                {QStringLiteral("changedLines"), hunk.lines.size()},
                {QStringLiteral("cluster"), cluster},
            });
        }
    }
    return list;
}

QVariantList ChangeClusters::buildClusters(const QHash<QString, QVariant> &pieces) const
{
    QVariantList list;
    for (const Cluster &cluster : m_clusters) {
        QStringList ids;
        QVariantList own;
        QSet<QString> paths;
        int added = 0;
        int removed = 0;
        for (const Hunk &hunk : m_hunks) {
            bool any = false;
            for (const Line &line : hunk.lines) {
                if (line.cluster != cluster.id)
                    continue;
                any = true;
                const DiffLine::Kind kind = m_diff[hunk.file].hunks[hunk.index].lines[line.index].kind;
                added += kind == DiffLine::Added;
                removed += kind == DiffLine::Removed;
            }
            if (any) {
                ids.append(pieceId(hunk.id, cluster.id));
                own.append(pieces.value(ids.last()));
                paths.insert(m_diff[hunk.file].path());
            }
        }
        QStringList files(paths.cbegin(), paths.cend());
        files.sort();
        list.append(QVariantMap{
            {QStringLiteral("id"), cluster.id},
            {QStringLiteral("name"), title(cluster.message)},
            {QStringLiteral("message"), cluster.message},
            {QStringLiteral("color"), cluster.color},
            {QStringLiteral("pinned"), cluster.pinned},
            {QStringLiteral("hunks"), ids},
            {QStringLiteral("pieces"), own},
            {QStringLiteral("files"), files},
            {QStringLiteral("added"), added},
            {QStringLiteral("removed"), removed},
        });
    }
    return list;
}

QVariantList ChangeClusters::buildFiles(const QHash<QString, QVariant> &pieces) const
{
    QVariantList list;
    int current = -1;
    QStringList ids;
    QVariantList own;
    const auto flush = [&] {
        if (current >= 0)
            list.append(QVariantMap{{QStringLiteral("path"), m_diff[current].path()},
                                    {QStringLiteral("hunks"), ids}, {QStringLiteral("pieces"), own}});
        ids.clear();
        own.clear();
    };
    for (const Hunk &hunk : m_hunks) {
        if (hunk.file != current) {
            flush();
            current = hunk.file;
        }
        for (int cluster : clustersOf(hunk)) {
            ids.append(pieceId(hunk.id, cluster));
            own.append(pieces.value(ids.last()));
        }
    }
    flush();
    return list;
}

int ChangeClusters::unassignedCount() const
{
    int count = 0;
    for (const Hunk &hunk : m_hunks)
        count += clustersOf(hunk).contains(-1);
    return count;
}

int ChangeClusters::addCluster(const QString &name)
{
    Cluster cluster;
    cluster.id = m_nextId++;
    cluster.message = name.isEmpty() ? tr("Cluster %1").arg(cluster.id) : name;
    cluster.color = m_nextColor++ % kColors;
    m_clusters.append(cluster);
    notifyChanged();
    return cluster.id;
}

void ChangeClusters::removeCluster(int id)
{
    const int index = clusterIndex(id);
    if (index < 0)
        return;
    m_clusters.removeAt(index);
    for (Hunk &hunk : m_hunks) {
        for (Line &line : hunk.lines) {
            if (line.cluster == id)
                setLineCluster(line, -1);
        }
    }
    notifyChanged();
}

void ChangeClusters::renameCluster(int id, const QString &name)
{
    const int index = clusterIndex(id);
    if (index < 0)
        return;
    QString &message = m_clusters[index].message;
    const qsizetype newline = message.indexOf(u'\n');
    const QString renamed = name.trimmed() + (newline < 0 ? QString() : message.mid(newline));
    if (renamed == message)
        return;
    message = renamed;
    notifyChanged();
}

void ChangeClusters::setClusterMessage(int id, const QString &message)
{
    const int index = clusterIndex(id);
    if (index < 0 || m_clusters[index].message == message)
        return;
    m_clusters[index].message = message;
    notifyChanged();
}

QString ChangeClusters::clusterMessage(int id) const
{
    const int index = clusterIndex(id);
    return index < 0 ? QString() : m_clusters[index].message;
}

void ChangeClusters::setClusterPinned(int id, bool pinned)
{
    const int index = clusterIndex(id);
    if (index < 0 || m_clusters[index].pinned == pinned)
        return;
    m_clusters[index].pinned = pinned;
    notifyChanged();
}

void ChangeClusters::moveCluster(int id, int index)
{
    const int from = clusterIndex(id);
    if (from < 0)
        return;
    const int to = std::clamp(index, 0, int(m_clusters.size()) - 1);
    if (from == to)
        return;
    m_clusters.move(from, to);
    notifyChanged();
}

void ChangeClusters::setLineCluster(Line &line, int cluster)
{
    line.cluster = cluster;
    if (cluster < 0)
        m_clusterOf.remove(line.key);
    else
        m_clusterOf.insert(line.key, cluster);
}

void ChangeClusters::assign(const QString &pieceId, int clusterId)
{
    if (clusterId >= 0 && clusterIndex(clusterId) < 0)
        clusterId = -1;
    const auto [hunkId, from] = parsePiece(pieceId);
    Hunk *h = hunk(hunkId);
    if (!h)
        return;
    bool any = false;
    for (Line &line : h->lines) {
        if ((from == -2 || line.cluster == from) && line.cluster != clusterId) {
            setLineCluster(line, clusterId);
            any = true;
        }
    }
    if (any)
        notifyChanged();
}

void ChangeClusters::assignFile(const QString &path, int clusterId)
{
    if (clusterId >= 0 && clusterIndex(clusterId) < 0)
        return;
    bool any = false;
    for (Hunk &hunk : m_hunks) {
        if (m_diff[hunk.file].path() != path)
            continue;
        for (Line &line : hunk.lines) {
            if (line.cluster != clusterId) {
                setLineCluster(line, clusterId);
                any = true;
            }
        }
    }
    if (any)
        notifyChanged();
}

void ChangeClusters::assignLines(const QString &hunkId, const QList<int> &lines, int clusterId)
{
    if (clusterId >= 0 && clusterIndex(clusterId) < 0)
        clusterId = -1;
    Hunk *h = hunk(hunkId);
    if (!h)
        return;
    bool any = false;
    for (Line &line : h->lines) {
        if (lines.contains(line.index) && line.cluster != clusterId) {
            setLineCluster(line, clusterId);
            any = true;
        }
    }
    if (any)
        notifyChanged();
}

QByteArray ChangeClusters::clusterPatch(int id) const
{
    QByteArray patch;
    for (int f = 0; f < m_diff.size(); ++f) {
        const FileDiff &file = m_diff[f];
        LineSelection selection(file.hunks.size());
        bool any = false;
        for (const Hunk &hunk : m_hunks) {
            if (hunk.file != f)
                continue;
            QList<bool> chosen(file.hunks[hunk.index].lines.size(), false);
            for (const Line &line : hunk.lines) {
                if (line.cluster == id) {
                    chosen[line.index] = true;
                    any = true;
                }
            }
            selection[hunk.index] = chosen;
        }
        if (any)
            patch += buildPatch(file, selection, PatchDirection::Forward);
    }
    return patch;
}

bool ChangeClusters::commitCluster(int id)
{
    const int index = clusterIndex(id);
    if (index < 0)
        return false;
    // The patch is against the base; committed on HEAD, it has to be HEAD
    const auto commitOf = [this](const QString &rev) {
        return m_git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                          rev + QStringLiteral("^{commit}")}).out.trimmed();
    };
    if (m_base != QLatin1String("HEAD") && commitOf(m_base) != commitOf(QStringLiteral("HEAD"))) {
        setError(tr("The changes are read against %1, not HEAD; commit them through the plan.").arg(m_base));
        return false;
    }
    const QByteArray patch = clusterPatch(id);
    if (patch.isEmpty()) {
        setError(tr("“%1” has no changes to commit.").arg(title(m_clusters[index].message)));
        return false;
    }
    QString message = m_clusters[index].message.trimmed();
    QString error;
    const QString sha = ::commitPatch(m_git, patch, message, error);
    if (sha.isEmpty()) {
        setError(error);
        return false;
    }
    committed(id);
    // Committed, but something after it went wrong
    if (!error.isEmpty())
        setError(error);
    emit clusterCommitted(sha);
    return true;
}

QString ChangeClusters::lineKey(const QString &path, const QByteArray &content, int occurrence)
{
    return hash(path.toUtf8() + '\0' + content + '\0' + QByteArray::number(occurrence));
}

QByteArray ChangeClusters::lineContent(const Hunk &hunk, const Line &line) const
{
    const DiffLine &diffLine = m_diff[hunk.file].hunks[hunk.index].lines[line.index];
    return char(diffLine.kind) + diffLine.content;
}

void ChangeClusters::committed(int id, const QString &base)
{
    const int index = clusterIndex(id);
    if (index >= 0)
        m_clusters.removeAt(index);

    // A line is known by how often the same change came before it in its
    // file. Once the committed lines are gone, a line after one of them
    // with the same change counts one less: its key is made anew from
    // the lines that stay, so it keeps its cluster.
    QHash<QString, int> kept;
    int file = -1;
    QHash<QByteArray, int> seen;
    for (const Hunk &hunk : std::as_const(m_hunks)) {
        if (hunk.file != file) {
            file = hunk.file;
            seen.clear();
        }
        for (const Line &line : hunk.lines) {
            if (line.cluster == id)
                continue;
            const QByteArray content = lineContent(hunk, line);
            const int occurrence = seen[content]++;
            if (line.cluster >= 0)
                kept.insert(lineKey(m_diff[hunk.file].path(), content, occurrence), line.cluster);
        }
    }
    m_clusterOf = kept;

    if (!base.isEmpty() && base != m_base) {
        m_base = base;
        emit baseChanged();
    }
    reload();
}

bool ChangeClusters::commitAll()
{
    // By id, as each commit reloads
    QList<int> order;
    for (const Cluster &cluster : std::as_const(m_clusters)) {
        if (!clusterPatch(cluster.id).isEmpty())
            order.append(cluster.id);
    }
    for (int id : std::as_const(order)) {
        if (!commitCluster(id))
            return false;
    }
    return true;
}
