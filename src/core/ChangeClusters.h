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
// hunk between HEAD and the working tree can go into a cluster, a logical
// unit of work such as a feature or a fix, whatever file it is in. A
// cluster becomes a commit of exactly its hunks, built on a scratch index,
// so neither the working tree nor the hunks of other clusters are touched.
//
// Hunks keep their cluster across reloads by an id made of their path and
// changed lines, not their line numbers, which move as other clusters are
// committed before them.
//
// The views (a list of groups, a node graph, a canvas, a stack of future
// commits) share this model and differ only in how they show it; each may
// keep positions of hunks and clusters here so they survive a reload.
class ChangeClusters : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    // {id, path, header, added, removed, lines: [{kind, text}], cluster, x, y}
    Q_PROPERTY(QVariantList hunks READ hunks NOTIFY changed FINAL)
    // {id, name, color, hunks: [hunk ids], added, removed, files, x, y, width, height}
    Q_PROPERTY(QVariantList clusters READ clusters NOTIFY changed FINAL)
    // The changed files, each with the ids of its hunks: {path, hunks}
    Q_PROPERTY(QVariantList files READ files NOTIFY changed FINAL)
    Q_PROPERTY(int unassignedCount READ unassignedCount NOTIFY changed FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

public:
    explicit ChangeClusters(QObject *parent = nullptr);

    QString path() const { return m_git.workDir(); }
    void setPath(const QString &path);

    QVariantList hunks() const;
    QVariantList clusters() const;
    QVariantList files() const;
    int unassignedCount() const;
    QString error() const { return m_error; }

    // Reads `git diff HEAD` again; hunks that are still there keep their
    // cluster and position, clusters that are left empty stay.
    Q_INVOKABLE void reload();

    // Returns the new cluster's id.
    Q_INVOKABLE int addCluster(const QString &name);
    Q_INVOKABLE void removeCluster(int id);
    Q_INVOKABLE void renameCluster(int id, const QString &name);
    // Places the cluster at `index` in the order its commits are made.
    Q_INVOKABLE void moveCluster(int id, int index);
    // -1 takes the hunk out of its cluster.
    Q_INVOKABLE void assign(const QString &hunkId, int clusterId);
    // Every hunk of a file into a cluster.
    Q_INVOKABLE void assignFile(const QString &path, int clusterId);

    Q_INVOKABLE void setHunkPosition(const QString &hunkId, qreal x, qreal y);
    Q_INVOKABLE void setClusterGeometry(int id, qreal x, qreal y, qreal width, qreal height);

    // Makes a commit of the cluster's hunks on HEAD, with `message`, and
    // removes the cluster. Returns false and sets error if git refuses.
    Q_INVOKABLE bool commitCluster(int id, const QString &message);
    // Every cluster that has hunks, in order, each a commit on the one
    // before, with the cluster's name as its message.
    Q_INVOKABLE bool commitAll();

    // The patch a cluster would commit, against HEAD
    QByteArray clusterPatch(int id) const;

signals:
    void pathChanged();
    void changed();
    void errorChanged();
    // After a commit, so the history can be read again.
    void committed(const QString &sha);

private:
    struct Hunk
    {
        QString id;
        int file = 0;  // index into m_diff
        int index = 0; // index into its file's hunks
        int cluster = -1;
        QPointF position;
    };
    struct Cluster
    {
        int id = 0;
        QString name;
        int color = 0;
        QRectF geometry;
    };

    void setError(const QString &error);
    int clusterIndex(int id) const;
    const Hunk *hunk(const QString &id) const;

    GitRunner m_git;
    QList<FileDiff> m_diff;
    QList<Hunk> m_hunks;
    QList<Cluster> m_clusters;
    // What reload() keeps of hunks that come and go
    QHash<QString, int> m_clusterOf;
    QHash<QString, QPointF> m_positionOf;
    int m_nextId = 1;
    int m_nextColor = 0;
    QString m_error;
};
