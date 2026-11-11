#include "PatchCommit.h"

#include "DiffParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("PatchCommit", text);
}

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out).section(QLatin1Char('\n'), 0, 0).trimmed();
}

} // namespace

QStringList patchPaths(const QByteArray &patch)
{
    QStringList paths;
    for (const FileDiff &file : parseDiff(patch)) {
        for (const QString &path : {file.oldPath, file.newPath}) {
            if (!path.isEmpty() && !paths.contains(path))
                paths.append(path);
        }
    }
    return paths;
}

QString treeWithPatch(const GitRunner &git, const QString &base, const QByteArray &patch, QString &error)
{
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        error = tr("Cannot make a scratch index: %1").arg(scratch.errorString());
        return {};
    }
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), QDir(scratch.path()).filePath(QStringLiteral("index")));
    const GitResult read = git.run(base.isEmpty() ? QStringList{QStringLiteral("read-tree"), QStringLiteral("--empty")}
                                                  : QStringList{QStringLiteral("read-tree"), base},
                                   {}, env);
    if (!read.ok()) {
        error = read.errorText();
        return {};
    }
    const GitResult applied = git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                       QStringLiteral("--whitespace=nowarn")}, patch, env);
    if (!applied.ok()) {
        error = applied.errorText();
        return {};
    }
    const GitResult tree = git.run({QStringLiteral("write-tree")}, {}, env);
    if (!tree.ok()) {
        error = tree.errorText();
        return {};
    }
    return firstLine(tree.out);
}

QString commitPatch(const GitRunner &git, const QByteArray &patch, const QString &message, QString &error)
{
    if (patch.isEmpty()) {
        error = tr("There are no changes to commit.");
        return {};
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        error = tr("Cannot make a scratch index: %1").arg(scratch.errorString());
        return {};
    }
    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_INDEX_FILE"), QDir(scratch.path()).filePath(QStringLiteral("index")));
    const bool unborn = !git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"),
                                  QStringLiteral("HEAD")}).ok();
    const GitResult read = git.run(unborn ? QStringList{QStringLiteral("read-tree"), QStringLiteral("--empty")}
                                          : QStringList{QStringLiteral("read-tree"), QStringLiteral("HEAD")},
                                   {}, env);
    if (!read.ok()) {
        error = read.errorText();
        return {};
    }
    if (const GitResult r = git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                     QStringLiteral("--whitespace=nowarn")}, patch, env);
        !r.ok()) {
        error = r.errorText();
        return {};
    }
    if (const GitResult r = git.run({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-F"),
                                     QStringLiteral("-")}, message.toUtf8() + '\n', env);
        !r.ok()) {
        error = r.errorText();
        return {};
    }
    git.run(QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--")}
            + patchPaths(patch));
    return firstLine(git.run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).out);
}
