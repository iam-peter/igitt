// The theme files: the built-in ones are complete and readable, and a
// user's theme builds on them as docs/THEMES.md says.

#include "themelibrary.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QColor color(const QVariantMap &roles, const QString &role)
{
    return roles.value(role).value<QColor>();
}

} // namespace

class TestThemes : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_user.isValid());
        QDir(m_user.path()).removeRecursively();
        QDir().mkpath(m_user.path());
    }

    void builtInThemesAreComplete()
    {
        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR)});
        QVERIFY(library.loadErrors().isEmpty());
        const QList<ThemeLibrary::Theme> themes = library.themes();
        QStringList ids;
        for (const ThemeLibrary::Theme &theme : themes)
            ids.append(theme.id);
        QCOMPARE(ids, (QStringList{u"light"_s, u"dark"_s, u"alucard"_s, u"catppuccin-frappe"_s,
                                   u"catppuccin-latte"_s, u"catppuccin-macchiato"_s,
                                   u"catppuccin-mocha"_s, u"dracula"_s}));

        for (const ThemeLibrary::Theme &theme : themes) {
            // Light and dark set every role themselves, as every other
            // theme falls back on them.
            QStringList errors;
            const QVariantMap roles = library.resolve(theme.id, &errors);
            QVERIFY2(errors.isEmpty(), qPrintable(theme.id + u": "_s + errors.join(u", "_s)));
            for (const ThemeLibrary::Role &role : ThemeLibrary::roles()) {
                const QVariant value = roles.value(role.name);
                switch (role.kind) {
                case ThemeLibrary::Kind::Color:
                    QVERIFY2(value.value<QColor>().isValid(), qPrintable(theme.id + u" "_s + role.name));
                    break;
                case ThemeLibrary::Kind::ColorList:
                    QVERIFY2(!value.toList().isEmpty(), qPrintable(theme.id + u" "_s + role.name));
                    break;
                case ThemeLibrary::Kind::Number:
                    QVERIFY2(value.canConvert<double>(), qPrintable(theme.id + u" "_s + role.name));
                    break;
                }
            }
            if (theme.id == "light"_L1 || theme.id == "dark"_L1) {
                const QByteArray json = readAll(theme.path);
                for (const ThemeLibrary::Role &role : ThemeLibrary::roles())
                    QVERIFY2(json.contains('"' + role.name.toLatin1() + '"'), qPrintable(theme.id + u" "_s + role.name));
            }
        }
    }

    void readsTheirOwnPalette()
    {
        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR)});
        const QVariantMap mocha = library.resolve(u"catppuccin-mocha"_s, nullptr);
        QCOMPARE(color(mocha, u"background_elevated"_s), QColor(u"#1e1e2e"_s));
        QCOMPARE(color(mocha, u"primary_default"_s), QColor(u"#cba6f7"_s));
        // crust@0.6.
        QCOMPARE(color(mocha, u"dim"_s).rgb(), QColor(u"#11111b"_s).rgb());
        QCOMPARE(color(mocha, u"dim"_s).alpha(), 153);
        QCOMPARE(mocha.value(u"graph_lanes"_s).toList().first().value<QColor>(), QColor(u"#89b4fa"_s));
    }

    // Without inherits a theme builds on light or dark, after its own dark;
    // what it does not set comes from there.
    void inheritsLightOrDark()
    {
        write(u"mine.json"_s, "{\"name\": \"Mine\", \"dark\": true, "
                              "\"roles\": {\"primary_default\": \"#ff0000@0.5\"}}");
        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR), m_user.path()});
        QStringList errors;
        const QVariantMap mine = library.resolve(u"mine"_s, &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u", "_s)));
        QCOMPARE(color(mine, u"primary_default"_s).rgba(), qRgba(255, 0, 0, 128));
        const QVariantMap dark = library.resolve(u"dark"_s, nullptr);
        QCOMPARE(mine.value(u"background_default"_s), dark.value(u"background_default"_s));

        const QList<ThemeLibrary::Theme> themes = library.themes();
        const auto theme = std::find_if(themes.cbegin(), themes.cend(),
                                        [](const ThemeLibrary::Theme &theme) { return theme.id == "mine"_L1; });
        QVERIFY(theme->dark);
        QVERIFY(theme->user);
    }

    // A theme that only changes colors recolors the roles below it that
    // use them; #rrggbbaa has the opacity last, as in CSS.
    void recolorsTheThemeBelow()
    {
        write(u"pink-mocha.json"_s, "{\"name\": \"Pink Mocha\", \"inherits\": \"catppuccin-mocha\", "
                                    "\"colors\": {\"mauve\": \"#f5c2e780\"}}");
        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR), m_user.path()});
        QStringList errors;
        const QVariantMap roles = library.resolve(u"pink-mocha"_s, &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u", "_s)));
        QCOMPARE(color(roles, u"primary_default"_s), QColor(0xf5, 0xc2, 0xe7, 0x80));
        QCOMPARE(color(roles, u"syntax_keyword"_s), QColor(0xf5, 0xc2, 0xe7, 0x80));
        // And it is dark, as the theme below is.
        const QList<ThemeLibrary::Theme> themes = library.themes();
        const auto theme = std::find_if(themes.cbegin(), themes.cend(),
                                        [](const ThemeLibrary::Theme &theme) { return theme.id == "pink-mocha"_L1; });
        QVERIFY(theme->dark);
    }

    // A user's theme of a built-in one's id hides it and builds on it.
    void overridesABuiltInTheme()
    {
        write(u"dracula.json"_s, "{\"name\": \"Dracula, my way\", \"roles\": {\"primary_default\": \"pink\"}}");
        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR), m_user.path()});
        QStringList errors;
        const QVariantMap roles = library.resolve(u"dracula"_s, &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u", "_s)));
        QCOMPARE(color(roles, u"primary_default"_s), QColor(u"#ff79c6"_s));
        QCOMPARE(color(roles, u"background_elevated"_s), QColor(u"#282a36"_s));
        const QList<ThemeLibrary::Theme> themes = library.themes();
        QCOMPARE(std::count_if(themes.cbegin(), themes.cend(),
                               [](const ThemeLibrary::Theme &theme) { return theme.id == "dracula"_L1; }),
                 1);
    }

    // What a theme gets wrong is reported, and the role keeps the value of
    // the theme below.
    void reportsMistakes()
    {
        write(u"broken.json"_s, "{\"name\": \"Broken\", \"colors\": {\"odd\": \"blue\"}, "
                                "\"roles\": {\"primary_defualt\": \"#ff0000\", "
                                "\"text_default\": \"nothing\", "
                                "\"text_muted\": \"#12345\", "
                                "\"text_link\": \"odd\", "
                                "\"background_default\": \"#ffffff@2\", "
                                "\"graph_lanes\": \"#ff0000\", "
                                "\"shadow_strength\": \"0.5\"}}");
        write(u"loop-a.json"_s, "{\"name\": \"A\", \"inherits\": \"loop-b\"}");
        write(u"loop-b.json"_s, "{\"name\": \"B\", \"inherits\": \"loop-a\"}");
        write(u"orphan.json"_s, "{\"name\": \"Orphan\", \"inherits\": \"nowhere\"}");
        write(u"unnamed.json"_s, "{\"roles\": {}}");
        write(u"invalid.json"_s, "{\"name\": \"Invalid\",");

        ThemeLibrary library;
        library.load({QStringLiteral(IGITT_THEMES_DIR), m_user.path()});
        QCOMPARE(library.loadErrors().size(), 2);
        QVERIFY(!library.contains(u"unnamed"_s));
        QVERIFY(!library.contains(u"invalid"_s));

        QStringList errors;
        const QVariantMap roles = library.resolve(u"broken"_s, &errors);
        QCOMPARE(errors.size(), 7);
        QVERIFY(errors.filter(u"there is no role primary_defualt"_s).size() == 1);
        const QVariantMap light = library.resolve(u"light"_s, nullptr);
        for (const QString &role : {u"text_default"_s, u"text_muted"_s, u"text_link"_s, u"background_default"_s,
                                    u"graph_lanes"_s, u"shadow_strength"_s})
            QCOMPARE(roles.value(role), light.value(role));

        errors.clear();
        library.resolve(u"loop-a"_s, &errors);
        QVERIFY(!errors.filter(u"inherits itself"_s).isEmpty());

        errors.clear();
        library.resolve(u"orphan"_s, &errors);
        QVERIFY(!errors.filter(u"inherits nowhere, which is no theme"_s).isEmpty());
    }

private:
    QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    void write(const QString &name, const char *json)
    {
        QFile file(QDir(m_user.path()).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(json);
    }

    QTemporaryDir m_user;
};

QTEST_GUILESS_MAIN(TestThemes)
#include "tst_themes.moc"
