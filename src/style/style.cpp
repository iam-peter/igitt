#include "style.h"

Style::Style(QObject *parent)
    : QObject(parent)
{}

void Style::setState(StyleState::State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emitChanged();
}

void Style::setPart(StylePart::Part part)
{
    if (m_part == part)
        return;
    m_part = part;
    emitChanged();
}

// Both lists share the accessors; the list's data says which one it is
QQmlListProperty<Style> Style::rules()
{
    return {this, &m_rules, &Style::append, &Style::count, &Style::at, &Style::clear};
}

QQmlListProperty<Style> Style::includes()
{
    return {this, &m_includes, &Style::append, &Style::count, &Style::at, &Style::clear};
}

namespace {

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

QList<Style *> Style::ruleList() const
{
    return live(m_rules);
}

QList<Style *> Style::includeList() const
{
    return live(m_includes);
}

void Style::append(QQmlListProperty<Style> *list, Style *style)
{
    listOf(list).append(style);
    auto owner = static_cast<Style *>(list->object);
    owner->reconnect();
    owner->emitChanged();
}

qsizetype Style::count(QQmlListProperty<Style> *list)
{
    return listOf(list).size();
}

Style *Style::at(QQmlListProperty<Style> *list, qsizetype index)
{
    return listOf(list).at(index);
}

void Style::clear(QQmlListProperty<Style> *list)
{
    listOf(list).clear();
    auto owner = static_cast<Style *>(list->object);
    owner->reconnect();
    owner->emitChanged();
}

// A change inside a rule or an include is a change of this style
void Style::reconnect()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    const QList<Style *> children = ruleList() + includeList();
    for (Style *child : children)
        m_connections.append(connect(child, &Style::changed, this, &Style::emitChanged));
}

// Guarded: styles that include each other would pass it on forever
void Style::emitChanged()
{
    if (m_emitting)
        return;
    m_emitting = true;
    emit changed();
    m_emitting = false;
}
