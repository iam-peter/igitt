#pragma once

#include "stylevalues.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QtQml/qqml.h>

// The state a control is in, for a style's rules
namespace StyleState {
Q_NAMESPACE
QML_ELEMENT

enum State {
    Any,     // a rule for every state
    Idle,
    Hover,   // under the pointer
    Active,  // pressed or checked
    Disable,
    Edit,    // a field with the keyboard focus
};
Q_ENUM_NS(State)
} // namespace StyleState

// A part of a control that has a cascade of its own, as the box of a
// check box or the thumb of a switch
namespace StylePart {
Q_NAMESPACE
QML_ELEMENT

enum Part {
    None,
    Indicator, // a check box's box, a switch's track, a spin box's step
    Handle,    // a switch's or scroll bar's thumb
    Label,     // a title
    Icon,      // an icon beside a label
    Popup,
    Track,
    Progress,
    Separator,
    Arrow,     // a combo box's chevron
    Leading,   // what stands before a field's text: an icon, items
    Trailing,  // what stands after it: buttons, items
    Unit,      // the unit after a value, "cm"
};
Q_ENUM_NS(Part)
} // namespace StylePart

// A set of style values, with rules for states and parts nested inside:
//
//     Style {
//         background: semantics.outline_default
//         Style { state: StyleState.Hover; background: semantics.outline_hover }
//         Style { part: StylePart.Indicator; accent: semantics.primary_default }
//     }
//
// A rule applies in its state (Any: always). One with a part applies to
// that part of the control, in the control's state, and its own rules
// in the part's. includes are other styles applied first, so a style can
// build on others ("the outlined button, small").
class Style : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_CLASSINFO("DefaultProperty", "rules")

    Q_PROPERTY(StyleState::State state READ state WRITE setState NOTIFY changed FINAL)
    Q_PROPERTY(StylePart::Part part READ part WRITE setPart NOTIFY changed FINAL)
    Q_PROPERTY(QQmlListProperty<Style> rules READ rules FINAL)
    Q_PROPERTY(QQmlListProperty<Style> includes READ includes FINAL)

    Q_PROPERTY(QColor background READ background WRITE setBackground RESET resetBackground NOTIFY changed FINAL)
    Q_PROPERTY(StyleFont text READ text WRITE setText NOTIFY changed FINAL)
    Q_PROPERTY(StyleFont icon READ icon WRITE setIcon NOTIFY changed FINAL)
    Q_PROPERTY(QColor placeholder READ placeholder WRITE setPlaceholder RESET resetPlaceholder NOTIFY changed FINAL)
    Q_PROPERTY(QColor selection READ selection WRITE setSelection RESET resetSelection NOTIFY changed FINAL)
    Q_PROPERTY(QColor selectedText READ selectedText WRITE setSelectedText RESET resetSelectedText NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent RESET resetAccent NOTIFY changed FINAL)
    Q_PROPERTY(QColor dim READ dim WRITE setDim RESET resetDim NOTIFY changed FINAL)
    Q_PROPERTY(StyleBorder border READ border WRITE setBorder NOTIFY changed FINAL)
    Q_PROPERTY(StyleRadius radius READ radius WRITE setRadius NOTIFY changed FINAL)
    Q_PROPERTY(StylePadding padding READ padding WRITE setPadding NOTIFY changed FINAL)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing RESET resetSpacing NOTIFY changed FINAL)
    Q_PROPERTY(int width READ width WRITE setWidth RESET resetWidth NOTIFY changed FINAL)
    Q_PROPERTY(int height READ height WRITE setHeight RESET resetHeight NOTIFY changed FINAL)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity RESET resetOpacity NOTIFY changed FINAL)

public:
    explicit Style(QObject *parent = nullptr);

    StyleState::State state() const { return m_state; }
    void setState(StyleState::State state);
    StylePart::Part part() const { return m_part; }
    void setPart(StylePart::Part part);

    QQmlListProperty<Style> rules();
    QQmlListProperty<Style> includes();
    QList<Style *> ruleList() const;
    QList<Style *> includeList() const;

    const StyleValues &values() const { return m_values; }

    // Whether the rule applies to a control in this state
    bool appliesIn(StyleState::State state) const
    {
        return m_state == StyleState::Any || m_state == state;
    }

    QColor background() const { return m_values.background.value_or(QColor()); }
    void setBackground(const QColor &value) { set(m_values.background, value); }
    void resetBackground() { reset(m_values.background); }

    StyleFont text() const { return m_values.text; }
    void setText(const StyleFont &value) { set(m_values.text, value); }

    StyleFont icon() const { return m_values.icon; }
    void setIcon(const StyleFont &value) { set(m_values.icon, value); }

    QColor placeholder() const { return m_values.placeholder.value_or(QColor()); }
    void setPlaceholder(const QColor &value) { set(m_values.placeholder, value); }
    void resetPlaceholder() { reset(m_values.placeholder); }

    QColor selection() const { return m_values.selection.value_or(QColor()); }
    void setSelection(const QColor &value) { set(m_values.selection, value); }
    void resetSelection() { reset(m_values.selection); }

    QColor selectedText() const { return m_values.selectedText.value_or(QColor()); }
    void setSelectedText(const QColor &value) { set(m_values.selectedText, value); }
    void resetSelectedText() { reset(m_values.selectedText); }

    QColor accent() const { return m_values.accent.value_or(QColor()); }
    void setAccent(const QColor &value) { set(m_values.accent, value); }
    void resetAccent() { reset(m_values.accent); }

    QColor dim() const { return m_values.dim.value_or(QColor()); }
    void setDim(const QColor &value) { set(m_values.dim, value); }
    void resetDim() { reset(m_values.dim); }

    StyleBorder border() const { return m_values.border; }
    void setBorder(const StyleBorder &value) { set(m_values.border, value); }

    StyleRadius radius() const { return m_values.radius; }
    void setRadius(const StyleRadius &value) { set(m_values.radius, value); }

    StylePadding padding() const { return m_values.padding; }
    void setPadding(const StylePadding &value) { set(m_values.padding, value); }

    int spacing() const { return m_values.spacing.value_or(0); }
    void setSpacing(int value) { set(m_values.spacing, value); }
    void resetSpacing() { reset(m_values.spacing); }

    int width() const { return m_values.width.value_or(0); }
    void setWidth(int value) { set(m_values.width, value); }
    void resetWidth() { reset(m_values.width); }

    int height() const { return m_values.height.value_or(0); }
    void setHeight(int value) { set(m_values.height, value); }
    void resetHeight() { reset(m_values.height); }

    qreal opacity() const { return m_values.opacity.value_or(1.0); }
    void setOpacity(qreal value) { set(m_values.opacity, value); }
    void resetOpacity() { reset(m_values.opacity); }

signals:
    // Any value, rule or include changed, also deep inside
    void changed();

private:
    template<typename T, typename V>
    void set(T &slot, const V &value)
    {
        if (slot == T(value))
            return;
        slot = value;
        emitChanged();
    }

    template<typename T>
    void reset(std::optional<T> &slot)
    {
        if (!slot)
            return;
        slot.reset();
        emitChanged();
    }

    static void append(QQmlListProperty<Style> *list, Style *style);
    static qsizetype count(QQmlListProperty<Style> *list);
    static Style *at(QQmlListProperty<Style> *list, qsizetype index);
    static void clear(QQmlListProperty<Style> *list);

    void reconnect();
    void emitChanged();

    StyleValues m_values;
    QList<QPointer<Style>> m_rules;
    QList<QPointer<Style>> m_includes;
    QList<QMetaObject::Connection> m_connections;
    StyleState::State m_state = StyleState::Any;
    StylePart::Part m_part = StylePart::None;
    bool m_emitting = false;
};
