#include "Fonts.h"

#include <QDirIterator>
#include <QFontDatabase>
#include <QGuiApplication>

void Fonts::registerBundled()
{
    QDirIterator fonts(QStringLiteral(":/qt/qml/Igitt/fonts"), {QStringLiteral("*.ttf")});
    while (fonts.hasNext()) {
        if (QFontDatabase::addApplicationFont(fonts.next()) < 0)
            qWarning("Could not load the font %s", qPrintable(fonts.filePath()));
    }
}

void Fonts::setApplicationFont(const QFont &font)
{
    QGuiApplication::setFont(font);
}
