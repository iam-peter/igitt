#include "stylecascade.h"

#include <QLoggingCategory>
#include <QSet>

namespace {

Q_LOGGING_CATEGORY(styleLog, "igitt.style", QtWarningMsg)

using Path = QList<const Style *>;

// Styles that include each other end here instead of recursing forever
bool enter(const Style *style, Path &path)
{
    if (path.contains(style)) {
        qCWarning(styleLog) << "Style includes itself:" << style;
        return false;
    }
    path.append(style);
    return true;
}

// Applies a layer for a state: its includes, its own values, then its
// rules for the state; rules for a part are left to that part
void apply(StyleValues &values, const Style *style, StyleState::State state, Path &path)
{
    if (!style->appliesIn(state) || !enter(style, path))
        return;
    for (const Style *included : style->includeList())
        apply(values, included, state, path);
    values.overlay(style->values());
    for (const Style *rule : style->ruleList()) {
        if (rule->part() == StylePart::None)
            apply(values, rule, state, path);
    }
    path.removeLast();
}

// The rules a layer has for a part, as they apply in the control's state
void collectPart(QList<Style *> &rules, const Style *style, StylePart::Part part,
                 StyleState::State state, Path &path)
{
    if (!style->appliesIn(state) || !enter(style, path))
        return;
    for (const Style *included : style->includeList())
        collectPart(rules, included, part, state, path);
    for (Style *rule : style->ruleList()) {
        if (rule->part() == part)
            rules.append(rule);
        else if (rule->part() == StylePart::None)
            collectPart(rules, rule, part, state, path);
    }
    path.removeLast();
}

QList<Style *> live(const QList<QPointer<Style>> &list)
{
    QList<Style *> styles;
    for (const QPointer<Style> &style : list) {
        if (style)
            styles.append(style.data());
    }
    return styles;
}

QList<QPointer<Style>> &listOf(QQmlListProperty<Style> *list)
{
    return *static_cast<QList<QPointer<Style>> *>(list->data);
}

} // namespace

StyleCascade::StyleCascade(QObject *parent)
    : QObject(parent)
{}

QQmlListProperty<Style> StyleCascade::base()
{
    return {this, &m_base, &StyleCascade::append, &StyleCascade::count, &StyleCascade::at,
            &StyleCascade::clear};
}

QQmlListProperty<Style> StyleCascade::styles()
{
    return {this, &m_styles, &StyleCascade::append, &StyleCascade::count, &StyleCascade::at,
            &StyleCascade::clear};
}

QQmlListProperty<Style> StyleCascade::overrides()
{
    return {this, &m_overrides, &StyleCascade::append, &StyleCascade::count, &StyleCascade::at,
            &StyleCascade::clear};
}

void StyleCascade::append(QQmlListProperty<Style> *list, Style *style)
{
    listOf(list).append(style);
    static_cast<StyleCascade *>(list->object)->listChanged();
}

qsizetype StyleCascade::count(QQmlListProperty<Style> *list)
{
    return listOf(list).size();
}

Style *StyleCascade::at(QQmlListProperty<Style> *list, qsizetype index)
{
    return listOf(list).at(index);
}

void StyleCascade::clear(QQmlListProperty<Style> *list)
{
    listOf(list).clear();
    static_cast<StyleCascade *>(list->object)->listChanged();
}

StyleState::State StyleCascade::state() const
{
    if (m_state)
        return *m_state;
    if (m_parent)
        return m_parent->state();
    return StyleState::Idle;
}

void StyleCascade::setState(StyleState::State state)
{
    if (m_state == state)
        return;
    m_state = state;
    invalidate();
}

void StyleCascade::resetState()
{
    if (!m_state)
        return;
    m_state.reset();
    invalidate();
}

void StyleCascade::setPart(StylePart::Part part)
{
    if (m_part == part)
        return;
    m_part = part;
    invalidate();
}

void StyleCascade::setParentCascade(StyleCascade *parent)
{
    if (m_parent == parent)
        return;
    disconnect(m_parentConnection);
    m_parent = parent;
    if (m_parent)
        m_parentConnection = connect(m_parent, &StyleCascade::changed, this, &StyleCascade::invalidate);
    invalidate();
}

void StyleCascade::componentComplete()
{
    m_complete = true;
    if (live(m_base).isEmpty() && !m_parent)
        qCWarning(styleLog) << "StyleCascade without a base style:" << parent();
    invalidate();
}

// The cascade follows every style in its own layers; the parent's tells it
// about the rest.
//
// QML fills a list by clearing it and appending item by item, so it is
// only told once the list is complete, after the current event: read in
// between, an empty list would leave a button without its colors.
void StyleCascade::listChanged()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    const QList<Style *> own = live(m_base) + live(m_styles) + live(m_overrides);
    for (Style *style : own)
        m_connections.append(connect(style, &Style::changed, this, &StyleCascade::invalidate));
    m_dirty = true;
    if (!m_complete || m_notifyScheduled)
        return;
    m_notifyScheduled = true;
    QMetaObject::invokeMethod(this, [this] {
        m_notifyScheduled = false;
        invalidate();
    }, Qt::QueuedConnection);
}

// Resolving waits for the next read; until the cascade is complete nobody
// is told, as its lists are still being filled
void StyleCascade::invalidate()
{
    m_dirty = true;
    if (m_complete)
        emit changed();
}

QList<Style *> StyleCascade::layers() const
{
    QList<Style *> layers = live(m_base);
    if (m_parent && m_part != StylePart::None) {
        const StyleState::State parentState = m_parent->state();
        for (const Style *layer : m_parent->layers()) {
            Path path;
            collectPart(layers, layer, m_part, parentState, path);
        }
    }
    layers += live(m_styles);
    layers += live(m_overrides);
    return layers;
}

const StyleValues &StyleCascade::values() const
{
    if (!m_dirty)
        return m_values;
    StyleValues values;
    const StyleState::State current = state();
    for (const Style *layer : layers()) {
        Path path;
        apply(values, layer, current, path);
    }
    if (m_parent)
        values.inheritFrom(m_parent->values());
    m_values = values;
    m_dirty = false;
    return m_values;
}

// Once per value: a control reads something no style of it sets
void StyleCascade::warnUnset(const char *name) const
{
    if (!m_complete || m_warned.contains(name))
        return;
    m_warned.insert(name);
    qCWarning(styleLog) << "No style sets" << name << "for" << parent() << "part" << m_part;
}
