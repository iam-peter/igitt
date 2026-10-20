#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// A file's revisions along a time axis, for the time-lapse view: a dot per
// revision at its date (or evenly spaced), the current one highlighted.
// Clicking or dragging asks for the nearest revision.
class RevisionTimeline : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    // Commit times in seconds since the epoch, oldest first.
    Q_PROPERTY(QVariantList times READ times WRITE setTimes NOTIFY changed)
    // Who made each revision; dots of one author share a color.
    Q_PROPERTY(QStringList authors MEMBER m_authors NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    // Spread revisions evenly instead of by date, when many are close.
    Q_PROPERTY(bool evenSpacing MEMBER m_evenSpacing NOTIFY changed)
    Q_PROPERTY(int hoveredRevision READ hoveredRevision NOTIFY hoveredChanged)
    Q_PROPERTY(QColor lineColor MEMBER m_lineColor NOTIFY changed)
    Q_PROPERTY(QColor highlightColor MEMBER m_highlightColor NOTIFY changed)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY changed)

public:
    explicit RevisionTimeline(QQuickItem *parent = nullptr);

    QVariantList times() const;
    void setTimes(const QVariantList &times);
    int current() const { return m_current; }
    void setCurrent(int current);
    int hoveredRevision() const { return m_hovered; }

    // Where revision `index` is drawn, for tooltips.
    Q_INVOKABLE qreal xOf(int index) const;

    void paint(QPainter *painter) override;

signals:
    void changed();
    void hoveredChanged();
    void revisionRequested(int revision);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    int nearest(qreal x) const;
    void setHovered(int revision);

    QList<qint64> m_times;
    QStringList m_authors;
    int m_current = -1;
    int m_hovered = -1;
    bool m_evenSpacing = false;
    QColor m_lineColor = Qt::gray;
    QColor m_highlightColor = QColor(0x4e79a7);
    QColor m_textColor = Qt::gray;
};
