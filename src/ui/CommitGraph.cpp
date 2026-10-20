#include "CommitGraph.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

namespace {

// Room on the left for the ring around HEAD, which is wider than a lane.
constexpr qreal LeftMargin = 4;

QColor laneColor(int index)
{
    return QColor::fromString(laneColorName(index));
}

} // namespace

CommitGraph::CommitGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &CommitGraph::changed, this, [this] { update(); });
}

template <typename T>
void CommitGraph::set(T &member, const T &value)
{
    if (member == value)
        return;
    member = value;
    emit changed();
}

void CommitGraph::setRow(const QVariant &row)
{
    m_row = row.value<GraphRow>();
    updateImplicitWidth();
    emit changed();
}

void CommitGraph::setLaneWidth(qreal width)
{
    set(m_laneWidth, width);
    updateImplicitWidth();
}

void CommitGraph::setNodeKind(NodeKind kind) { set(m_nodeKind, kind); }
void CommitGraph::setInitials(const QString &initials) { set(m_initials, initials); }
void CommitGraph::setShowInitials(bool show) { set(m_showInitials, show); }
void CommitGraph::setConnectLeft(bool connect) { set(m_connectLeft, connect); }
void CommitGraph::setOutlineColor(const QColor &color) { set(m_outlineColor, color); }

void CommitGraph::updateImplicitWidth()
{
    setImplicitWidth(LeftMargin + std::max(1, m_row.laneCount) * m_laneWidth);
}

void CommitGraph::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    const qreal h = height();
    const qreal mid = h / 2;
    const auto x = [this](int lane) { return LeftMargin + (lane + 0.5) * m_laneWidth; };
    const QColor color = laneColor(m_row.color);
    const QPointF center(x(m_row.column), mid);

    if (m_connectLeft) {
        QColor link = color;
        link.setAlphaF(0.6);
        painter->setPen(QPen(link, 1.5));
        painter->drawLine(QPointF(0, mid), center);
    }

    painter->setBrush(Qt::NoBrush);
    for (const GraphSegment &s : std::as_const(m_row.segments)) {
        QPen pen(laneColor(s.color), 2.5);
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const QPointF a(x(s.from), s.upper ? 0 : mid);
        const QPointF b(x(s.to), s.upper ? mid : h);
        if (s.from == s.to) {
            painter->drawLine(a, b);
            continue;
        }
        // Run straight, then turn into the other lane with a rounded bend.
        const qreal ym = (a.y() + b.y()) / 2;
        QPainterPath path(a);
        path.cubicTo(a.x(), ym, b.x(), ym, b.x(), b.y());
        painter->drawPath(path);
    }

    const bool avatar = m_showInitials && (m_nodeKind == Commit || m_nodeKind == Head);
    const qreal radius = m_nodeKind == Merge ? 4.5
                       : m_nodeKind == WorkingTree ? 7
                       : avatar ? std::min(10.0, mid - 3) : 5;

    if (m_nodeKind == Head) {
        // A ring around HEAD, set off from the node by the background.
        painter->setPen(QPen(color, 2));
        painter->setBrush(m_outlineColor);
        painter->drawEllipse(center, radius + 3, radius + 3);
    }

    if (m_nodeKind == WorkingTree) {
        QPen pen(color, 2, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(m_outlineColor);
    } else {
        painter->setPen(QPen(m_outlineColor, 1.5));
        painter->setBrush(color);
    }
    painter->drawEllipse(center, radius, radius);

    if (avatar && !m_initials.isEmpty()) {
        QFont font = painter->font();
        font.setPixelSize(int(radius * 0.95));
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(color.lightnessF() > 0.62 ? QColor(0x202020) : QColor(Qt::white));
        painter->drawText(QRectF(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius),
                          Qt::AlignCenter, m_initials);
    }
}
