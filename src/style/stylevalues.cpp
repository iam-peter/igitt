#include "stylevalues.h"

#include <QFontInfo>
#include <QGuiApplication>

namespace {

template<typename T>
void take(std::optional<T> &slot, const std::optional<T> &later)
{
    if (later)
        slot = later;
}

} // namespace

int StyleFont::defaultSize()
{
    return QFontInfo(QGuiApplication::font()).pixelSize();
}

void StyleFont::overlay(const StyleFont &later)
{
    take(m_color, later.m_color);
    take(m_size, later.m_size);
    take(m_weight, later.m_weight);
}

void StyleBorder::overlay(const StyleBorder &later)
{
    take(m_color, later.m_color);
    take(m_width, later.m_width);
}

void StyleRadius::overlay(const StyleRadius &later)
{
    // A later value sets every corner; a later corner only its own
    if (later.m_value) {
        m_value = later.m_value;
        m_topLeft.reset();
        m_topRight.reset();
        m_bottomLeft.reset();
        m_bottomRight.reset();
    }
    take(m_topLeft, later.m_topLeft);
    take(m_topRight, later.m_topRight);
    take(m_bottomLeft, later.m_bottomLeft);
    take(m_bottomRight, later.m_bottomRight);
}

std::optional<int> StylePadding::side(const std::optional<int> &own, const std::optional<int> &axis) const
{
    if (own)
        return own;
    if (axis)
        return axis;
    return m_value;
}

void StylePadding::overlay(const StylePadding &later)
{
    // Both as four sides, then the later sides win
    StylePadding sides;
    sides.m_left = side(m_left, m_horizontal);
    sides.m_right = side(m_right, m_horizontal);
    sides.m_top = side(m_top, m_vertical);
    sides.m_bottom = side(m_bottom, m_vertical);
    take(sides.m_left, later.side(later.m_left, later.m_horizontal));
    take(sides.m_right, later.side(later.m_right, later.m_horizontal));
    take(sides.m_top, later.side(later.m_top, later.m_vertical));
    take(sides.m_bottom, later.side(later.m_bottom, later.m_vertical));
    // value stays readable when one is set for all sides
    sides.m_value = later.m_value ? later.m_value : m_value;
    *this = sides;
}

void StyleValues::overlay(const StyleValues &later)
{
    take(background, later.background);
    text.overlay(later.text);
    icon.overlay(later.icon);
    take(placeholder, later.placeholder);
    take(selection, later.selection);
    take(selectedText, later.selectedText);
    take(accent, later.accent);
    take(dim, later.dim);
    border.overlay(later.border);
    radius.overlay(later.radius);
    padding.overlay(later.padding);
    take(spacing, later.spacing);
    take(width, later.width);
    take(height, later.height);
    take(opacity, later.opacity);
}

void StyleValues::inheritFrom(const StyleValues &parent)
{
    StyleFont inheritedText = parent.text;
    inheritedText.overlay(text);
    text = inheritedText;

    StyleFont inheritedIcon = parent.icon;
    inheritedIcon.overlay(icon);
    icon = inheritedIcon;
}
