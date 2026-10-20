#include "ConflictResolver.h"

#include <QVariantMap>

ConflictResolver::ConflictResolver(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

ConflictResolver::~ConflictResolver()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void ConflictResolver::setWorkDir(const QString &dir)
{
    if (m_workDir == dir)
        return;
    m_workDir = dir;
    emit sourceChanged();
    reload();
}

void ConflictResolver::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit sourceChanged();
    reload();
}

void ConflictResolver::reload()
{
    if (m_workDir.isEmpty() || m_path.isEmpty())
        return;
    const int generation = ++m_generation;
    m_busy = true;
    emit loaded();
    m_pool.start([this, generation, git = GitRunner(m_workDir), path = m_path] {
        ConflictFile file = loadConflict(git, path);
        QMetaObject::invokeMethod(this, [this, generation, file = std::move(file)] {
            if (generation != m_generation)
                return;
            m_file = file;
            m_choices.clear();
            for (const ConflictChunk &chunk : std::as_const(m_file.chunks)) {
                if (chunk.conflict)
                    m_choices.append(ConflictChoice{});
            }
            m_busy = false;
            emit loaded();
            emit choicesChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantList ConflictResolver::chunks() const
{
    QVariantList list;
    int conflict = 0;
    for (const ConflictChunk &chunk : m_file.chunks) {
        list.append(QVariantMap{
            {QStringLiteral("conflict"), chunk.conflict},
            {QStringLiteral("conflictIndex"), chunk.conflict ? conflict++ : -1},
            {QStringLiteral("text"), chunk.lines.join(QLatin1Char('\n'))},
            {QStringLiteral("ours"), chunk.ours.join(QLatin1Char('\n'))},
            {QStringLiteral("base"), chunk.base.join(QLatin1Char('\n'))},
            {QStringLiteral("theirs"), chunk.theirs.join(QLatin1Char('\n'))},
        });
    }
    return list;
}

QVariantList ConflictResolver::choices() const
{
    QVariantList list;
    for (const ConflictChoice &choice : m_choices)
        list.append(QVariantMap{{QStringLiteral("ours"), choice.ours}, {QStringLiteral("theirs"), choice.theirs}});
    return list;
}

int ConflictResolver::unresolvedCount() const
{
    return int(std::count_if(m_choices.cbegin(), m_choices.cend(),
                             [](const ConflictChoice &c) { return !c.ours && !c.theirs; }));
}

void ConflictResolver::choose(int conflict, bool theirs, bool take)
{
    if (conflict < 0 || conflict >= m_choices.size())
        return;
    ConflictChoice &choice = m_choices[conflict];
    bool &side = theirs ? choice.theirs : choice.ours;
    if (side == take)
        return;
    // The side already taken stays first.
    if (take)
        choice.theirsFirst = theirs ? false : choice.theirs;
    side = take;
    emit choicesChanged();
}

void ConflictResolver::takeAll(bool theirs)
{
    for (ConflictChoice &choice : m_choices)
        choice = {!theirs, theirs, false};
    emit choicesChanged();
}
