#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

#include "core/AskPass.h"
#include "Fonts.h"

Q_IMPORT_QML_PLUGIN(Igitt_CorePlugin)
Q_IMPORT_QML_PLUGIN(IgittPlugin)
Q_IMPORT_QML_PLUGIN(IgittStylePlugin)

int main(int argc, char *argv[])
{
    // Started by git or ssh to ask for a password: no window of its own,
    // the igitt that runs them asks
    if (AskPass::isHelperCall(argc)) {
        QCoreApplication helper(argc, argv);
        return AskPass::runHelper(QString::fromLocal8Bit(argv[1]));
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("igitt"));
    QGuiApplication::setOrganizationName(QStringLiteral("igitt"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("igitt.local"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Igitt/logo/igitt.svg")));
    // Wayland shows the icon of the desktop file this names.
    QGuiApplication::setDesktopFileName(QStringLiteral("igitt"));
    // igitt's own style, its colors from the theme in use; what it leaves
    // out comes from Basic.
    QQuickStyle::setStyle(QStringLiteral("IgittStyle"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));
    Fonts::registerBundled();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("History browser and commit tool for git"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("Repository to open (default: current directory)"));
    parser.process(app);

    const QString path = parser.positionalArguments().value(0, QDir::currentPath());
    AskPass::instance()->start(QCoreApplication::applicationFilePath());

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.setInitialProperties({
        {QStringLiteral("initialPath"), path},
    });
    engine.loadFromModule("Igitt", "Main");
    return app.exec();
}
