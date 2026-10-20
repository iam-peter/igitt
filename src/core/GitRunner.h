#pragma once

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

struct GitResult
{
    int exitCode = -1;
    QByteArray out;
    QByteArray err;

    bool ok() const { return exitCode == 0; }
    QString errorText() const;
};

// Runs the git command line client. Every write operation goes through the
// CLI so hooks, config and git's own safety checks apply exactly as they do
// in a terminal.
class GitRunner
{
public:
    explicit GitRunner(const QString &workDir = {});

    QString workDir() const { return m_workDir; }
    void setWorkDir(const QString &dir) { m_workDir = dir; }

    GitResult run(const QStringList &args, const QByteArray &input = {},
                  const QProcessEnvironment &extraEnv = {}) const;

private:
    QString m_workDir;
};
