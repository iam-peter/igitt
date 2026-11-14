#pragma once

#include "core/FileHistory.h"

#include <QAbstractListModel>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>

// A file through its history, like Perforce's Time-lapse View: pick a
// revision and see the file as it was, each line marked with the revision
// that last changed it, the lines this revision added and changed
// highlighted and, optionally, the ones it deleted. git runs off the UI
// thread.
class Timelapse : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString workDir READ workDir WRITE setWorkDir NOTIFY sourceChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY sourceChanged)
    // [{sha, shortSha, author, date, time, subject, path}], oldest first;
    // time in seconds since the epoch.
    Q_PROPERTY(QVariantList revisions READ revisions NOTIFY revisionsChanged)
    Q_PROPERTY(int revisionCount READ revisionCount NOTIFY revisionsChanged)
    Q_PROPERTY(int revision READ revision WRITE setRevision NOTIFY revisionChanged)
    Q_PROPERTY(bool showDeleted READ showDeleted WRITE setShowDeleted NOTIFY showDeletedChanged)
    // How the lines go from revision to revision, one step for each:
    // [{lines, added, changed, removed, binary, runs: [{kind, oldStart,
    // oldCount, newStart, newCount}]}], kind a LineChange: 0 kept,
    // 1 removed, 2 added, 3 changed.
    // Empty until loadFlow() read it.
    Q_PROPERTY(QVariantList flow READ flow NOTIFY flowChanged)
    // What the revision on display did to each row, a LineChange, for a
    // minimap of the changes
    Q_PROPERTY(QList<int> changes READ changes NOTIFY changesChanged)
    // The rows before which the revision removed lines that are not shown,
    // as deleted lines are hidden
    Q_PROPERTY(QList<int> hiddenRemovals READ hiddenRemovals NOTIFY changesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Roles {
        DeletedRole = Qt::UserRole + 1,
        TextRole,
        LineNoRole,
        RevisionRole,
        ShortShaRole,
        AuthorRole,
        AgeRole, // 0 for the oldest revision, 1 for the newest
        ChangeRole, // what the revision on display did to the line, a LineChange
    };

    explicit Timelapse(QObject *parent = nullptr);
    ~Timelapse() override;

    QString workDir() const { return m_workDir; }
    void setWorkDir(const QString &dir);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantList revisions() const;
    int revisionCount() const { return int(m_revisions.size()); }
    int revision() const { return m_revision; }
    void setRevision(int revision);
    bool showDeleted() const { return m_showDeleted; }
    void setShowDeleted(bool show);
    bool busy() const { return m_pending > 0; }
    QVariantList flow() const { return m_flow; }
    // Reads the flow, unless it is read already.
    Q_INVOKABLE void loadFlow();
    QString error() const { return m_error; }
    QList<int> changes() const { return m_changes; }
    QList<int> hiddenRemovals() const { return m_hiddenRemovals; }

    // Rows and the lines of the file, which differ by the deleted lines
    // shown: the line (0-based, fractional as the row) a row is at, a
    // deleted row at the line it was before; and the row of a line.
    Q_INVOKABLE qreal lineAt(qreal row) const;
    Q_INVOKABLE qreal rowOf(qreal line) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sourceChanged();
    void revisionsChanged();
    void revisionChanged();
    void showDeletedChanged();
    void flowChanged();
    void busyChanged();
    void errorChanged();
    void changesChanged();

private:
    template <typename Result>
    void run(std::function<Result()> job, std::function<void(Result)> done);
    void loadRevisions();
    void loadLines();
    void setError(const QString &error);
    void showLines();

    QThreadPool m_pool;
    int m_pending = 0;
    int m_generation = 0; // drops results for a revision no longer on display
    QString m_workDir;
    QString m_path;
    QList<FileRevision> m_revisions;
    QVariantList m_flow;
    bool m_flowWanted = false;
    int m_flowGeneration = 0; // drops a flow of revisions no longer shown
    int m_revision = -1;
    bool m_showDeleted = true;
    QList<TimelapseLine> m_allLines; // deleted ones included, whether shown or not
    QList<TimelapseLine> m_lines;
    QList<int> m_changes;
    QList<int> m_hiddenRemovals;
    QList<int> m_lineOfRow; // the lines before each row
    QString m_error;
};
