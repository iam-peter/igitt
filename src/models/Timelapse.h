#pragma once

#include "core/FileHistory.h"

#include <QAbstractListModel>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>

// A file through its history, like Perforce's Time-lapse View: pick a
// revision and see the file as it was, each line marked with the revision
// that last changed it, the lines this revision changed highlighted and,
// optionally, the ones it deleted. git runs off the UI thread.
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
        ChangedHereRole,
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
    QString error() const { return m_error; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sourceChanged();
    void revisionsChanged();
    void revisionChanged();
    void showDeletedChanged();
    void busyChanged();
    void errorChanged();

private:
    template <typename Result>
    void run(std::function<Result()> job, std::function<void(Result)> done);
    void loadRevisions();
    void loadLines();
    void setError(const QString &error);

    QThreadPool m_pool;
    int m_pending = 0;
    int m_generation = 0; // drops results for a revision no longer on display
    QString m_workDir;
    QString m_path;
    QList<FileRevision> m_revisions;
    int m_revision = -1;
    bool m_showDeleted = true;
    QList<TimelapseLine> m_lines;
    QString m_error;
};
