#pragma once

#include <QFont>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// The fonts igitt ships with: Inter for the interface and JetBrains Mono
// for code, both under the SIL Open Font License.
class Fonts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Call once before any UI is created.
    static void registerBundled();

    // Makes `font` the application default, which plain Text items and
    // painted items use; controls follow their window's font.
    Q_INVOKABLE void setApplicationFont(const QFont &font);
};
