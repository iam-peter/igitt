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

// The theme in use, for Semantics.qml: the built-in themes and those in
// the user's folder, a light and a dark one chosen of them, and whether
// the light or the dark one is shown (with the system, or always one).
// The choice is kept in the settings ("appearance"). Theme files that
// change are read again at once, so a theme can be written while igitt
// shows it.
class Themes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "system", "light" or "dark"
    Q_PROPERTY(QString scheme READ scheme WRITE setScheme NOTIFY schemeChanged FINAL)
    Q_PROPERTY(QString lightTheme READ lightTheme WRITE setLightTheme NOTIFY schemeChanged FINAL)
    Q_PROPERTY(QString darkTheme READ darkTheme WRITE setDarkTheme NOTIFY schemeChanged FINAL)
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

    QString scheme() const { return m_scheme; }
    void setScheme(const QString &scheme);
    QString lightTheme() const { return m_lightTheme; }
    void setLightTheme(const QString &id);
    QString darkTheme() const { return m_darkTheme; }
    void setDarkTheme(const QString &id);

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
    void schemeChanged();
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
    QString m_scheme;
    QString m_lightTheme;
    QString m_darkTheme;
    QString m_current;
    bool m_dark = false;
    QVariantMap m_roles;
    QStringList m_errors;
};
