#include "ChangeMinimap.h"

#include "core/FileHistory.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace {

constexpr qreal MinMark = 2; // a single line still shows

} // namespace

ChangeMinimap::ChangeMinimap(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &ChangeMinimap::changesChanged, this, [this] { update(); });
    connect(this, &ChangeMinimap::viewChanged, this, [this] { update(); });
}

void ChangeMinimap::setChanges(const QList<int> &changes)
{
    m_changes = changes;
    emit changesChanged();
}

void ChangeMinimap::setHiddenRemovals(const QList<int> &rows)
{
    m_hiddenRemovals = rows;
    emit changesChanged();
}

qreal ChangeMinimap::scale() const
{
    // A short file is drawn at the size of its rows in the view, not
    // stretched over the whole height
    const qreal rows = std::max<qreal>(1, std::max<qreal>(m_changes.size(), m_visibleCount));
    return height() / rows;
}

qreal ChangeMinimap::yOf(qreal row) const
{
    return row * scale();
}

void ChangeMinimap::mousePressEvent(QMouseEvent *event)
{
    emit rowRequested(event->position().y() / scale());
    event->accept();
}

void ChangeMinimap::mouseMoveEvent(QMouseEvent *event)
{
    emit rowRequested(std::clamp(event->position().y(), 0.0, height()) / scale());
    event->accept();
}

void ChangeMinimap::paint(QPainter *painter)
{
    if (m_changes.isEmpty() || height() < 4)
        return;
    const qreal s = scale();
    painter->setPen(Qt::NoPen);

    QColor band = m_bandColor;
    band.setAlphaF(0.22);
    painter->setBrush(band);
    painter->drawRect(QRectF(0, yOf(m_firstVisible), width(), std::max(MinMark, m_visibleCount * s)));

    // Each run of rows alike as one mark
    const auto colorOf = [&](int change) {
        switch (LineChange(change)) {
        case LineChange::Added: return m_addedColor;
        case LineChange::Removed: return m_removedColor;
        case LineChange::Changed: return m_changedColor;
        case LineChange::Kept: break;
        }
        return QColor();
    };
    const qreal inset = 2;
    for (int row = 0; row < m_changes.size();) {
        int end = row + 1;
        while (end < m_changes.size() && m_changes[end] == m_changes[row])
            ++end;
        if (const QColor color = colorOf(m_changes[row]); color.isValid()) {
            const qreal top = row * s;
            const qreal length = std::max(MinMark, (end - row) * s);
            painter->setBrush(color);
            painter->drawRect(QRectF(inset, top, width() - 2 * inset, length));
        }
        row = end;
    }
    painter->setBrush(m_removedColor);
    for (const int row : std::as_const(m_hiddenRemovals))
        painter->drawRect(QRectF(0, row * s - MinMark / 2, width(), MinMark));
}
