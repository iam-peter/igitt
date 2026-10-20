#include "PlanModel.h"

void PlanModel::setEntries(const QList<PlanEntry> &entries, bool executable, const QString &summary)
{
    beginResetModel();
    m_entries = entries;
    m_executable = executable;
    m_summary = summary;
    endResetModel();
    emit changed();
}

int PlanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PlanModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const PlanEntry &entry = m_entries[index.row()];
    switch (role) {
    case EntryIdRole: return entry.id;
    case TitleRole: return entry.title;
    case LogRole: return entry.log.trimmed();
    case ErrorRole: return entry.error;
    }
    return {};
}

QHash<int, QByteArray> PlanModel::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},
        {TitleRole, "title"},
        {LogRole, "log"},
        {ErrorRole, "error"},
    };
}
