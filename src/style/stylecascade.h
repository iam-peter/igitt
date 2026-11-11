#pragma once

#include "style.h"

#include <QSet>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QtQml/qqml.h>

// The style values of one control (or one part of it) in its state.
//
// A control lists its styles in layers, each later one overriding the
// earlier: base (the control's default, e.g. ButtonStyle.base), styles
// (what a page chooses, e.g. ButtonStyle.ghost), overrides (what the
// control itself forces, e.g. the focus ring). The cascade applies every
// layer's rules for its state and passes the result on:
//
//     StyleCascade {
//         id: cascade
//         base: [ButtonStyle.base]
//         styles: control.styles
//         state: control.hovered ? StyleState.Hover : StyleState.Idle
//     }
//     background: Rectangle { color: cascade.background }
//
// A part's cascade has the control's as parentCascade and gets the rules
// for its part from the control's layers; it is in the control's state
// unless it sets its own, and takes text and icon from the control where
// its rules do not set them.
class StyleCascade : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QQmlListProperty<Style> base READ base FINAL)
    Q_PROPERTY(QQmlListProperty<Style> styles READ styles FINAL)
    Q_PROPERTY(QQmlListProperty<Style> overrides READ overrides FINAL)
    Q_PROPERTY(StyleState::State state READ state WRITE setState RESET resetState NOTIFY changed FINAL)
    Q_PROPERTY(StylePart::Part part READ part WRITE setPart NOTIFY changed FINAL)
    Q_PROPERTY(StyleCascade *parentCascade READ parentCascade WRITE setParentCascade NOTIFY changed FINAL)

    Q_PROPERTY(QColor background READ background NOTIFY changed FINAL)
    Q_PROPERTY(StyleFont text READ text NOTIFY changed FINAL)
    Q_PROPERTY(StyleFont icon READ icon NOTIFY changed FINAL)
    Q_PROPERTY(QColor placeholder READ placeholder NOTIFY changed FINAL)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed FINAL)
    Q_PROPERTY(QColor selectedText READ selectedText NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor dim READ dim NOTIFY changed FINAL)
    Q_PROPERTY(StyleBorder border READ border NOTIFY changed FINAL)
    Q_PROPERTY(StyleRadius radius READ radius NOTIFY changed FINAL)
    Q_PROPERTY(StylePadding padding READ padding NOTIFY changed FINAL)
    Q_PROPERTY(int spacing READ spacing NOTIFY changed FINAL)
    Q_PROPERTY(int width READ width NOTIFY changed FINAL)
    Q_PROPERTY(int height READ height NOTIFY changed FINAL)
    Q_PROPERTY(qreal opacity READ opacity NOTIFY changed FINAL)

public:
    explicit StyleCascade(QObject *parent = nullptr);

    QQmlListProperty<Style> base();
    QQmlListProperty<Style> styles();
    QQmlListProperty<Style> overrides();

    StyleState::State state() const;
    void setState(StyleState::State state);
    void resetState();

    StylePart::Part part() const { return m_part; }
    void setPart(StylePart::Part part);

    StyleCascade *parentCascade() const { return m_parent; }
    void setParentCascade(StyleCascade *parent);

    const StyleValues &values() const;

    QColor background() const { return read(values().background, "background", QColor(Qt::transparent)); }
    StyleFont text() const { return values().text; }
    StyleFont icon() const { return values().icon; }
    QColor placeholder() const { return read(values().placeholder, "placeholder", QColor()); }
    QColor selection() const { return read(values().selection, "selection", QColor()); }
    QColor selectedText() const { return read(values().selectedText, "selectedText", QColor()); }
    QColor accent() const { return read(values().accent, "accent", QColor()); }
    QColor dim() const { return read(values().dim, "dim", QColor(Qt::transparent)); }
    StyleBorder border() const { return values().border; }
    StyleRadius radius() const { return values().radius; }
    StylePadding padding() const { return values().padding; }
    int spacing() const { return read(values().spacing, "spacing", 0); }
    int width() const { return read(values().width, "width", 0); }
    int height() const { return read(values().height, "height", 0); }
    qreal opacity() const { return read(values().opacity, "opacity", 1.0); }

    void classBegin() override {}
    void componentComplete() override;

signals:
    // The resolved values may have changed: a layer, the state or the
    // parent changed
    void changed();

private:
    // The layers in order: base, the parent's rules for this part, styles,
    // overrides. Part cascades below this one look for their rules in it.
    QList<Style *> layers() const;

    template<typename T>
    T read(const std::optional<T> &slot, const char *name, const T &fallback) const
    {
        if (!slot)
            warnUnset(name);
        return slot.value_or(fallback);
    }

    void warnUnset(const char *name) const;
    void listChanged();
    void invalidate();

    static void append(QQmlListProperty<Style> *list, Style *style);
    static qsizetype count(QQmlListProperty<Style> *list);
    static Style *at(QQmlListProperty<Style> *list, qsizetype index);
    static void clear(QQmlListProperty<Style> *list);

    QList<QPointer<Style>> m_base;
    QList<QPointer<Style>> m_styles;
    QList<QPointer<Style>> m_overrides;
    QList<QMetaObject::Connection> m_connections;
    QMetaObject::Connection m_parentConnection;
    QPointer<StyleCascade> m_parent;
    std::optional<StyleState::State> m_state;
    StylePart::Part m_part = StylePart::None;
    mutable StyleValues m_values;
    mutable bool m_dirty = true;
    mutable QSet<QByteArray> m_warned;
    bool m_complete = false;
    bool m_notifyScheduled = false;
};
