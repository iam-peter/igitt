#include "CommitGraph.h"

#include <QPainter>
#include <QPainterPath>

namespace {

QColor laneColor(int index)
{
    static const QColor palette[] = {
        QColor(0x4e79a7), QColor(0xf28e2b), QColor(0x59a14f), QColor(0xe15759),
        QColor(0xb07aa1), QColor(0x76b7b2), QColor(0xedc948), QColor(0xff9da7),
        QColor(0x9c755f),
    };
    return palette[index % std::size(palette)];
}

} // namespace

CommitGraph::CommitGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void CommitGraph::setRow(const QVariant &row)
{
    m_row = row.value<GraphRow>();
    updateImplicitWidth();
    update();
    emit rowChanged();
}

void CommitGraph::setLaneWidth(qreal width)
{
    if (qFuzzyCompare(m_laneWidth, width))
        return;
    m_laneWidth = width;
    updateImplicitWidth();
    update();
    emit laneWidthChanged();
}

void CommitGraph::setIsHead(bool isHead)
{
    if (m_isHead == isHead)
        return;
    m_isHead = isHead;
    update();
    emit isHeadChanged();
}

void CommitGraph::setOutlineColor(const QColor &color)
{
    if (m_outlineColor == color)
        return;
    m_outlineColor = color;
    update();
    emit outlineColorChanged();
}

void CommitGraph::updateImplicitWidth()
{
    setImplicitWidth(std::max(1, m_row.laneCount) * m_laneWidth);
}

void CommitGraph::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal h = height();
    const qreal mid = h / 2;
    const auto x = [this](int lane) { return (lane + 0.5) * m_laneWidth; };

    painter->setBrush(Qt::NoBrush);
    for (const GraphSegment &s : std::as_const(m_row.segments)) {
        QPen pen(laneColor(s.color), 2);
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const QPointF a(x(s.from), s.upper ? 0 : mid);
        const QPointF b(x(s.to), s.upper ? mid : h);
        if (s.from == s.to) {
            painter->drawLine(a, b);
        } else {
            const qreal ym = (a.y() + b.y()) / 2;
            QPainterPath path(a);
            path.cubicTo(a.x(), ym, b.x(), ym, b.x(), b.y());
            painter->drawPath(path);
        }
    }

    const QPointF center(x(m_row.column), mid);
    const qreal radius = m_isHead ? 5 : 4;
    if (m_isHead) {
        // A hollow ring marks the checked out commit.
        painter->setPen(QPen(laneColor(m_row.color), 2.5));
        painter->setBrush(m_outlineColor);
    } else {
        painter->setPen(QPen(m_outlineColor, 1.5));
        painter->setBrush(laneColor(m_row.color));
    }
    painter->drawEllipse(center, radius, radius);
}
