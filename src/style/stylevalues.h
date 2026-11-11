#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QtQml/qqml.h>

#include <optional>

// The values a Style can set and a StyleCascade resolves. Every value is
// optional: a style sets only what it decides, later layers override
// earlier ones, and what no layer sets reads as the type's default.
//
// Grouped values (font, border, radius, padding) are QML value types, so a
// style writes `border.color: ...` or `padding { left: 8; right: 8 }`.

class StyleFont
{
    Q_GADGET
    QML_VALUE_TYPE(styleFont)
    Q_PROPERTY(QColor color READ color WRITE setColor RESET resetColor FINAL)
    Q_PROPERTY(int size READ size WRITE setSize RESET resetSize FINAL)
    Q_PROPERTY(int weight READ weight WRITE setWeight RESET resetWeight FINAL)

public:
    QColor color() const { return m_color.value_or(QColor()); }
    void setColor(const QColor &value) { m_color = value; }
    void resetColor() { m_color.reset(); }

    // Pixel size; unset, the application font's
    int size() const { return m_size.value_or(defaultSize()); }
    void setSize(int value) { m_size = value; }
    void resetSize() { m_size.reset(); }

    // Font.Weight; unset, normal. A control reads its font before its
    // styles are in place, so unset has to be a font's own default too.
    int weight() const { return m_weight.value_or(QFont::Normal); }
    void setWeight(int value) { m_weight = value; }
    void resetWeight() { m_weight.reset(); }

    // Later set members win
    void overlay(const StyleFont &later);

    static int defaultSize();

    friend bool operator==(const StyleFont &a, const StyleFont &b) { return a.m_color == b.m_color && a.m_size == b.m_size && a.m_weight == b.m_weight; }
    friend bool operator!=(const StyleFont &a, const StyleFont &b) { return !(a == b); }

private:
    std::optional<QColor> m_color;
    std::optional<int> m_size;
    std::optional<int> m_weight;
};

class StyleBorder
{
    Q_GADGET
    QML_VALUE_TYPE(styleBorder)
    Q_PROPERTY(QColor color READ color WRITE setColor RESET resetColor FINAL)
    Q_PROPERTY(int width READ width WRITE setWidth RESET resetWidth FINAL)

public:
    QColor color() const { return m_color.value_or(QColor(Qt::transparent)); }
    void setColor(const QColor &value) { m_color = value; }
    void resetColor() { m_color.reset(); }

    int width() const { return m_width.value_or(0); }
    void setWidth(int value) { m_width = value; }
    void resetWidth() { m_width.reset(); }

    void overlay(const StyleBorder &later);

    friend bool operator==(const StyleBorder &a, const StyleBorder &b) { return a.m_color == b.m_color && a.m_width == b.m_width; }
    friend bool operator!=(const StyleBorder &a, const StyleBorder &b) { return !(a == b); }

private:
    std::optional<QColor> m_color;
    std::optional<int> m_width;
};

// One radius for all corners, or one per corner. A corner set later beats
// an earlier value and the other way round, as with CSS shorthands.
class StyleRadius
{
    Q_GADGET
    QML_VALUE_TYPE(styleRadius)
    Q_PROPERTY(int value READ value WRITE setValue RESET resetValue FINAL)
    Q_PROPERTY(int topLeft READ topLeft WRITE setTopLeft RESET resetTopLeft FINAL)
    Q_PROPERTY(int topRight READ topRight WRITE setTopRight RESET resetTopRight FINAL)
    Q_PROPERTY(int bottomLeft READ bottomLeft WRITE setBottomLeft RESET resetBottomLeft FINAL)
    Q_PROPERTY(int bottomRight READ bottomRight WRITE setBottomRight RESET resetBottomRight FINAL)

public:
    int value() const { return m_value.value_or(0); }
    void setValue(int value) { m_value = value; }
    void resetValue() { m_value.reset(); }

    int topLeft() const { return m_topLeft.value_or(value()); }
    void setTopLeft(int value) { m_topLeft = value; }
    void resetTopLeft() { m_topLeft.reset(); }

    int topRight() const { return m_topRight.value_or(value()); }
    void setTopRight(int value) { m_topRight = value; }
    void resetTopRight() { m_topRight.reset(); }

    int bottomLeft() const { return m_bottomLeft.value_or(value()); }
    void setBottomLeft(int value) { m_bottomLeft = value; }
    void resetBottomLeft() { m_bottomLeft.reset(); }

    int bottomRight() const { return m_bottomRight.value_or(value()); }
    void setBottomRight(int value) { m_bottomRight = value; }
    void resetBottomRight() { m_bottomRight.reset(); }

    void overlay(const StyleRadius &later);

    friend bool operator==(const StyleRadius &a, const StyleRadius &b) { return a.m_value == b.m_value && a.m_topLeft == b.m_topLeft && a.m_topRight == b.m_topRight && a.m_bottomLeft == b.m_bottomLeft && a.m_bottomRight == b.m_bottomRight; }
    friend bool operator!=(const StyleRadius &a, const StyleRadius &b) { return !(a == b); }

private:
    std::optional<int> m_value;
    std::optional<int> m_topLeft;
    std::optional<int> m_topRight;
    std::optional<int> m_bottomLeft;
    std::optional<int> m_bottomRight;
};

// All sides (value), two sides (horizontal, vertical) or one. Reading a
// side gives the most specific value set for it; a later general value
// beats an earlier specific one.
class StylePadding
{
    Q_GADGET
    QML_VALUE_TYPE(stylePadding)
    Q_PROPERTY(int value READ value WRITE setValue RESET resetValue FINAL)
    Q_PROPERTY(int horizontal READ horizontal WRITE setHorizontal RESET resetHorizontal FINAL)
    Q_PROPERTY(int vertical READ vertical WRITE setVertical RESET resetVertical FINAL)
    Q_PROPERTY(int left READ left WRITE setLeft RESET resetLeft FINAL)
    Q_PROPERTY(int right READ right WRITE setRight RESET resetRight FINAL)
    Q_PROPERTY(int top READ top WRITE setTop RESET resetTop FINAL)
    Q_PROPERTY(int bottom READ bottom WRITE setBottom RESET resetBottom FINAL)

public:
    int value() const { return m_value.value_or(0); }
    void setValue(int value) { m_value = value; }
    void resetValue() { m_value.reset(); }

    int horizontal() const { return m_horizontal.value_or(value()); }
    void setHorizontal(int value) { m_horizontal = value; }
    void resetHorizontal() { m_horizontal.reset(); }

    int vertical() const { return m_vertical.value_or(value()); }
    void setVertical(int value) { m_vertical = value; }
    void resetVertical() { m_vertical.reset(); }

    int left() const { return m_left.value_or(horizontal()); }
    void setLeft(int value) { m_left = value; }
    void resetLeft() { m_left.reset(); }

    int right() const { return m_right.value_or(horizontal()); }
    void setRight(int value) { m_right = value; }
    void resetRight() { m_right.reset(); }

    int top() const { return m_top.value_or(vertical()); }
    void setTop(int value) { m_top = value; }
    void resetTop() { m_top.reset(); }

    int bottom() const { return m_bottom.value_or(vertical()); }
    void setBottom(int value) { m_bottom = value; }
    void resetBottom() { m_bottom.reset(); }

    void overlay(const StylePadding &later);

    friend bool operator==(const StylePadding &a, const StylePadding &b) { return a.m_value == b.m_value && a.m_horizontal == b.m_horizontal && a.m_vertical == b.m_vertical && a.m_left == b.m_left && a.m_right == b.m_right && a.m_top == b.m_top && a.m_bottom == b.m_bottom; }
    friend bool operator!=(const StylePadding &a, const StylePadding &b) { return !(a == b); }

private:
    // Each side as set, from the most specific member
    std::optional<int> side(const std::optional<int> &own, const std::optional<int> &axis) const;

    std::optional<int> m_value;
    std::optional<int> m_horizontal;
    std::optional<int> m_vertical;
    std::optional<int> m_left;
    std::optional<int> m_right;
    std::optional<int> m_top;
    std::optional<int> m_bottom;
};

struct StyleValues
{
    std::optional<QColor> background;
    StyleFont text;
    StyleFont icon;
    std::optional<QColor> placeholder;   // placeholder text
    std::optional<QColor> selection;     // selected text's background
    std::optional<QColor> selectedText;
    std::optional<QColor> accent;        // the mark in an indicator: a tick, a dot
    std::optional<QColor> dim;           // behind a popup, over the window
    StyleBorder border;
    StyleRadius radius;
    StylePadding padding;
    std::optional<int> spacing;
    std::optional<int> width;            // implicit size
    std::optional<int> height;
    std::optional<qreal> opacity;

    // Every value `later` sets replaces this one's
    void overlay(const StyleValues &later);
    // Text and icon a part does not set itself come from its control
    void inheritFrom(const StyleValues &parent);

};
