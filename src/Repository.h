#pragma once

#include "core/GitRunner.h"
#include "models/CommitModel.h"
#include "models/DiffModel.h"
#include "models/FileListModel.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

// The repository as the UI sees it: history, working tree status and the
// diff currently on display, plus the operations that change them.
class Repository : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY pathChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY headChanged)
    Q_PROPERTY(QString headSha READ headSha NOTIFY headChanged)
    Q_PROPERTY(QString upstream READ upstream NOTIFY headChanged)
    Q_PROPERTY(int ahead READ ahead NOTIFY headChanged)
    Q_PROPERTY(int behind READ behind NOTIFY headChanged)
    Q_PROPERTY(CommitModel *commits READ commits CONSTANT)
    Q_PROPERTY(FileListModel *unstaged READ unstaged CONSTANT)
    Q_PROPERTY(FileListModel *staged READ staged CONSTANT)
    Q_PROPERTY(DiffModel *diff READ diff CONSTANT)
    Q_PROPERTY(DiffSource diffSource READ diffSource NOTIFY diffChanged)
    Q_PROPERTY(QString diffPath READ diffPath NOTIFY diffChanged)
    Q_PROPERTY(QString diffCommit READ diffCommit NOTIFY diffChanged)
    Q_PROPERTY(QString commitDetails READ commitDetails NOTIFY diffChanged)

public:
    enum DiffSource { NoDiff, UnstagedDiff, StagedDiff, CommitDiff };
    Q_ENUM(DiffSource)

    explicit Repository(QObject *parent = nullptr);

    QString path() const { return m_git.workDir(); }
    bool valid() const { return !m_git.workDir().isEmpty(); }
    QString branch() const { return m_status.branch; }
    QString headSha() const { return m_status.headSha; }
    QString upstream() const { return m_status.upstream; }
    int ahead() const { return m_status.ahead; }
    int behind() const { return m_status.behind; }
    CommitModel *commits() { return &m_commits; }
    FileListModel *unstaged() { return &m_unstaged; }
    FileListModel *staged() { return &m_staged; }
    DiffModel *diff() { return &m_diff; }
    DiffSource diffSource() const { return m_diffSource; }
    QString diffPath() const { return m_diffPath; }
    QString diffCommit() const { return m_diffCommit; }
    QString commitDetails() const { return m_commitDetails; }

    // Accepts a local path or a file:// URL anywhere inside a work tree.
    Q_INVOKABLE bool open(const QString &pathOrUrl);
    // Reloads refs, history and status.
    Q_INVOKABLE void refresh();
    // Reloads status and the file diff on display; reloads history only if
    // HEAD moved.
    Q_INVOKABLE void refreshStatus();

    Q_INVOKABLE void showCommit(const QString &sha);
    Q_INVOKABLE void showFile(const QString &path, bool staged);

    Q_INVOKABLE bool stageFile(const QString &path);
    Q_INVOKABLE bool unstageFile(const QString &path);
    Q_INVOKABLE bool stageAll();
    Q_INVOKABLE bool unstageAll();
    // Stages the selected lines of the unstaged diff, or unstages the
    // selected lines of the staged diff.
    Q_INVOKABLE bool applySelection();

    Q_INVOKABLE bool commit(const QString &message, bool amend);
    Q_INVOKABLE QString headMessage() const;

signals:
    void pathChanged();
    void headChanged();
    void diffChanged();
    void errorOccurred(const QString &message);

private:
    bool check(const GitResult &result, const QString &what);
    void loadHistory();
    void loadStatus();
    void reloadDiff();
    QStringList diffArgs() const;

    GitRunner m_git;
    StatusInfo m_status;
    QString m_historyHead;
    CommitModel m_commits;
    FileListModel m_unstaged{false};
    FileListModel m_staged{true};
    DiffModel m_diff;
    DiffSource m_diffSource = NoDiff;
    QString m_diffPath;
    QString m_diffCommit;
    QString m_commitDetails;
};
