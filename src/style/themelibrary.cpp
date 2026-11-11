#include "themelibrary.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace {

using Kind = ThemeLibrary::Kind;

// The two themes every other one builds on in the end
bool isRoot(const QString &id)
{
    return id == QLatin1String("light") || id == QLatin1String("dark");
}

// #rrggbb, or #rrggbbaa with the opacity last as in CSS (QColor reads
// eight digits as #aarrggbb)
std::optional<QColor> parseHex(const QString &text)
{
    static const QRegularExpression hex(QStringLiteral("^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$"));
    const QRegularExpressionMatch match = hex.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    QColor color = QColor::fromString(u'#' + match.captured(1));
    if (match.hasCaptured(2))
        color.setAlpha(match.captured(2).toInt(nullptr, 16));
    return color;
}

// A role's color: #hex or the name of one of the theme's colors, either
// with @opacity after it ("crust@0.6")
std::optional<QColor> parseColor(const QJsonValue &value, const QJsonObject &colors, QString *error)
{
    if (!value.isString()) {
        *error = QStringLiteral("is not a color");
        return std::nullopt;
    }
    QString text = value.toString().trimmed();
    std::optional<double> opacity;
    if (const qsizetype at = text.lastIndexOf(u'@'); at >= 0) {
        bool ok = false;
        const double a = text.mid(at + 1).toDouble(&ok);
        if (!ok || a < 0 || a > 1) {
            *error = QStringLiteral("has an opacity that is no number from 0 to 1: %1").arg(text);
            return std::nullopt;
        }
        opacity = a;
        text = text.left(at).trimmed();
    }
    std::optional<QColor> color;
    if (text.startsWith(u'#')) {
        color = parseHex(text);
        if (!color) {
            *error = QStringLiteral("is no #rrggbb or #rrggbbaa color: %1").arg(text);
            return std::nullopt;
        }
    } else {
        const QJsonValue named = colors.value(text);
        if (named.isUndefined()) {
            *error = QStringLiteral("names no color of the theme: %1").arg(text);
            return std::nullopt;
        }
        color = parseHex(named.toString());
        if (!color) {
            *error = QStringLiteral("names %1, which is no #rrggbb or #rrggbbaa color").arg(text);
            return std::nullopt;
        }
    }
    if (opacity)
        color->setAlphaF(color->alphaF() * *opacity);
    return color;
}

std::optional<QVariant> parseRole(Kind kind, const QJsonValue &value, const QJsonObject &colors,
                                  QString *error)
{
    switch (kind) {
    case Kind::Color:
        if (const std::optional<QColor> color = parseColor(value, colors, error))
            return QVariant::fromValue(*color);
        return std::nullopt;
    case Kind::ColorList: {
        if (!value.isArray() || value.toArray().isEmpty()) {
            *error = QStringLiteral("is no list of colors");
            return std::nullopt;
        }
        QVariantList list;
        for (const QJsonValue &entry : value.toArray()) {
            const std::optional<QColor> color = parseColor(entry, colors, error);
            if (!color)
                return std::nullopt;
            list.append(QVariant::fromValue(*color));
        }
        return list;
    }
    case Kind::Number:
        if (!value.isDouble()) {
            *error = QStringLiteral("is no number");
            return std::nullopt;
        }
        return value.toDouble();
    }
    return std::nullopt;
}

QVariant fallback(Kind kind)
{
    switch (kind) {
    case Kind::Color: return QVariant::fromValue(QColor(Qt::transparent));
    case Kind::ColorList: return QVariantList{QVariant::fromValue(QColor(Qt::gray))};
    case Kind::Number: return 0.0;
    }
    return {};
}

} // namespace

const QList<ThemeLibrary::Role> &ThemeLibrary::roles()
{
    static const QList<Role> roles = [] {
        QList<Role> list;
        const auto colors = {
            "text_default", "text_muted", "text_inactive", "text_on_primary", "text_on_danger",
            "text_link",
            "primary_default", "danger_default", "danger_fill",
            "success_default", "warning_default", "info_default", "special_default",
            "icon_folder",
            "background_default", "background_elevated", "background_muted", "background_secondary",
            "background_accent", "background_hover", "background_stripe",
            "selection_default",
            "field_default", "field_hover",
            "outline_default", "outline_hover",
            "stroke_subtle", "stroke_field", "stroke_focus", "stroke_outline", "stroke_tab",
            "switch_thumb", "switch_thumb_checked", "scroll_thumb",
            "diff_added", "diff_removed", "diff_hunk", "diff_added_word", "diff_removed_word",
            "syntax_keyword", "syntax_type", "syntax_function", "syntax_string", "syntax_number",
            "syntax_comment", "syntax_preprocessor", "syntax_heading",
            "dim", "dim_subtle",
        };
        for (const char *name : colors)
            list.append({QString::fromLatin1(name), Kind::Color});
        list.append({QStringLiteral("graph_lanes"), Kind::ColorList});
        list.append({QStringLiteral("shadow_strength"), Kind::Number});
        return list;
    }();
    return roles;
}

void ThemeLibrary::load(const QStringList &folders)
{
    m_files.clear();
    m_loadErrors.clear();
    for (qsizetype i = 0; i < folders.size(); ++i) {
        // Only the first folder is igitt's own
        const bool user = i > 0;
        const QDir dir(folders[i]);
        const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            const QString path = dir.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                m_loadErrors.append(QStringLiteral("%1: %2").arg(path, file.errorString()));
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (!document.isObject()) {
                m_loadErrors.append(QStringLiteral("%1: %2 at offset %3")
                                        .arg(path, parseError.errorString())
                                        .arg(parseError.offset));
                continue;
            }
            const QJsonObject json = document.object();
            if (json.value(u"name").toString().isEmpty()) {
                m_loadErrors.append(QStringLiteral("%1: the theme has no name").arg(path));
                continue;
            }
            const QString id = QFileInfo(name).completeBaseName();
            m_files[id].append(File{id, path, json, user});
        }
    }
}

QList<ThemeLibrary::Theme> ThemeLibrary::themes() const
{
    QList<Theme> list;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        Merged merged;
        QStringList visiting;
        merge(it.key(), it.value().size() - 1, merged, visiting, nullptr);
        const File &top = it.value().constLast();
        list.append(Theme{it.key(), top.json.value(u"name").toString(), merged.dark, top.user, top.path});
    }
    // Light and dark first, then the others by name
    std::sort(list.begin(), list.end(), [](const Theme &a, const Theme &b) {
        if (isRoot(a.id) != isRoot(b.id))
            return isRoot(a.id);
        if (isRoot(a.id))
            return a.id == QLatin1String("light");
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return list;
}

// The colors and roles of a theme and every one below it, before the
// roles are read
bool ThemeLibrary::merge(const QString &id, qsizetype depth, Merged &out, QStringList &visiting,
                         QStringList *errors) const
{
    const auto it = m_files.constFind(id);
    if (it == m_files.cend() || depth < 0 || depth >= it->size())
        return false;
    const File &file = it->at(depth);
    const QString key = QStringLiteral("%1#%2").arg(id).arg(depth);
    if (visiting.contains(key)) {
        if (errors)
            errors->append(QStringLiteral("%1: the theme inherits itself").arg(file.path));
        return false;
    }
    visiting.append(key);

    // Below a theme: the one it names; one that hides a built-in theme of
    // its id builds on that; light and dark on nothing; any other on
    // light or dark, as it is
    const QJsonValue inherits = file.json.value(u"inherits");
    QString parent;
    qsizetype parentDepth = -1;
    if (inherits.isString()) {
        parent = inherits.toString();
        if (parent == id) {
            parentDepth = depth - 1;
        } else if (m_files.contains(parent)) {
            parentDepth = m_files.value(parent).size() - 1;
        } else if (errors) {
            errors->append(QStringLiteral("%1: inherits %2, which is no theme").arg(file.path, parent));
        }
    } else if (depth > 0) {
        parent = id;
        parentDepth = depth - 1;
    } else if (!isRoot(id)) {
        parent = file.json.value(u"dark").toBool() ? QStringLiteral("dark") : QStringLiteral("light");
        parentDepth = m_files.value(parent).size() - 1;
    }
    if (parentDepth >= 0)
        merge(parent, parentDepth, out, visiting, errors);

    if (file.json.contains(u"dark"))
        out.dark = file.json.value(u"dark").toBool();
    const QJsonObject colors = file.json.value(u"colors").toObject();
    for (auto c = colors.begin(); c != colors.end(); ++c)
        out.colors.insert(c.key(), c.value());
    const QJsonObject roles = file.json.value(u"roles").toObject();
    for (auto r = roles.begin(); r != roles.end(); ++r) {
        const bool known = std::any_of(ThemeLibrary::roles().cbegin(), ThemeLibrary::roles().cend(),
                                       [&](const Role &role) { return role.name == r.key(); });
        if (!known) {
            if (errors)
                errors->append(QStringLiteral("%1: there is no role %2").arg(file.path, r.key()));
            continue;
        }
        // The value below stays where this one cannot be read
        if (errors && out.roles.contains(r.key())) {
            const Role role = *std::find_if(ThemeLibrary::roles().cbegin(), ThemeLibrary::roles().cend(),
                                            [&](const Role &role) { return role.name == r.key(); });
            QString error;
            if (!parseRole(role.kind, r.value(), out.colors, &error)) {
                errors->append(QStringLiteral("%1: %2 %3").arg(file.path, r.key(), error));
                continue;
            }
        }
        out.roles.insert(r.key(), r.value());
    }
    visiting.removeLast();
    return true;
}

QVariantMap ThemeLibrary::resolve(const QString &id, QStringList *errors) const
{
    QStringList ownErrors;
    QStringList &problems = errors ? *errors : ownErrors;
    Merged merged;
    QStringList visiting;
    if (!merge(id, m_files.value(id).size() - 1, merged, visiting, &problems))
        problems.append(QStringLiteral("There is no theme %1").arg(id));

    QVariantMap values;
    for (const Role &role : roles()) {
        const QJsonValue value = merged.roles.value(role.name);
        if (value.isUndefined()) {
            problems.append(QStringLiteral("%1: no theme sets %2").arg(id, role.name));
            values.insert(role.name, fallback(role.kind));
            continue;
        }
        QString error;
        const std::optional<QVariant> parsed = parseRole(role.kind, value, merged.colors, &error);
        if (!parsed) {
            problems.append(QStringLiteral("%1: %2 %3").arg(id, role.name, error));
            values.insert(role.name, fallback(role.kind));
            continue;
        }
        values.insert(role.name, *parsed);
    }
    return values;
}
