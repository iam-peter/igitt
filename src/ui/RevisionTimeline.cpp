#include "RevisionTimeline.h"

#include "core/GraphLayout.h"

#include <QDateTime>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr qreal Margin = 14;   // keeps the outer dots and their rings inside
constexpr qreal AxisY = 0.55;  // of the height
constexpr qreal Radius = 4.5;

} // namespace

RevisionTimeline::RevisionTimeline(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    connect(this, &RevisionTimeline::changed, this, [this] { update(); });
    connect(this, &RevisionTimeline::hoveredChanged, this, [this] { update(); });
}

QVariantList RevisionTimeline::times() const
{
    QVariantList list;
    for (qint64 t : m_times)
        list.append(t);
    return list;
}

void RevisionTimeline::setTimes(const QVariantList &times)
{
    m_times.clear();
    for (const QVariant &t : times)
        m_times.append(t.toLongLong());
    emit changed();
}

void RevisionTimeline::setCurrent(int current)
{
    if (m_current != current) {
        m_current = current;
        emit changed();
    }
}

qreal RevisionTimeline::xOf(int index) const
{
    const qreal left = Margin;
    const qreal span = width() - 2 * Margin;
    if (index < 0 || index >= m_times.size())
        return left;
    if (m_times.size() == 1)
        return left + span;
    if (m_evenSpacing)
        return left + span * index / (m_times.size() - 1);
    const qint64 oldest = m_times.first();
    const qint64 newest = m_times.last();
    if (newest <= oldest)
        return left + span * index / (m_times.size() - 1);
    return left + span * qreal(m_times[index] - oldest) / qreal(newest - oldest);
}

int RevisionTimeline::nearest(qreal x) const
{
    int best = -1;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_times.size(); ++i) {
        const qreal distance = std::abs(xOf(i) - x);
        // Among dots at the same place, the newer wins: it was drawn on top.
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void RevisionTimeline::setHovered(int revision)
{
    if (m_hovered != revision) {
        m_hovered = revision;
        emit hoveredChanged();
    }
}

void RevisionTimeline::mousePressEvent(QMouseEvent *event)
{
    if (const int revision = nearest(event->position().x()); revision >= 0)
        emit revisionRequested(revision);
    event->accept();
}

void RevisionTimeline::mouseMoveEvent(QMouseEvent *event)
{
    mousePressEvent(event);
    setHovered(nearest(event->position().x()));
}

void RevisionTimeline::hoverMoveEvent(QHoverEvent *event)
{
    setHovered(nearest(event->position().x()));
}

void RevisionTimeline::hoverLeaveEvent(QHoverEvent *)
{
    setHovered(-1);
}

void RevisionTimeline::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal axis = height() * AxisY;

    painter->setPen(QPen(m_lineColor, 1.5));
    painter->drawLine(QPointF(Margin, axis), QPointF(width() - Margin, axis));
    if (m_times.isEmpty())
        return;

    // The part of history up to the revision on display.
    if (m_current >= 0 && m_current < m_times.size()) {
        painter->setPen(QPen(m_highlightColor, 3));
        painter->drawLine(QPointF(Margin, axis), QPointF(xOf(m_current), axis));
    }

    QHash<QString, int> authorColors;
    for (int i = 0; i < m_times.size(); ++i) {
        const QString author = m_authors.value(i);
        if (!authorColors.contains(author))
            authorColors.insert(author, int(authorColors.size()));
        const QPointF center(xOf(i), axis);
        const bool emphasized = i == m_current || i == m_hovered;
        painter->setPen(QPen(Qt::white, 1));
        painter->setBrush(QColor::fromString(laneColorName(authorColors.value(author))));
        painter->drawEllipse(center, emphasized ? Radius + 1 : Radius, emphasized ? Radius + 1 : Radius);
    }

    if (m_current >= 0 && m_current < m_times.size()) {
        const QPointF center(xOf(m_current), axis);
        painter->setPen(QPen(m_highlightColor, 2.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(center, Radius + 5, Radius + 5);
    }

    // Dates of the first and last revision, and the one on display.
    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);
    const QString format = QStringLiteral("yyyy-MM-dd");
    const qreal textTop = axis + Radius + 6;
    const qreal textHeight = height() - textTop;
    painter->setPen(m_textColor);
    painter->drawText(QRectF(Margin - 10, textTop, 120, textHeight), Qt::AlignLeft | Qt::AlignTop,
                      QDateTime::fromSecsSinceEpoch(m_times.first()).toString(format));
    painter->drawText(QRectF(width() - Margin - 110, textTop, 120, textHeight), Qt::AlignRight | Qt::AlignTop,
                      QDateTime::fromSecsSinceEpoch(m_times.last()).toString(format));
    if (m_current >= 0 && m_current < m_times.size()) {
        painter->setPen(m_highlightColor);
        painter->drawText(QRectF(xOf(m_current) - 60, 0, 120, axis - Radius - 6), Qt::AlignHCenter | Qt::AlignBottom,
                          QDateTime::fromSecsSinceEpoch(m_times[m_current]).toString(format));
    }
}
