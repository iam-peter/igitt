#pragma once

#include "models/CommitModel.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Commit activity over time above the history, as in GitLens: a curve of
// commits per day, markers for commits with branch or tag labels, the
// rows on screen as a band, and the selected commit as a line. Clicking
// or dragging asks for the commit nearest to that point in time.
//
// The commits are a model's rows, or else times (seconds since the epoch,
// newest first, the first being HEAD), for commits that are not rows of a
// model, such as those composing commits shows.
class ActivityMinimap : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CommitModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QList<qreal> times READ times WRITE setTimes NOTIFY modelChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY viewChanged)
    Q_PROPERTY(int firstVisibleRow READ firstVisibleRow WRITE setFirstVisibleRow NOTIFY viewChanged)
    Q_PROPERTY(int lastVisibleRow READ lastVisibleRow WRITE setLastVisibleRow NOTIFY viewChanged)
    Q_PROPERTY(QColor lineColor MEMBER m_lineColor NOTIFY viewChanged)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY viewChanged)
    Q_PROPERTY(QColor bandColor MEMBER m_bandColor NOTIFY viewChanged)
    // The line at the chosen commit.
    Q_PROPERTY(QColor currentColor MEMBER m_currentColor NOTIFY viewChanged)

public:
    explicit ActivityMinimap(QQuickItem *parent = nullptr);

    CommitModel *model() const { return m_model; }
    void setModel(CommitModel *model);
    QList<qreal> times() const { return m_times; }
    void setTimes(const QList<qreal> &times);
    int currentRow() const { return m_currentRow; }
    void setCurrentRow(int row);
    int firstVisibleRow() const { return m_firstVisible; }
    void setFirstVisibleRow(int row);
    int lastVisibleRow() const { return m_lastVisible; }
    void setLastVisibleRow(int row);

    void paint(QPainter *painter) override;

signals:
    void modelChanged();
    void viewChanged();
    void rowRequested(int row);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void rebuild();
    // The commits shown, from the model or the times
    int rows() const;
    qint64 timeAt(int row) const;
    qreal xForTime(qint64 secs) const;
    int rowNearest(qreal x) const;
    QRectF plotArea() const;

    QPointer<CommitModel> m_model;
    QList<qreal> m_times;
    int m_currentRow = -1;
    int m_firstVisible = -1;
    int m_lastVisible = -1;
    QColor m_lineColor = QColor(0x4e79a7);
    QColor m_textColor = Qt::gray;
    QColor m_bandColor = QColor(0x4e79a7);
    QColor m_currentColor = QColor(0x2ecc40);
    qint64 m_oldest = 0;
    qint64 m_newest = 0;
};
