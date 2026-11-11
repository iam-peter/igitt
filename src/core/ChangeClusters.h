#pragma once

#include "DiffParser.h"
#include "GitRunner.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Commits composed from change clusters instead of the staging area: every
// changed line between a base (HEAD) and the working tree can go into a
// cluster, a logical unit of work such as a feature or a fix, whatever
// file it is in. A cluster becomes a commit of exactly its lines.
//
// The units the views show are pieces: the changed lines of a hunk that
// are in the same cluster, or in none. A hunk is one piece until some of
// its lines go elsewhere, then one per cluster its lines are in.
//
// Lines keep their cluster across reloads by their file, content and
// order, not their line numbers, which move as other clusters are
// committed before them.
//
// The views (a list of groups, a node graph, a canvas, a stack of future
// commits) share this model and differ only in how they show it; each may
// keep positions of pieces and clusters here so they survive a reload.
class ChangeClusters : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    // What the changes are against: HEAD, or a planned commit while
    // planning
    Q_PROPERTY(QString base READ base WRITE setBase NOTIFY baseChanged FINAL)
    // The pieces: {id, hunk, path, isNew, isDeleted, header, section, line,
    // added, removed, lines: [{kind, text}], split, cluster, x, y}; split
    // says the hunk's other lines are in other pieces
    Q_PROPERTY(QVariantList hunks READ hunks NOTIFY changed FINAL)
    // {id, name, message, color, hunks: [piece ids], added, removed, files,
    // x, y, width, height}, in the order they are committed; name is the
    // message's title
    Q_PROPERTY(QVariantList clusters READ clusters NOTIFY changed FINAL)
    // The changed files, each with the ids of its pieces: {path, hunks}
    Q_PROPERTY(QVariantList files READ files NOTIFY changed FINAL)
    Q_PROPERTY(int unassignedCount READ unassignedCount NOTIFY changed FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    // What the clusters are committed on: its short id and subject
    Q_PROPERTY(QString headSha READ headSha NOTIFY changed FINAL)
    Q_PROPERTY(QString headSubject READ headSubject NOTIFY changed FINAL)

public:
    explicit ChangeClusters(QObject *parent = nullptr);

    QString path() const { return m_git.workDir(); }
    void setPath(const QString &path);
    QString base() const { return m_base; }
    void setBase(const QString &base);

    QVariantList hunks() const;
    QVariantList clusters() const;
    QVariantList files() const;
    int unassignedCount() const;
    QString error() const { return m_error; }
    QString headSha() const { return m_headSha; }
    QString headSubject() const { return m_headSubject; }

    // Reads the changes again; lines that are still there keep their
    // cluster, pieces their position; clusters left empty stay.
    Q_INVOKABLE void reload();

    // Returns the new cluster's id.
    Q_INVOKABLE int addCluster(const QString &name);
    Q_INVOKABLE void removeCluster(int id);
    // Sets the message's title and keeps the rest.
    Q_INVOKABLE void renameCluster(int id, const QString &name);
    // The whole message: title, body and trailers.
    Q_INVOKABLE void setClusterMessage(int id, const QString &message);
    Q_INVOKABLE QString clusterMessage(int id) const;
    // Places the cluster at `index` in the order its commits are made.
    Q_INVOKABLE void moveCluster(int id, int index);

    // Every line of a piece into a cluster; -1 takes it out of its cluster.
    Q_INVOKABLE void assign(const QString &pieceId, int clusterId);
    // Every line of a file into a cluster.
    Q_INVOKABLE void assignFile(const QString &path, int clusterId);
    // Some changed lines of a hunk, by their index in it, into a cluster.
    Q_INVOKABLE void assignLines(const QString &hunkId, const QList<int> &lines, int clusterId);
    // A hunk with every line and the cluster each changed line is in:
    // {id, path, line, lines: [{index, kind, text, cluster, oldLine, newLine}]}
    Q_INVOKABLE QVariantMap hunkLines(const QString &hunkId) const;

    Q_INVOKABLE void setHunkPosition(const QString &pieceId, qreal x, qreal y);
    Q_INVOKABLE void setClusterGeometry(int id, qreal x, qreal y, qreal width, qreal height);

    // The patch a cluster commits, against the base.
    Q_INVOKABLE QByteArray clusterPatch(int id) const;

    // Makes a commit of the cluster's lines on HEAD with its message, and
    // removes the cluster. Returns false and sets error if git refuses.
    Q_INVOKABLE bool commitCluster(int id);
    // Every cluster that has lines, in order, each a commit on the one
    // before.
    Q_INVOKABLE bool commitAll();
    // A cluster was committed some other way (Repository::commitPatch()):
    // it goes, and the changes are read again, against `base` if given (a
    // planned commit). The other clusters keep their lines, also where the
    // committed one had the same change earlier in a file.
    Q_INVOKABLE void committed(int id, const QString &base = {});
    Q_INVOKABLE void setError(const QString &error);

signals:
    void pathChanged();
    void baseChanged();
    void changed();
    void errorChanged();
    // After commitCluster(), so the history can be read again.
    void clusterCommitted(const QString &sha);

private:
    struct Line
    {
        int index = 0; // in its hunk's lines
        QString key;   // stays the same across reloads
        int cluster = -1;
    };
    struct Hunk
    {
        QString id;
        int file = 0;      // index into m_diff
        int index = 0;     // index into its file's hunks
        QList<Line> lines; // the changed ones
    };
    struct Cluster
    {
        int id = 0;
        QString message;
        int color = 0;
        QRectF geometry;
    };

    int clusterIndex(int id) const;
    Hunk *hunk(const QString &id);
    const Hunk *hunk(const QString &id) const;
    // The clusters of a hunk's lines, in their order, -1 for none
    static QList<int> clustersOf(const Hunk &hunk);
    static QString pieceId(const QString &hunkId, int cluster);
    // A piece id's hunk and cluster
    static std::pair<QString, int> parsePiece(const QString &pieceId);
    void setLineCluster(Line &line, int cluster);
    static QString lineKey(const QString &path, const QByteArray &content, int occurrence);
    QByteArray lineContent(const Hunk &hunk, const Line &line) const;
    // A piece's position goes with its lines to the piece they make up now
    void movePosition(const QString &from, const QString &to);

    GitRunner m_git;
    QString m_base = QStringLiteral("HEAD");
    QList<FileDiff> m_diff;
    QList<Hunk> m_hunks;
    QList<Cluster> m_clusters;
    // What reload() keeps of lines and pieces that come and go
    QHash<QString, int> m_clusterOf;
    QHash<QString, QPointF> m_positionOf;
    int m_nextId = 1;
    int m_nextColor = 0;
    QString m_error;
    QString m_headSha;
    QString m_headSubject;
};
