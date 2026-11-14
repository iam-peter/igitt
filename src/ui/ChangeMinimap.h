#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// The whole of a file beside its lines, for the time-lapse view: the rows
// a revision added, changed and removed as marks in green, orange and
// red, the rows in sight as a band. Where removed lines are hidden, a red
// tick marks where they were. Clicking or dragging asks for the row under
// the pointer to be centered.
class ChangeMinimap : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    // A LineChange for each row: Timelapse::changes
    Q_PROPERTY(QList<int> changes READ changes WRITE setChanges NOTIFY changesChanged)
    // Rows before which removed lines are hidden: Timelapse::hiddenRemovals
    Q_PROPERTY(QList<int> hiddenRemovals READ hiddenRemovals WRITE setHiddenRemovals NOTIFY changesChanged)
    // The rows in sight, fractional as the view scrolls
    Q_PROPERTY(qreal firstVisible MEMBER m_firstVisible NOTIFY viewChanged)
    Q_PROPERTY(qreal visibleCount MEMBER m_visibleCount NOTIFY viewChanged)
    Q_PROPERTY(QColor addedColor MEMBER m_addedColor NOTIFY viewChanged)
    Q_PROPERTY(QColor removedColor MEMBER m_removedColor NOTIFY viewChanged)
    Q_PROPERTY(QColor changedColor MEMBER m_changedColor NOTIFY viewChanged)
    Q_PROPERTY(QColor bandColor MEMBER m_bandColor NOTIFY viewChanged)

public:
    explicit ChangeMinimap(QQuickItem *parent = nullptr);

    QList<int> changes() const { return m_changes; }
    void setChanges(const QList<int> &changes);
    QList<int> hiddenRemovals() const { return m_hiddenRemovals; }
    void setHiddenRemovals(const QList<int> &rows);

    // Where row `row` is drawn
    Q_INVOKABLE qreal yOf(qreal row) const;

    void paint(QPainter *painter) override;

signals:
    void changesChanged();
    void viewChanged();
    void rowRequested(qreal row);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    qreal scale() const;

    QList<int> m_changes;
    QList<int> m_hiddenRemovals;
    qreal m_firstVisible = 0;
    qreal m_visibleCount = 0;
    QColor m_addedColor = QColor(0x2ca02c);
    QColor m_removedColor = QColor(0xd62728);
    QColor m_changedColor = QColor(0xff7f0e);
    QColor m_bandColor = Qt::gray;
};
