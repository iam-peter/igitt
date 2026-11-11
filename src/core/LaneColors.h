#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// The colors of the graph's lanes, from the theme: QML sets them
// (TokenInterface.semantics.graph_lanes), the models and painted items
// read them through laneColorName() and follow changed().
class LaneColors : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QStringList colors READ colors WRITE setColors NOTIFY changed FINAL)

public:
    static LaneColors *instance();
    static LaneColors *create(QQmlEngine *, QJSEngine *);

    QStringList colors() const { return m_colors; }
    void setColors(const QStringList &colors);

    // The color of a lane, by GraphRow::color and GraphSegment::color.
    QString colorName(int color) const;

signals:
    void changed();

private:
    // Only instance() makes one, which QML gets from create().
    explicit LaneColors(QObject *parent);

    QStringList m_colors;
};
