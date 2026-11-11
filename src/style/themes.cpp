#include "themes.h"

#include <QDir>
#include <QJSEngine>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>

namespace {

Q_LOGGING_CATEGORY(themeLog, "igitt.theme", QtWarningMsg)

const QString kBuiltIn = QStringLiteral(":/qt/qml/IgittStyle/themes");

QString setting(const char *key, const QString &fallback)
{
    const QString value = QSettings().value(QLatin1String("appearance/") + QLatin1String(key)).toString();
    return value.isEmpty() ? fallback : value;
}

void save(const char *key, const QString &value)
{
    QSettings().setValue(QLatin1String("appearance/") + QLatin1String(key), value);
}

} // namespace

Themes::Themes(QObject *parent)
    : QObject(parent)
    , m_builtInFolder(kBuiltIn)
    // Beside the settings, not in a folder of the organisation's
    , m_folder(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                   .filePath(QCoreApplication::applicationName() + QStringLiteral("/themes")))
{
    m_theme = setting("theme", QStringLiteral("system"));

    // An editor saves a file in a few steps; read once they are done
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, &Themes::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        // Also when the theme chosen is gone and the system's stands in
        if (m_theme == QLatin1String("system") || !m_library.contains(m_theme))
            apply();
    });
    reload();
}

Themes *Themes::instance()
{
    static Themes *themes = new Themes(qApp);
    return themes;
}

Themes *Themes::create(QQmlEngine *, QJSEngine *)
{
    Themes *themes = instance();
    QJSEngine::setObjectOwnership(themes, QJSEngine::CppOwnership);
    return themes;
}

void Themes::setFolders(const QString &builtIn, const QString &user)
{
    m_builtInFolder = builtIn;
    m_folder = user;
    reload();
}

void Themes::setTheme(const QString &theme)
{
    const QString value = theme.isEmpty() ? QStringLiteral("system") : theme;
    if (m_theme == value)
        return;
    m_theme = value;
    save("theme", value);
    emit themeChanged();
    apply();
}

QVariantList Themes::available() const
{
    QVariantList list;
    for (const ThemeLibrary::Theme &theme : m_library.themes()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), theme.id},
            {QStringLiteral("name"), theme.name},
            {QStringLiteral("dark"), theme.dark},
            {QStringLiteral("user"), theme.user},
            {QStringLiteral("path"), theme.path},
        });
    }
    return list;
}

QUrl Themes::openFolder()
{
    QDir().mkpath(m_folder);
    watch();
    return QUrl::fromLocalFile(m_folder);
}

void Themes::reload()
{
    m_library.load({m_builtInFolder, m_folder});
    watch();
    emit availableChanged();
    apply();
}

// The folder for themes that come or go, each file for edits. A file an
// editor replaces drops out of the watch, so this runs after every read.
void Themes::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QDir dir(m_folder);
    if (!dir.exists())
        return;
    m_watcher.addPath(m_folder);
    const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : names)
        m_watcher.addPath(dir.filePath(name));
}

bool Themes::systemIsDark() const
{
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
    // A desktop that does not say: its palette does
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

void Themes::apply()
{
    auto *hints = QGuiApplication::styleHints();
    QStringList errors;
    bool system = m_theme == QLatin1String("system");
    if (!system && !m_library.contains(m_theme)) {
        errors.append(QStringLiteral("There is no theme %1, the system's is shown").arg(m_theme));
        system = true;
    }
    QString id = m_theme;
    if (system) {
        // The system's own scheme again, which an earlier choice hid
        hints->unsetColorScheme();
        id = systemIsDark() ? QStringLiteral("dark") : QStringLiteral("light");
    }
    m_roles = m_library.resolve(id, &errors);
    m_current = id;
    // As the theme is
    for (const ThemeLibrary::Theme &theme : m_library.themes()) {
        if (theme.id == id)
            m_dark = theme.dark;
    }
    // Window frames and native dialogs follow the theme, where the
    // platform lets them
    if (!system)
        hints->setColorScheme(m_dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);

    errors += m_library.loadErrors();
    errors.removeDuplicates();
    if (errors != m_errors) {
        for (const QString &error : std::as_const(errors)) {
            if (!m_errors.contains(error))
                qCWarning(themeLog).noquote() << error;
        }
    }
    m_errors = errors;
    emit rolesChanged();
}
