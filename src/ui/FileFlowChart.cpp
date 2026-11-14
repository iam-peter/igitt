#include "FileFlowChart.h"

#include "core/FileHistory.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

constexpr qreal Margin = 28;     // left and right of the first and last node
constexpr qreal TopRoom = 18;        // room for what each revision added and removed
constexpr qreal BottomRoom = 20;     // room for the labels
constexpr qreal BarWidth = 8;
constexpr qreal MinBand = 2;     // a change of a line or two still shows

} // namespace

FileFlowChart::FileFlowChart(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &FileFlowChart::changed, this, [this] { update(); });
    connect(this, &FileFlowChart::stepsChanged, this, [this] { update(); });
    connect(this, &FileFlowChart::hoveredChanged, this, [this] { update(); });
    connect(this, &QQuickItem::widthChanged, this, &FileFlowChart::fittingSpacingChanged);
}

void FileFlowChart::setSteps(const QVariantList &steps)
{
    m_stepList = steps;
    m_steps.clear();
    m_maxLines = 1;
    for (const QVariant &value : steps) {
        const QVariantMap map = value.toMap();
        Step step;
        step.lines = map.value(QStringLiteral("lines")).toInt();
        step.added = map.value(QStringLiteral("added")).toInt();
        step.changed = map.value(QStringLiteral("changed")).toInt();
        step.removed = map.value(QStringLiteral("removed")).toInt();
        for (const QVariant &runValue : map.value(QStringLiteral("runs")).toList()) {
            const QVariantMap r = runValue.toMap();
            step.runs.append({r.value(QStringLiteral("kind")).toInt(), r.value(QStringLiteral("oldStart")).toInt(),
                              r.value(QStringLiteral("oldCount")).toInt(), r.value(QStringLiteral("newStart")).toInt(),
                              r.value(QStringLiteral("newCount")).toInt()});
        }
        m_maxLines = std::max(m_maxLines, step.lines);
        m_steps.append(step);
    }
    emit stepsChanged();
    emit contentWidthChanged();
    emit fittingSpacingChanged();
}

void FileFlowChart::setCurrent(int current)
{
    if (m_current != current) {
        m_current = current;
        emit changed();
    }
}

void FileFlowChart::setSpacing(qreal spacing)
{
    spacing = std::clamp(spacing, 24.0, 240.0);
    if (!qFuzzyCompare(m_spacing, spacing)) {
        m_spacing = spacing;
        emit changed();
        emit contentWidthChanged();
    }
}

void FileFlowChart::setScroll(qreal scroll)
{
    if (!qFuzzyCompare(m_scroll + 1, scroll + 1)) {
        m_scroll = scroll;
        emit changed();
    }
}

qreal FileFlowChart::contentWidth() const
{
    return 2 * Margin + std::max(0.0, qreal(m_steps.size() - 1)) * m_spacing + BarWidth;
}

qreal FileFlowChart::fittingSpacing() const
{
    return m_steps.size() < 2 ? 240.0 : (width() - 2 * Margin - BarWidth) / qreal(m_steps.size() - 1);
}

qreal FileFlowChart::xOf(int index) const
{
    return Margin + BarWidth / 2 + index * m_spacing - m_scroll;
}

int FileFlowChart::nearest(qreal x) const
{
    if (m_steps.isEmpty())
        return -1;
    const int index = int(std::lround((x + m_scroll - Margin - BarWidth / 2) / m_spacing));
    if (index < 0 || index >= m_steps.size() || std::abs(xOf(index) - x) > m_spacing / 2)
        return -1;
    return index;
}

void FileFlowChart::setHovered(int revision)
{
    if (m_hovered != revision) {
        m_hovered = revision;
        emit hoveredChanged();
    }
}

void FileFlowChart::mousePressEvent(QMouseEvent *event)
{
    if (const int revision = nearest(event->position().x()); revision >= 0)
        emit revisionRequested(revision);
    event->accept();
}

void FileFlowChart::hoverMoveEvent(QHoverEvent *event)
{
    setHovered(nearest(event->position().x()));
}

void FileFlowChart::hoverLeaveEvent(QHoverEvent *)
{
    setHovered(-1);
}

void FileFlowChart::paint(QPainter *painter)
{
    if (m_steps.isEmpty() || width() < 20)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal plot = std::max(1.0, height() - TopRoom - BottomRoom);
    const qreal scale = plot / m_maxLines;
    const auto yOf = [&](int line) { return TopRoom + line * scale; };

    // Only the nodes in sight, and one more on either side for the flows
    // that reach into it
    const int first = std::max(0, int(std::floor((m_scroll - Margin) / m_spacing)) - 1);
    const int last = std::min(int(m_steps.size()) - 1, int(std::ceil((m_scroll + width()) / m_spacing)) + 1);

    QColor kept = m_keptColor;
    kept.setAlphaF(0.18);
    QColor added = m_addedColor;
    added.setAlphaF(0.8);
    QColor removed = m_removedColor;
    removed.setAlphaF(0.8);
    QColor changed = m_changedColor;
    changed.setAlphaF(0.8);

    // A band from a range on the left to one on the right, curved like a
    // Sankey diagram's; a range of no lines is a point
    const auto band = [&](qreal x0, qreal top0, qreal bottom0, qreal x1, qreal top1, qreal bottom1) {
        const qreal middle = (x0 + x1) / 2;
        QPainterPath path(QPointF(x0, top0));
        path.cubicTo(middle, top0, middle, top1, x1, top1);
        path.lineTo(x1, bottom1);
        path.cubicTo(middle, bottom1, middle, bottom0, x0, bottom0);
        path.closeSubpath();
        return path;
    };
    // A change too small to see is drawn a little larger, around its place
    const auto widen = [](qreal &top, qreal &bottom) {
        if (bottom - top < MinBand) {
            const qreal middle = (top + bottom) / 2;
            top = middle - MinBand / 2;
            bottom = middle + MinBand / 2;
        }
    };

    painter->setPen(Qt::NoPen);
    for (int i = std::max(1, first); i <= last; ++i) {
        const qreal x0 = xOf(i - 1) + BarWidth / 2;
        const qreal x1 = xOf(i) - BarWidth / 2;
        for (const Run &run : m_steps[i].runs) {
            qreal top0 = yOf(run.oldStart);
            qreal bottom0 = yOf(run.oldStart + run.oldCount);
            qreal top1 = yOf(run.newStart);
            qreal bottom1 = yOf(run.newStart + run.newCount);
            switch (LineChange(run.kind)) {
            case LineChange::Kept:
                painter->setBrush(kept);
                break;
            case LineChange::Removed:
                widen(top0, bottom0);
                painter->setBrush(removed);
                break;
            case LineChange::Added:
                widen(top1, bottom1);
                painter->setBrush(added);
                break;
            case LineChange::Changed:
                widen(top0, bottom0);
                widen(top1, bottom1);
                painter->setBrush(changed);
                break;
            }
            painter->drawPath(band(x0, top0, bottom0, x1, top1, bottom1));
        }
    }

    // The first revision brings the whole file in, from nothing
    if (first == 0 && m_steps.first().lines > 0) {
        const qreal x1 = xOf(0) - BarWidth / 2;
        painter->setBrush(added);
        painter->drawPath(band(x1 - std::min(m_spacing / 2, Margin + 4), TopRoom, TopRoom, x1, TopRoom, yOf(m_steps.first().lines)));
    }

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);
    for (int i = first; i <= last; ++i) {
        const Step &step = m_steps[i];
        const qreal x = xOf(i);
        const QRectF bar(x - BarWidth / 2, TopRoom, BarWidth, std::max(1.0, step.lines * scale));
        painter->setPen(Qt::NoPen);
        painter->setBrush(i == m_current ? m_currentColor : m_nodeColor);
        painter->drawRoundedRect(bar, 2, 2);
        if (i == m_hovered) {
            painter->setPen(QPen(m_currentColor, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(bar.adjusted(-2, -2, 2, 2), 3, 3);
        }

        // What it changed, added and removed, above, as the commit row
        // counts files; its label beneath
        if (m_spacing >= 40) {
            const QRectF above(x - m_spacing / 2, 0, m_spacing, TopRoom - 2);
            const QList<std::pair<QString, QColor>> counts = {
                {step.changed > 0 ? QStringLiteral("✎%1").arg(step.changed) : QString(), m_changedColor},
                {step.added > 0 ? QStringLiteral("+%1").arg(step.added) : QString(), m_addedColor},
                {step.removed > 0 ? QStringLiteral("−%1").arg(step.removed) : QString(), m_removedColor},
            };
            const QFontMetricsF metrics(font);
            constexpr qreal Gap = 4;
            qreal total = 0;
            for (const auto &[text, color] : counts)
                total += text.isEmpty() ? 0 : metrics.horizontalAdvance(text) + (total > 0 ? Gap : 0);
            qreal left = x - total / 2;
            for (const auto &[text, color] : counts) {
                if (text.isEmpty())
                    continue;
                painter->setPen(color);
                painter->drawText(QRectF(left, above.top(), m_spacing, above.height()), Qt::AlignLeft | Qt::AlignBottom, text);
                left += metrics.horizontalAdvance(text) + Gap;
            }
            if (i < m_labels.size()) {
                painter->setPen(i == m_current ? m_currentColor : m_textColor);
                painter->drawText(QRectF(x - m_spacing / 2, height() - BottomRoom + 2, m_spacing, BottomRoom - 2),
                                  Qt::AlignHCenter | Qt::AlignTop,
                                  metrics.elidedText(m_labels[i], Qt::ElideRight, m_spacing - 4));
            }
        }
    }
}
