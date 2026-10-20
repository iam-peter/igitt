#pragma once

#include "core/MergeConflict.h"

#include <QObject>
#include <QThreadPool>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

// One conflicted file for the merge tool: the chunks of both sides, which
// side to keep per conflict, and the output that makes.
class ConflictResolver : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString workDir READ workDir WRITE setWorkDir NOTIFY sourceChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY sourceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY loaded)
    Q_PROPERTY(QString error READ error NOTIFY loaded)
    Q_PROPERTY(QString operation READ operation NOTIFY loaded)
    Q_PROPERTY(QString oursLabel READ oursLabel NOTIFY loaded)
    Q_PROPERTY(QString theirsLabel READ theirsLabel NOTIFY loaded)
    Q_PROPERTY(bool deletedInOurs READ deletedInOurs NOTIFY loaded)
    Q_PROPERTY(bool deletedInTheirs READ deletedInTheirs NOTIFY loaded)
    // [{conflict, conflictIndex, text, ours, base, theirs}]; texts are
    // joined lines. conflictIndex is -1 for lines both sides agree on.
    Q_PROPERTY(QVariantList chunks READ chunks NOTIFY loaded)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY loaded)
    // [{ours, theirs}] per conflict.
    Q_PROPERTY(QVariantList choices READ choices NOTIFY choicesChanged)
    Q_PROPERTY(int unresolvedCount READ unresolvedCount NOTIFY choicesChanged)
    Q_PROPERTY(QString output READ output NOTIFY choicesChanged)

public:
    explicit ConflictResolver(QObject *parent = nullptr);
    ~ConflictResolver() override;

    QString workDir() const { return m_workDir; }
    void setWorkDir(const QString &dir);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool busy() const { return m_busy; }
    QString error() const { return m_file.error; }
    QString operation() const { return m_file.operation; }
    QString oursLabel() const { return m_file.oursLabel; }
    QString theirsLabel() const { return m_file.theirsLabel; }
    bool deletedInOurs() const { return m_file.deletedInOurs; }
    bool deletedInTheirs() const { return m_file.deletedInTheirs; }
    QVariantList chunks() const;
    int conflictCount() const { return int(m_choices.size()); }
    QVariantList choices() const;
    int unresolvedCount() const;
    QString output() const { return resolveConflict(m_file, m_choices); }

    // Takes (or drops) one side of a conflict. With both sides taken, the
    // one taken first comes first in the output.
    Q_INVOKABLE void choose(int conflict, bool theirs, bool take);
    Q_INVOKABLE void takeAll(bool theirs);
    Q_INVOKABLE void reload();

signals:
    void sourceChanged();
    void loaded();
    void choicesChanged();

private:
    QThreadPool m_pool;
    int m_generation = 0;
    bool m_busy = false;
    QString m_workDir;
    QString m_path;
    ConflictFile m_file;
    QList<ConflictChoice> m_choices;
};
