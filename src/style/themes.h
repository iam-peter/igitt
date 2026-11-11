#pragma once

#include "themelibrary.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

class QJSEngine;
class QQmlEngine;

// The theme in use, for Semantics.qml: one of the built-in themes and
// those in the user's folder, or "system", which is light or dark as the
// system is. The choice is kept in the settings ("appearance/theme"). Theme files that
// change are read again at once, so a theme can be written while igitt
// shows it.
class Themes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "system" or a theme's id
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
    // The theme shown and whether it is a dark one
    Q_PROPERTY(QString current READ current NOTIFY rolesChanged FINAL)
    Q_PROPERTY(bool dark READ dark NOTIFY rolesChanged FINAL)
    // Its roles by name, see ThemeLibrary::resolve()
    Q_PROPERTY(QVariantMap roles READ roles NOTIFY rolesChanged FINAL)
    // Every theme: id, name, dark, user, path
    Q_PROPERTY(QVariantList available READ available NOTIFY availableChanged FINAL)
    // What is wrong with the theme files, the one shown's first
    Q_PROPERTY(QStringList errors READ errors NOTIFY rolesChanged FINAL)
    // Where the user's own themes go
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)

public:
    // One for all of igitt, so C++ can follow it as QML does
    static Themes *instance();
    static Themes *create(QQmlEngine *, QJSEngine *);

    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);

    QString current() const { return m_current; }
    bool dark() const { return m_dark; }
    QVariantMap roles() const { return m_roles; }
    QVariantList available() const;
    QStringList errors() const { return m_errors; }
    QString folder() const { return m_folder; }

    // The user's folder, made if it is not there yet, to open in a file
    // manager
    Q_INVOKABLE QUrl openFolder();
    Q_INVOKABLE void reload();

    // The folders themes come from; tests set their own
    void setFolders(const QString &builtIn, const QString &user);

signals:
    void themeChanged();
    void rolesChanged();
    void availableChanged();

private:
    // Only instance() makes one, which QML gets from create()
    explicit Themes(QObject *parent);

    void apply();
    void watch();
    bool systemIsDark() const;

    ThemeLibrary m_library;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QString m_builtInFolder;
    QString m_folder;
    QString m_theme;
    QString m_current;
    bool m_dark = false;
    QVariantMap m_roles;
    QStringList m_errors;
};
