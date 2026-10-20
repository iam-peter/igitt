#include "GitRunner.h"

#include <QProcess>

QString GitResult::errorText() const
{
    const QString text = QString::fromUtf8(err).trimmed();
    if (!text.isEmpty())
        return text;
    return QString::fromUtf8(out).trimmed();
}

GitRunner::GitRunner(const QString &workDir)
    : m_workDir(workDir)
{
}

GitResult GitRunner::run(const QStringList &args, const QByteArray &input,
                         const QProcessEnvironment &extraEnv) const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Reads such as `git status` must not take the index lock, or they race
    // with whatever the user is doing in a terminal at the same time.
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    for (const QProcessEnvironment &add : {m_env, extraEnv}) {
        const QStringList keys = add.keys();
        for (const QString &key : keys)
            env.insert(key, add.value(key));
    }

    const QStringList fullArgs = QStringList{
        QStringLiteral("--no-pager"),
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("-c"), QStringLiteral("color.ui=false"),
    } + args;

    QProcess proc;
    proc.setWorkingDirectory(m_workDir);
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("git"), fullArgs);

    GitResult result;
    if (!proc.waitForStarted()) {
        result.err = "could not start git: " + proc.errorString().toUtf8();
        return result;
    }
    if (!input.isEmpty())
        proc.write(input);
    proc.closeWriteChannel();
    proc.waitForFinished(-1);

    result.out = proc.readAllStandardOutput();
    result.err = proc.readAllStandardError();
    result.exitCode = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
    return result;
}
