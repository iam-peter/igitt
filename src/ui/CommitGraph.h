#pragma once

#include "core/GraphLayout.h"

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Paints the graph lanes of one history row.
class CommitGraph : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariant row READ row WRITE setRow NOTIFY rowChanged)
    Q_PROPERTY(qreal laneWidth READ laneWidth WRITE setLaneWidth NOTIFY laneWidthChanged)
    Q_PROPERTY(bool isHead READ isHead WRITE setIsHead NOTIFY isHeadChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY outlineColorChanged)

public:
    explicit CommitGraph(QQuickItem *parent = nullptr);

    QVariant row() const { return QVariant::fromValue(m_row); }
    void setRow(const QVariant &row);
    qreal laneWidth() const { return m_laneWidth; }
    void setLaneWidth(qreal width);
    bool isHead() const { return m_isHead; }
    void setIsHead(bool isHead);
    QColor outlineColor() const { return m_outlineColor; }
    void setOutlineColor(const QColor &color);

    void paint(QPainter *painter) override;

signals:
    void rowChanged();
    void laneWidthChanged();
    void isHeadChanged();
    void outlineColorChanged();

private:
    void updateImplicitWidth();

    GraphRow m_row;
    qreal m_laneWidth = 14;
    bool m_isHead = false;
    QColor m_outlineColor = Qt::white;
};
