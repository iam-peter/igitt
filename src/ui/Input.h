#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// What QML's own click signals leave out: the keys held while clicking
class Input : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Qt.ControlModifier, Qt.ShiftModifier, ... held as of the last event
    Q_INVOKABLE int modifiers() const { return int(QGuiApplication::keyboardModifiers()); }
};
