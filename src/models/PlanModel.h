#pragma once

#include "core/PlanSession.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// The pending actions of a plan, as last simulated.
class PlanModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool executable READ executable NOTIFY changed)
    // The commands executing the plan will run.
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    enum Roles {
        EntryIdRole = Qt::UserRole + 1,
        TitleRole,
        LogRole,
        ErrorRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setEntries(const QList<PlanEntry> &entries, bool executable, const QString &summary);
    void clear() { setEntries({}, false, {}); }

    int count() const { return int(m_entries.size()); }
    bool executable() const { return m_executable; }
    QString summary() const { return m_summary; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();

private:
    QList<PlanEntry> m_entries;
    bool m_executable = false;
    QString m_summary;
};
