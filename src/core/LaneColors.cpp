#include "LaneColors.h"

#include <QCoreApplication>
#include <QJSEngine>

LaneColors::LaneColors(QObject *parent)
    : QObject(parent)
{}

LaneColors *LaneColors::instance()
{
    static LaneColors *colors = new LaneColors(QCoreApplication::instance());
    return colors;
}

LaneColors *LaneColors::create(QQmlEngine *, QJSEngine *)
{
    LaneColors *colors = instance();
    QJSEngine::setObjectOwnership(colors, QJSEngine::CppOwnership);
    return colors;
}

void LaneColors::setColors(const QStringList &colors)
{
    if (m_colors == colors)
        return;
    m_colors = colors;
    emit changed();
}

QString LaneColors::colorName(int color) const
{
    // Until a theme sets them, and for tests without one.
    static const QStringList fallback = {
        QStringLiteral("#4e79a7"), QStringLiteral("#f28e2b"), QStringLiteral("#59a14f"),
        QStringLiteral("#e15759"), QStringLiteral("#b07aa1"), QStringLiteral("#76b7b2"),
        QStringLiteral("#edc948"), QStringLiteral("#ff9da7"), QStringLiteral("#9c755f"),
    };
    const QStringList &colors = m_colors.isEmpty() ? fallback : m_colors;
    return colors.at(color % colors.size());
}
