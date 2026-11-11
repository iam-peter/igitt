#include "ChangeClusters.h"

#include "PatchBuilder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

namespace {

// The palette views pick their colors from, by Cluster::color.
constexpr int kColors = 8;

QString hunkKey(const QString &path, const DiffHunk &hunk)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(path.toUtf8());
    for (const DiffLine &line : hunk.lines) {
        if (line.kind == DiffLine::Added || line.kind == DiffLine::Removed) {
            const char kind = char(line.kind);
            hash.addData(QByteArrayView(&kind, 1));
            hash.addData(line.content);
            hash.addData("\n");
        }
    }
    return QString::fromLatin1(hash.result().toHex().left(16));
}

QString decode(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes);
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

    // Tracked changes against HEAD, staged or not: clusters take the place
    // of the staging area. Then each untracked file as a new one.
    QByteArray text;
    const GitResult tracked = m_git.run({QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("--no-color"),
                                         QStringLiteral("--no-ext-diff"), QStringLiteral("--no-renames")});
    if (!tracked.ok()) {
        setError(tracked.errorText());
        emit changed();
        return;
    }
    text = tracked.out;
    const GitResult others = m_git.run({QStringLiteral("ls-files"), QStringLiteral("--others"),
                                        QStringLiteral("--exclude-standard"), QStringLiteral("-z")});
    for (const QByteArray &name : others.out.split('\0')) {
        if (name.isEmpty())
            continue;
        // --no-index exits 1 when the files differ, which they always do
        const GitResult added = m_git.run({QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--no-color"),
                                           QStringLiteral("--"), QStringLiteral("/dev/null"), QString::fromUtf8(name)});
        text += added.out;
    }
    setError({});
    m_diff = parseDiff(text);

    QHash<QString, int> seen;
    for (int f = 0; f < m_diff.size(); ++f) {
        const FileDiff &file = m_diff[f];
        if (file.isBinary)
            continue;
        for (int h = 0; h < file.hunks.size(); ++h) {
            const QString key = hunkKey(file.path(), file.hunks[h]);
            // The same change twice in a file: tell them apart by order
            const int occurrence = seen[key]++;
            Hunk hunk;
            hunk.id = occurrence == 0 ? key : key + u'-' + QString::number(occurrence);
            hunk.file = f;
            hunk.index = h;
            hunk.cluster = m_clusterOf.value(hunk.id, -1);
            if (clusterIndex(hunk.cluster) < 0)
                hunk.cluster = -1;
            hunk.position = m_positionOf.value(hunk.id, QPointF(-1, -1));
            m_hunks.append(hunk);
        }
    }
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

const ChangeClusters::Hunk *ChangeClusters::hunk(const QString &id) const
{
    for (const Hunk &hunk : m_hunks) {
        if (hunk.id == id)
            return &hunk;
    }
    return nullptr;
}

QVariantList ChangeClusters::hunks() const
{
    QVariantList list;
    for (const Hunk &hunk : m_hunks) {
        const FileDiff &file = m_diff[hunk.file];
        const DiffHunk &diff = file.hunks[hunk.index];
        int added = 0;
        int removed = 0;
        QVariantList lines;
        for (const DiffLine &line : diff.lines) {
            if (line.kind == DiffLine::Added)
                ++added;
            else if (line.kind == DiffLine::Removed)
                ++removed;
            if (line.kind == DiffLine::NoNewline)
                continue;
            lines.append(QVariantMap{
                {QStringLiteral("kind"), QString(QChar::fromLatin1(line.kind))},
                {QStringLiteral("text"), decode(line.content)},
            });
        }
        list.append(QVariantMap{
            {QStringLiteral("id"), hunk.id},
            {QStringLiteral("path"), file.path()},
            {QStringLiteral("isNew"), file.isNew},
            {QStringLiteral("isDeleted"), file.isDeleted},
            {QStringLiteral("header"), decode(diff.header())},
            {QStringLiteral("section"), decode(diff.section).trimmed()},
            {QStringLiteral("line"), diff.newCount > 0 ? diff.newStart : diff.oldStart},
            {QStringLiteral("added"), added},
            {QStringLiteral("removed"), removed},
            {QStringLiteral("lines"), lines},
            {QStringLiteral("cluster"), hunk.cluster},
            {QStringLiteral("x"), hunk.position.x()},
            {QStringLiteral("y"), hunk.position.y()},
        });
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
            if (hunk.cluster != cluster.id)
                continue;
            ids.append(hunk.id);
            paths.insert(m_diff[hunk.file].path());
            for (const DiffLine &line : m_diff[hunk.file].hunks[hunk.index].lines) {
                added += line.kind == DiffLine::Added;
                removed += line.kind == DiffLine::Removed;
            }
        }
        QStringList files(paths.cbegin(), paths.cend());
        files.sort();
        list.append(QVariantMap{
            {QStringLiteral("id"), cluster.id},
            {QStringLiteral("name"), cluster.name},
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
        ids.append(hunk.id);
    }
    flush();
    return list;
}

int ChangeClusters::unassignedCount() const
{
    return int(std::count_if(m_hunks.cbegin(), m_hunks.cend(), [](const Hunk &hunk) { return hunk.cluster < 0; }));
}

int ChangeClusters::addCluster(const QString &name)
{
    Cluster cluster;
    cluster.id = m_nextId++;
    cluster.name = name.isEmpty() ? tr("Cluster %1").arg(cluster.id) : name;
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
        if (hunk.cluster == id) {
            hunk.cluster = -1;
            m_clusterOf.remove(hunk.id);
        }
    }
    emit changed();
}

void ChangeClusters::renameCluster(int id, const QString &name)
{
    const int index = clusterIndex(id);
    if (index < 0 || m_clusters[index].name == name)
        return;
    m_clusters[index].name = name;
    emit changed();
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

void ChangeClusters::assign(const QString &hunkId, int clusterId)
{
    if (clusterId >= 0 && clusterIndex(clusterId) < 0)
        clusterId = -1;
    for (Hunk &hunk : m_hunks) {
        if (hunk.id != hunkId)
            continue;
        if (hunk.cluster == clusterId)
            return;
        hunk.cluster = clusterId;
        if (clusterId < 0)
            m_clusterOf.remove(hunk.id);
        else
            m_clusterOf.insert(hunk.id, clusterId);
        emit changed();
        return;
    }
}

void ChangeClusters::assignFile(const QString &path, int clusterId)
{
    if (clusterId >= 0 && clusterIndex(clusterId) < 0)
        return;
    bool any = false;
    for (Hunk &hunk : m_hunks) {
        if (m_diff[hunk.file].path() != path || hunk.cluster == clusterId)
            continue;
        hunk.cluster = clusterId;
        if (clusterId < 0)
            m_clusterOf.remove(hunk.id);
        else
            m_clusterOf.insert(hunk.id, clusterId);
        any = true;
    }
    if (any)
        emit changed();
}

void ChangeClusters::setHunkPosition(const QString &hunkId, qreal x, qreal y)
{
    for (Hunk &hunk : m_hunks) {
        if (hunk.id == hunkId) {
            hunk.position = QPointF(x, y);
            m_positionOf.insert(hunk.id, hunk.position);
            emit changed();
            return;
        }
    }
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
            const bool chosen = hunk.cluster == id;
            selection[hunk.index] = QList<bool>(file.hunks[hunk.index].lines.size(), chosen);
            any = any || chosen;
        }
        if (any)
            patch += buildPatch(file, selection, PatchDirection::Forward);
    }
    return patch;
}

bool ChangeClusters::commitCluster(int id, const QString &message)
{
    const int index = clusterIndex(id);
    if (index < 0)
        return false;
    const QByteArray patch = clusterPatch(id);
    if (patch.isEmpty()) {
        setError(tr("“%1” has no changes to commit.").arg(m_clusters[index].name));
        return false;
    }

    // HEAD plus the cluster, in an index of its own: the real one and the
    // working tree stay as they are
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        setError(tr("Cannot make a scratch index: %1").arg(scratch.errorString()));
        return false;
    }
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), QDir(scratch.path()).filePath(QStringLiteral("index")));
    const auto fail = [this](const GitResult &result) {
        setError(result.errorText());
        return false;
    };
    if (GitResult r = m_git.run({QStringLiteral("read-tree"), QStringLiteral("HEAD")}, {}, env); !r.ok())
        return fail(r);
    if (GitResult r = m_git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                 QStringLiteral("--whitespace=nowarn")}, patch, env);
        !r.ok())
        return fail(r);
    // git commit rather than commit-tree, so hooks (a Change-Id) and
    // signing apply as in a terminal
    QString text = message.trimmed();
    if (text.isEmpty())
        text = m_clusters[index].name;
    if (GitResult r = m_git.run({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-F"), QStringLiteral("-")},
                                text.toUtf8() + '\n', env);
        !r.ok())
        return fail(r);

    // The real index of the committed files follows HEAD; what else is
    // staged stays staged
    QStringList paths{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--")};
    QSet<QString> seen;
    for (const Hunk &hunk : std::as_const(m_hunks)) {
        const QString path = m_diff[hunk.file].path();
        if (hunk.cluster == id && !seen.contains(path)) {
            seen.insert(path);
            paths.append(path);
        }
    }
    m_git.run(paths);

    const QString sha = QString::fromLatin1(m_git.run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).out.trimmed());
    m_clusters.removeAt(index);
    for (auto it = m_clusterOf.begin(); it != m_clusterOf.end();) {
        if (it.value() == id)
            it = m_clusterOf.erase(it);
        else
            ++it;
    }
    reload();
    emit committed(sha);
    return true;
}

bool ChangeClusters::commitAll()
{
    // By id, as each commit reloads and reorders nothing but its own
    QList<int> order;
    for (const Cluster &cluster : std::as_const(m_clusters)) {
        const bool any = std::any_of(m_hunks.cbegin(), m_hunks.cend(),
                                     [&](const Hunk &hunk) { return hunk.cluster == cluster.id; });
        if (any)
            order.append(cluster.id);
    }
    for (int id : std::as_const(order)) {
        const int index = clusterIndex(id);
        if (index < 0 || !commitCluster(id, m_clusters[index].name))
            return false;
    }
    return true;
}
