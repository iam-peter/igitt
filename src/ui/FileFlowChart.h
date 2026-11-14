#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// A file's history as a Sankey diagram, for the time-lapse view: each
// revision is a node, a bar as long as the file is then, line 1 at the
// top. Between two revisions, the lines kept flow in gray from their old
// place to their new one and the lines changed in orange; the lines
// removed narrow in red to where they went, the lines added widen in
// green from where they came. Clicking a node asks for that revision.
//
// On the node of the current revision, a marker shows the lines in sight
// in its code; clicking or dragging along that node asks for a line.
//
// Only what is in sight is drawn: the nodes are spaced evenly, and the
// view shows them from `scroll` on.
class FileFlowChart : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    // Timelapse::flow
    Q_PROPERTY(QVariantList steps READ steps WRITE setSteps NOTIFY stepsChanged)
    // Under each node, such as its short id
    Q_PROPERTY(QStringList labels MEMBER m_labels NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    // From one node to the next, and how far the view is scrolled
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY changed)
    Q_PROPERTY(qreal scroll READ scroll WRITE setScroll NOTIFY changed)
    // All the nodes side by side, for a scroll bar
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentWidthChanged)
    // The spacing at which the nodes fill the width
    Q_PROPERTY(qreal fittingSpacing READ fittingSpacing NOTIFY fittingSpacingChanged)
    // The lines of the current revision in sight, 0-based and fractional;
    // the marker is hidden while viewEnd is not past viewStart
    Q_PROPERTY(qreal viewStart MEMBER m_viewStart NOTIFY changed)
    Q_PROPERTY(qreal viewEnd MEMBER m_viewEnd NOTIFY changed)
    Q_PROPERTY(int hoveredRevision READ hoveredRevision NOTIFY hoveredChanged)
    Q_PROPERTY(QColor keptColor MEMBER m_keptColor NOTIFY changed)
    Q_PROPERTY(QColor addedColor MEMBER m_addedColor NOTIFY changed)
    Q_PROPERTY(QColor removedColor MEMBER m_removedColor NOTIFY changed)
    Q_PROPERTY(QColor changedColor MEMBER m_changedColor NOTIFY changed)
    Q_PROPERTY(QColor nodeColor MEMBER m_nodeColor NOTIFY changed)
    Q_PROPERTY(QColor currentColor MEMBER m_currentColor NOTIFY changed)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY changed)

public:
    explicit FileFlowChart(QQuickItem *parent = nullptr);

    QVariantList steps() const { return m_stepList; }
    void setSteps(const QVariantList &steps);
    int current() const { return m_current; }
    void setCurrent(int current);
    qreal spacing() const { return m_spacing; }
    void setSpacing(qreal spacing);
    qreal scroll() const { return m_scroll; }
    void setScroll(qreal scroll);
    qreal contentWidth() const;
    qreal fittingSpacing() const;
    int hoveredRevision() const { return m_hovered; }

    // Where node `index` is drawn, for tooltips and to scroll it into view.
    Q_INVOKABLE qreal xOf(int index) const;

    void paint(QPainter *painter) override;

signals:
    void stepsChanged();
    void changed();
    void contentWidthChanged();
    void fittingSpacingChanged();
    void hoveredChanged();
    void revisionRequested(int revision);
    // A line of the current revision, by the node's marker
    void lineRequested(qreal line);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    struct Run
    {
        int kind = 0; // LineChange
        int oldStart = 0;
        int oldCount = 0;
        int newStart = 0;
        int newCount = 0;
    };
    struct Step
    {
        int lines = 0;
        int added = 0;
        int changed = 0;
        int removed = 0;
        QList<Run> runs;
    };

    int nearest(qreal x) const;
    qreal lineScale() const;
    void requestLine(qreal y);
    void setHovered(int revision);

    QVariantList m_stepList;
    QList<Step> m_steps;
    int m_maxLines = 1;
    QStringList m_labels;
    int m_current = -1;
    int m_hovered = -1;
    qreal m_spacing = 72;
    qreal m_scroll = 0;
    qreal m_viewStart = 0;
    qreal m_viewEnd = 0;
    bool m_draggingView = false;
    QColor m_keptColor = Qt::gray;
    QColor m_addedColor = QColor(0x2ca02c);
    QColor m_removedColor = QColor(0xd62728);
    QColor m_changedColor = QColor(0xff7f0e);
    QColor m_nodeColor = Qt::darkGray;
    QColor m_currentColor = QColor(0x4e79a7);
    QColor m_textColor = Qt::gray;
};
