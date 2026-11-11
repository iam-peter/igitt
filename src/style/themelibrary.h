#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The themes igitt knows: JSON files, each a named palette and the color
// roles made from it (see docs/THEMES.md).
//
//     {
//         "name": "Catppuccin Mocha",
//         "dark": true,
//         "colors": { "base": "#1e1e2e", "mauve": "#cba6f7" },
//         "roles": { "background_elevated": "base", "dim": "crust@0.6" }
//     }
//
// A theme builds on another (inherits; by default light or dark, after
// its own dark), so it only names the roles it changes. Its colors and
// roles go over those of the theme below, and only then are the roles
// read: a theme that only changes colors recolors the roles below it
// that use them.
class ThemeLibrary
{
public:
    enum class Kind { Color, ColorList, Number };
    struct Role
    {
        QString name;
        Kind kind;
    };
    // Every role a theme has, the same as Semantics.qml's properties
    static const QList<Role> &roles();

    struct Theme
    {
        QString id;    // the file name without .json
        QString name;  // for people
        bool dark = false;
        bool user = false; // from the user's folder, not built in
        QString path;
    };

    // Reads every *.json in the folders; a theme in a later folder hides
    // one of the same id in an earlier one
    void load(const QStringList &folders);

    // Light ones first, then dark ones, each by name
    QList<Theme> themes() const;
    bool contains(const QString &id) const { return m_files.contains(id); }

    // The roles of a theme, by name: a QColor, a QVariantList of them or a
    // double. What a theme gets wrong goes to errors; such a role keeps
    // the value of the theme below.
    QVariantMap resolve(const QString &id, QStringList *errors) const;

    // What could not be read at all: broken JSON, no name
    QStringList loadErrors() const { return m_loadErrors; }

private:
    struct File
    {
        QString id;
        QString path;
        QJsonObject json;
        bool user = false;
    };
    struct Merged
    {
        QJsonObject colors;
        QJsonObject roles;
        bool dark = false;
    };

    // Each id's files, the hidden ones first
    QHash<QString, QList<File>> m_files;
    QStringList m_loadErrors;

    bool merge(const QString &id, qsizetype depth, Merged &out, QStringList &visiting,
               QStringList *errors) const;
};
