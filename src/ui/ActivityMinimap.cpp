#include "ActivityMinimap.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

constexpr qreal MarkerRow = 10; // height of the marker strip at the bottom
constexpr qreal Margin = 4;

} // namespace

ActivityMinimap::ActivityMinimap(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &ActivityMinimap::viewChanged, this, [this] { update(); });
}

void ActivityMinimap::setModel(CommitModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
        connect(m_model, &QAbstractItemModel::modelReset, this, &ActivityMinimap::rebuild);
    rebuild();
    emit modelChanged();
}

void ActivityMinimap::setCurrentRow(int row)
{
    if (m_currentRow != row) {
        m_currentRow = row;
        emit viewChanged();
    }
}

void ActivityMinimap::setFirstVisibleRow(int row)
{
    if (m_firstVisible != row) {
        m_firstVisible = row;
        emit viewChanged();
    }
}

void ActivityMinimap::setLastVisibleRow(int row)
{
    if (m_lastVisible != row) {
        m_lastVisible = row;
        emit viewChanged();
    }
}

void ActivityMinimap::rebuild()
{
    m_oldest = m_newest = 0;
    if (m_model && m_model->count() > 0) {
        m_oldest = m_newest = m_model->timestampAt(0);
        for (int row = 1; row < m_model->count(); ++row) {
            const qint64 t = m_model->timestampAt(row);
            m_oldest = std::min(m_oldest, t);
            m_newest = std::max(m_newest, t);
        }
        // At least a day, or a burst of commits collapses into one point.
        m_oldest = std::min(m_oldest, m_newest - 24 * 3600);
    }
    update();
}

QRectF ActivityMinimap::plotArea() const
{
    return QRectF(Margin, Margin, width() - 2 * Margin, height() - 2 * Margin - MarkerRow);
}

qreal ActivityMinimap::xForTime(qint64 secs) const
{
    const QRectF area = plotArea();
    if (m_newest <= m_oldest)
        return area.right();
    return area.left() + area.width() * qreal(secs - m_oldest) / qreal(m_newest - m_oldest);
}

int ActivityMinimap::rowNearest(qreal x) const
{
    if (!m_model || m_model->count() == 0)
        return -1;
    const QRectF area = plotArea();
    const qreal fraction = std::clamp((x - area.left()) / area.width(), 0.0, 1.0);
    const qint64 target = m_oldest + qint64(fraction * qreal(m_newest - m_oldest));
    int best = 0;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (int row = 0; row < m_model->count(); ++row) {
        const qint64 distance = std::abs(m_model->timestampAt(row) - target);
        if (distance < bestDistance) {
            best = row;
            bestDistance = distance;
        }
    }
    return best;
}

void ActivityMinimap::mousePressEvent(QMouseEvent *event)
{
    if (const int row = rowNearest(event->position().x()); row >= 0)
        emit rowRequested(row);
    event->accept();
}

void ActivityMinimap::mouseMoveEvent(QMouseEvent *event)
{
    mousePressEvent(event);
}

void ActivityMinimap::paint(QPainter *painter)
{
    if (!m_model || m_model->count() == 0 || width() < 20)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF area = plotArea();
    const int columns = std::max(2, int(area.width() / 3));

    // Commits per time slot, lightly smoothed so the curve reads as activity
    // rather than noise.
    QList<qreal> counts(columns, 0);
    for (int row = 0; row < m_model->count(); ++row) {
        const qreal x = xForTime(m_model->timestampAt(row));
        const int column = std::clamp(int((x - area.left()) / area.width() * (columns - 1)), 0, columns - 1);
        counts[column] += 1;
    }
    QList<qreal> smooth(columns, 0);
    for (int i = 0; i < columns; ++i) {
        qreal sum = 0;
        qreal weight = 0;
        for (int d = -2; d <= 2; ++d) {
            if (i + d >= 0 && i + d < columns) {
                const qreal w = 3 - std::abs(d);
                sum += counts[i + d] * w;
                weight += w;
            }
        }
        smooth[i] = sum / weight;
    }
    const qreal peak = std::max(1.0, *std::max_element(smooth.cbegin(), smooth.cend()));

    // Rows on screen.
    if (m_firstVisible >= 0 && m_lastVisible >= m_firstVisible && m_lastVisible < m_model->count()) {
        qreal left = area.right();
        qreal right = area.left();
        for (int row = m_firstVisible; row <= m_lastVisible; ++row) {
            const qreal x = xForTime(m_model->timestampAt(row));
            left = std::min(left, x);
            right = std::max(right, x);
        }
        QColor band = m_bandColor;
        band.setAlphaF(0.18);
        painter->fillRect(QRectF(left - 2, 0, right - left + 4, height()), band);
    }

    // The curve, filled below.
    QPainterPath curve;
    const auto point = [&](int i) {
        return QPointF(area.left() + area.width() * i / (columns - 1),
                       area.bottom() - area.height() * std::sqrt(smooth[i] / peak));
    };
    curve.moveTo(point(0));
    for (int i = 1; i < columns; ++i) {
        const QPointF a = point(i - 1);
        const QPointF b = point(i);
        curve.quadTo(a, (a + b) / 2);
    }
    curve.lineTo(point(columns - 1));
    QPainterPath fill = curve;
    fill.lineTo(area.bottomRight());
    fill.lineTo(area.bottomLeft());
    fill.closeSubpath();
    QColor fillColor = m_lineColor;
    fillColor.setAlphaF(0.15);
    painter->fillPath(fill, fillColor);
    painter->setPen(QPen(m_lineColor, 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(curve);

    // Labelled commits along the bottom, HEAD as a hollow circle.
    const qreal markerY = height() - Margin - MarkerRow / 2;
    painter->setPen(Qt::NoPen);
    for (int row = 0; row < m_model->count(); ++row) {
        if (!m_model->hasRefsAt(row))
            continue;
        painter->setBrush(QColor::fromString(m_model->laneColorAt(row)));
        const qreal x = xForTime(m_model->timestampAt(row));
        painter->drawRect(QRectF(x - 2.5, markerY - 2.5, 5, 5));
    }
    if (const int head = m_model->headRow(); head >= 0) {
        painter->setPen(QPen(QColor::fromString(m_model->laneColorAt(head)), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(xForTime(m_model->timestampAt(head)), markerY), 4, 4);
    }

    // The selected commit.
    if (m_currentRow >= 0 && m_currentRow < m_model->count()) {
        const qreal x = xForTime(m_model->timestampAt(m_currentRow));
        painter->setPen(QPen(QColor(0x2ecc40), 2));
        painter->drawLine(QPointF(x, 0), QPointF(x, height()));
    }

    // The time span, in the corners.
    QFont font = painter->font();
    font.setPixelSize(9);
    painter->setFont(font);
    painter->setPen(m_textColor);
    const QString format = QStringLiteral("yyyy-MM-dd");
    painter->drawText(QRectF(area.left(), 0, area.width(), 12), Qt::AlignLeft | Qt::AlignTop,
                      QDateTime::fromSecsSinceEpoch(m_oldest).toString(format));
    painter->drawText(QRectF(area.left(), 0, area.width(), 12), Qt::AlignRight | Qt::AlignTop,
                      QDateTime::fromSecsSinceEpoch(m_newest).toString(format));
}
