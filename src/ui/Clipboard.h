#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class Clipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE void setText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
};
