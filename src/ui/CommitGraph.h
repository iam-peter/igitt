#pragma once

#include "core/GraphLayout.h"

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Paints the graph lanes of one history row and its commit node.
class CommitGraph : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariant row READ row WRITE setRow NOTIFY changed)
    Q_PROPERTY(qreal laneWidth READ laneWidth WRITE setLaneWidth NOTIFY changed)
    Q_PROPERTY(NodeKind nodeKind READ nodeKind WRITE setNodeKind NOTIFY changed)
    // Shown in the node, GitKraken style, if `showInitials` is set.
    Q_PROPERTY(QString initials READ initials WRITE setInitials NOTIFY changed)
    Q_PROPERTY(bool showInitials READ showInitials WRITE setShowInitials NOTIFY changed)
    // Draws a line from the left edge to the node, continuing the line
    // from the row's ref labels.
    Q_PROPERTY(bool connectLeft READ connectLeft WRITE setConnectLeft NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)

public:
    enum NodeKind { Commit, Head, Merge, WorkingTree };
    Q_ENUM(NodeKind)

    explicit CommitGraph(QQuickItem *parent = nullptr);

    QVariant row() const { return QVariant::fromValue(m_row); }
    void setRow(const QVariant &row);
    qreal laneWidth() const { return m_laneWidth; }
    void setLaneWidth(qreal width);
    NodeKind nodeKind() const { return m_nodeKind; }
    void setNodeKind(NodeKind kind);
    QString initials() const { return m_initials; }
    void setInitials(const QString &initials);
    bool showInitials() const { return m_showInitials; }
    void setShowInitials(bool show);
    bool connectLeft() const { return m_connectLeft; }
    void setConnectLeft(bool connect);
    QColor outlineColor() const { return m_outlineColor; }
    void setOutlineColor(const QColor &color);

    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    template <typename T>
    void set(T &member, const T &value);
    void updateImplicitWidth();

    GraphRow m_row;
    qreal m_laneWidth = 22;
    NodeKind m_nodeKind = Commit;
    QString m_initials;
    bool m_showInitials = true;
    bool m_connectLeft = false;
    QColor m_outlineColor = Qt::white;
};
