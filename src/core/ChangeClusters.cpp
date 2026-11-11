#include "ChangeClusters.h"

#include "PatchBuilder.h"
#include "PatchCommit.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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
{}

void ChangeClusters::setPath(const QString &path)
{
    if (m_git.workDir() == path)
        return;
    m_git.setWorkDir(path);
    m_clusters.clear();
    m_clusterOf.clear();
    m_positionOf.clear();
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
    if (m_git.workDir().isEmpty()) {
        emit changed();
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
    if (QFile::exists(realIndex))
        QFile::copy(realIndex, scratchIndex);
    const GitResult added = m_git.run({QStringLiteral("add"), QStringLiteral("-A")}, {}, env);
    QStringList diffArgs{QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--no-color"),
                         QStringLiteral("--no-ext-diff"), QStringLiteral("--no-renames")};
    if (!unborn)
        diffArgs.append(m_base);
    const GitResult diff = m_git.run(diffArgs, {}, env);
    if (!scratch.isValid() || !added.ok() || !diff.ok()) {
        setError(!added.ok() ? added.errorText() : diff.errorText());
        emit changed();
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
    setError({});
    emit changed();
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

QVariantList ChangeClusters::hunks() const
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
            const QPointF position = m_positionOf.value(id, QPointF(-1, -1));
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
                {QStringLiteral("x"), position.x()},
                {QStringLiteral("y"), position.y()},
            });
        }
    }
    return list;
}

QVariantList ChangeClusters::clusters() const
{
    QVariantList list;
    for (const Cluster &cluster : m_clusters) {
        QStringList ids;
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
            {QStringLiteral("hunks"), ids},
            {QStringLiteral("files"), files},
            {QStringLiteral("added"), added},
            {QStringLiteral("removed"), removed},
            {QStringLiteral("x"), cluster.geometry.x()},
            {QStringLiteral("y"), cluster.geometry.y()},
            {QStringLiteral("width"), cluster.geometry.width()},
            {QStringLiteral("height"), cluster.geometry.height()},
        });
    }
    return list;
}

QVariantList ChangeClusters::files() const
{
    QVariantList list;
    int current = -1;
    QStringList ids;
    const auto flush = [&] {
        if (current >= 0)
            list.append(QVariantMap{{QStringLiteral("path"), m_diff[current].path()}, {QStringLiteral("hunks"), ids}});
        ids.clear();
    };
    for (const Hunk &hunk : m_hunks) {
        if (hunk.file != current) {
            flush();
            current = hunk.file;
        }
        for (int cluster : clustersOf(hunk))
            ids.append(pieceId(hunk.id, cluster));
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
    emit changed();
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
    emit changed();
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
    emit changed();
}

void ChangeClusters::setClusterMessage(int id, const QString &message)
{
    const int index = clusterIndex(id);
    if (index < 0 || m_clusters[index].message == message)
        return;
    m_clusters[index].message = message;
    emit changed();
}

QString ChangeClusters::clusterMessage(int id) const
{
    const int index = clusterIndex(id);
    return index < 0 ? QString() : m_clusters[index].message;
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
    emit changed();
}

void ChangeClusters::setLineCluster(Line &line, int cluster)
{
    line.cluster = cluster;
    if (cluster < 0)
        m_clusterOf.remove(line.key);
    else
        m_clusterOf.insert(line.key, cluster);
}

void ChangeClusters::movePosition(const QString &from, const QString &to)
{
    const auto it = m_positionOf.constFind(from);
    if (it != m_positionOf.cend() && !m_positionOf.contains(to))
        m_positionOf.insert(to, *it);
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
    if (!any)
        return;
    movePosition(pieceId, ChangeClusters::pieceId(hunkId, clusterId));
    emit changed();
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
        emit changed();
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
        emit changed();
}

void ChangeClusters::setHunkPosition(const QString &pieceId, qreal x, qreal y)
{
    m_positionOf.insert(pieceId, QPointF(x, y));
    emit changed();
}

void ChangeClusters::setClusterGeometry(int id, qreal x, qreal y, qreal width, qreal height)
{
    const int index = clusterIndex(id);
    if (index < 0)
        return;
    m_clusters[index].geometry = QRectF(x, y, width, height);
    emit changed();
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
    const QString sha = commitPatch(m_git, patch, message, error);
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
