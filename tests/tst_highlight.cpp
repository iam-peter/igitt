#include "core/DiffMarkup.h"
#include "core/DiffParser.h"
#include "core/Highlighter.h"
#include "core/WordDiff.h"
#include "models/DiffModel.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QString styleName(TokenStyle style)
{
    switch (style) {
    case TokenStyle::Normal: return u"Normal"_s;
    case TokenStyle::Keyword: return u"Keyword"_s;
    case TokenStyle::Type: return u"Type"_s;
    case TokenStyle::Function: return u"Function"_s;
    case TokenStyle::String: return u"String"_s;
    case TokenStyle::Number: return u"Number"_s;
    case TokenStyle::Comment: return u"Comment"_s;
    case TokenStyle::Preprocessor: return u"Preprocessor"_s;
    case TokenStyle::Heading: return u"Heading"_s;
    case TokenStyle::Emphasis: return u"Emphasis"_s;
    }
    return {};
}

// "Style:text" for each span, which reads better in a failure than offsets.
QStringList describe(const QString &line, const QList<StyleSpan> &spans)
{
    QStringList out;
    for (const StyleSpan &span : spans)
        out.append(styleName(span.style) + u':' + line.mid(span.start, span.length));
    return out;
}

// Highlights `lines` in order with the built-in highlighter.
QList<QStringList> highlight(const QString &path, const QStringList &lines)
{
    SyntaxHighlighter highlighter(path, SyntaxHighlighter::Engine::Builtin);
    QList<QStringList> out;
    for (const QString &line : lines)
        out.append(describe(line, highlighter.highlightLine(line)));
    return out;
}

QStringList highlightOne(const QString &path, const QString &line)
{
    return highlight(path, {line}).first();
}

QStringList changedText(const QString &line, const QList<TextRange> &ranges)
{
    QStringList out;
    for (const TextRange &range : ranges)
        out.append(line.mid(range.start, range.length));
    return out;
}

} // namespace

class TestHighlight : public QObject
{
    Q_OBJECT

private slots:
    void languages()
    {
        using Engine = SyntaxHighlighter::Engine;
        QCOMPARE(SyntaxHighlighter(u"src/main.cpp"_s, Engine::Builtin).language(), u"C++"_s);
        QCOMPARE(SyntaxHighlighter(u"src/Repository.h"_s, Engine::Builtin).language(), u"C++"_s);
        QCOMPARE(SyntaxHighlighter(u"src/qml/Main.qml"_s, Engine::Builtin).language(), u"QML/JavaScript"_s);
        QCOMPARE(SyntaxHighlighter(u"CMakeLists.txt"_s, Engine::Builtin).language(), u"CMake"_s);
        QCOMPARE(SyntaxHighlighter(u"cmake/Find.cmake"_s, Engine::Builtin).language(), u"CMake"_s);
        QCOMPARE(SyntaxHighlighter(u"tool.py"_s, Engine::Builtin).language(), u"Python"_s);
        QCOMPARE(SyntaxHighlighter(u"README.md"_s, Engine::Builtin).language(), u"Markdown"_s);

        SyntaxHighlighter unknown(u"notes.txt"_s, Engine::Builtin);
        QVERIFY(!unknown.isValid());
        QVERIFY(unknown.highlightLine(u"int x = 1;").isEmpty());
        // Whichever engine is built in knows C++.
        QVERIFY(SyntaxHighlighter(u"main.cpp"_s).isValid());
    }

    void cpp()
    {
        QCOMPARE(highlightOne(u"a.cpp"_s, u"int main() { return 42; } // done"_s),
                 (QStringList{u"Type:int"_s, u"Function:main"_s, u"Keyword:return"_s, u"Number:42"_s,
                              u"Comment:// done"_s}));
        QCOMPARE(highlightOne(u"a.cpp"_s, u"#include <QString>"_s),
                 (QStringList{u"Preprocessor:#include"_s, u"String:<QString>"_s}));
        QCOMPARE(highlightOne(u"a.cpp"_s, u"  #  define X 0x1F"_s),
                 (QStringList{u"Preprocessor:#  define"_s, u"Number:0x1F"_s}));
        QCOMPARE(highlightOne(u"a.cpp"_s, u"auto s = \"a\\\"b\" + 'c';"_s),
                 (QStringList{u"Keyword:auto"_s, u"String:\"a\\\"b\""_s, u"String:'c'"_s}));
        QCOMPARE(highlightOne(u"a.cpp"_s, u"const double d = 1.5e-3;"_s),
                 (QStringList{u"Keyword:const"_s, u"Type:double"_s, u"Number:1.5e-3"_s}));
        QCOMPARE(highlightOne(u"a.cpp"_s, u"quint64 n = 1'000; Q_OBJECT"_s),
                 (QStringList{u"Type:quint64"_s, u"Number:1'000"_s, u"Preprocessor:Q_OBJECT"_s}));
        // An identifier containing a keyword is not one.
        QCOMPARE(highlightOne(u"a.cpp"_s, u"returned = interval;"_s), QStringList());
    }

    void cppAcrossLines()
    {
        QCOMPARE(highlight(u"a.cpp"_s, {u"x = 1; /* start"_s, u"still comment"_s, u"end */ return"_s}),
                 (QList<QStringList>{{u"Number:1"_s, u"Comment:/* start"_s},
                                     {u"Comment:still comment"_s},
                                     {u"Comment:end */"_s, u"Keyword:return"_s}}));
        QCOMPARE(highlight(u"a.cpp"_s, {u"auto s = R\"x(a\""_s, u"b)\" )x\" + 1;"_s}),
                 (QList<QStringList>{{u"Keyword:auto"_s, u"String:R\"x(a\""_s},
                                     {u"String:b)\" )x\""_s, u"Number:1"_s}}));

        // reset() forgets what was open.
        SyntaxHighlighter highlighter(u"a.cpp"_s, SyntaxHighlighter::Engine::Builtin);
        highlighter.highlightLine(u"/* open");
        highlighter.reset();
        QCOMPARE(describe(u"return 1;"_s, highlighter.highlightLine(u"return 1;")),
                 (QStringList{u"Keyword:return"_s, u"Number:1"_s}));
    }

    void qml()
    {
        QCOMPARE(highlightOne(u"Main.qml"_s, u"    property int count: 0"_s),
                 (QStringList{u"Keyword:property"_s, u"Type:int"_s, u"Number:0"_s}));
        QCOMPARE(highlightOne(u"Main.qml"_s, u"Rectangle { color: \"red\" } // box"_s),
                 (QStringList{u"Type:Rectangle"_s, u"String:\"red\""_s, u"Comment:// box"_s}));
        QCOMPARE(highlightOne(u"Main.qml"_s, u"onClicked: view.toggle($x)"_s),
                 (QStringList{u"Function:toggle"_s}));
        QCOMPARE(highlight(u"a.js"_s, {u"const s = `a ${x}"_s, u"b` + 1"_s}),
                 (QList<QStringList>{{u"Keyword:const"_s, u"String:`a ${x}"_s}, {u"String:b`"_s, u"Number:1"_s}}));
    }

    void cmake()
    {
        QCOMPARE(highlightOne(u"CMakeLists.txt"_s, u"add_library(foo STATIC ${SOURCES}) # lib"_s),
                 (QStringList{u"Function:add_library"_s, u"Keyword:STATIC"_s, u"Type:${SOURCES}"_s,
                              u"Comment:# lib"_s}));
        QCOMPARE(highlightOne(u"x.cmake"_s, u"IF(WIN32) message(\"$<CONFIG>\")"_s),
                 (QStringList{u"Keyword:IF"_s, u"Function:message"_s, u"String:\"$<CONFIG>\""_s}));
        QCOMPARE(highlight(u"CMakeLists.txt"_s, {u"#[[ a"_s, u"b ]] set(x)"_s}),
                 (QList<QStringList>{{u"Comment:#[[ a"_s}, {u"Comment:b ]]"_s, u"Function:set"_s}}));
        QCOMPARE(highlight(u"CMakeLists.txt"_s, {u"set(x [=[ a"_s, u"]] ]=])"_s}),
                 (QList<QStringList>{{u"Function:set"_s, u"String:[=[ a"_s}, {u"String:]] ]=]"_s}}));
    }

    void python()
    {
        QCOMPARE(highlightOne(u"tool.py"_s, u"@dataclass.wrap"_s), (QStringList{u"Preprocessor:@dataclass.wrap"_s}));
        QCOMPARE(highlightOne(u"tool.py"_s, u"def run(self, n=3):"_s),
                 (QStringList{u"Keyword:def"_s, u"Function:run"_s, u"Number:3"_s}));
        QCOMPARE(highlightOne(u"tool.py"_s, u"class Tool(object):"_s),
                 (QStringList{u"Keyword:class"_s, u"Type:Tool"_s, u"Type:object"_s}));
        QCOMPARE(highlightOne(u"tool.py"_s, u"x = f\"hi {y}\"  # note"_s),
                 (QStringList{u"String:f\"hi {y}\""_s, u"Comment:# note"_s}));
        QCOMPARE(highlight(u"tool.py"_s, {u"s = \"\"\"doc"_s, u"more\"\"\" if None else 1"_s}),
                 (QList<QStringList>{{u"String:\"\"\"doc"_s},
                                     {u"String:more\"\"\""_s, u"Keyword:if"_s, u"Keyword:None"_s,
                                      u"Keyword:else"_s, u"Number:1"_s}}));
    }

    void markdown()
    {
        QCOMPARE(highlightOne(u"README.md"_s, u"## Building"_s), (QStringList{u"Heading:## Building"_s}));
        QCOMPARE(highlightOne(u"README.md"_s, u"#hashtag"_s), QStringList());
        QCOMPARE(highlightOne(u"README.md"_s, u"Use `cmake --build` and **care**, *not* a_b_c"_s),
                 (QStringList{u"String:`cmake --build`"_s, u"Emphasis:**care**"_s, u"Emphasis:*not*"_s}));
        QCOMPARE(highlightOne(u"README.md"_s, u"- see [docs](https://example.com)"_s),
                 (QStringList{u"Keyword:-"_s, u"Type:https://example.com"_s}));
        QCOMPARE(highlightOne(u"README.md"_s, u"> quoted"_s), (QStringList{u"Comment:> quoted"_s}));
        QCOMPARE(highlight(u"README.md"_s, {u"```cpp"_s, u"# not a heading"_s, u"```"_s, u"# Heading"_s}),
                 (QList<QStringList>{{u"String:```cpp"_s}, {u"String:# not a heading"_s}, {u"String:```"_s},
                                     {u"Heading:# Heading"_s}}));
    }

    void wordDiff()
    {
        WordChanges changes = diffWords(u"int x = 1;", u"int y = 1;");
        QCOMPARE(changes.before, (QList<TextRange>{{4, 1}}));
        QCOMPARE(changes.after, (QList<TextRange>{{4, 1}}));

        changes = diffWords(u"line 4", u"line 4 changed");
        QVERIFY(changes.before.isEmpty());
        QCOMPARE(changes.after, (QList<TextRange>{{6, 8}}));

        // Words only white space apart are marked as one.
        const QString after = u"return a + b;"_s;
        changes = diffWords(u"return a;", after);
        QCOMPARE(changedText(after, changes.after), QStringList{u" + b"_s});

        // Anything else between keeps them apart.
        changes = diffWords(u"call(b, c)", u"call(d, e)");
        QCOMPARE(changedText(u"call(b, c)"_s, changes.before), (QStringList{u"b"_s, u"c"_s}));
        QCOMPARE(changedText(u"call(d, e)"_s, changes.after), (QStringList{u"d"_s, u"e"_s}));

        // Indentation alone.
        changes = diffWords(u"    x();", u"x();");
        QCOMPARE(changes.before, (QList<TextRange>{{0, 4}}));
        QVERIFY(changes.after.isEmpty());

        // Lines with little in common, or nothing at all, stay unmarked.
        changes = diffWords(u"return x;", u"int y = z;");
        QVERIFY(changes.before.isEmpty() && changes.after.isEmpty());
        changes = diffWords(u"", u"something new");
        QVERIFY(changes.before.isEmpty() && changes.after.isEmpty());
        changes = diffWords(u"same", u"same");
        QVERIFY(changes.before.isEmpty() && changes.after.isEmpty());

        // Too long to compare.
        QString many;
        QString others;
        for (int i = 0; i < 400; ++i) {
            many += u"a%1 "_s.arg(i);
            others += u"b%1 "_s.arg(i);
        }
        changes = diffWords(u"x " + many + u"y", u"x " + others + u"y");
        QVERIFY(changes.before.isEmpty() && changes.after.isEmpty());
    }

    void pairing()
    {
        const QList<FileDiff> files = parseDiff("diff --git a/f b/f\n--- a/f\n+++ b/f\n"
                                                "@@ -1,6 +1,8 @@\n-a\n-b\n+A\n+B\n c\n-d\n+D\n+E\n e\n+f\n g\n");
        QCOMPARE(files.size(), 1);
        QCOMPARE(pairChangedLines(files[0].hunks[0].lines),
                 (QList<std::pair<int, int>>{{0, 2}, {1, 3}, {5, 6}}));

        // The no-newline marker sits between the runs.
        const QList<FileDiff> noNewline = parseDiff("diff --git a/f b/f\n--- a/f\n+++ b/f\n@@ -1 +1 @@\n"
                                                    "-x\n\\ No newline at end of file\n+y\n"
                                                    "\\ No newline at end of file\n");
        QCOMPARE(pairChangedLines(noNewline[0].hunks[0].lines), (QList<std::pair<int, int>>{{0, 2}}));

        // Added lines before removed ones are not an edit of them.
        const QList<FileDiff> reversed = parseDiff("diff --git a/f b/f\n--- a/f\n+++ b/f\n@@ -1 +1 @@\n+y\n-x\n");
        QVERIFY(pairChangedLines(reversed[0].hunks[0].lines).isEmpty());
    }

    void diffMarkup()
    {
        const QList<FileDiff> files = parseDiff("diff --git a/a.cpp b/a.cpp\n--- a/a.cpp\n+++ b/a.cpp\n"
                                                "@@ -1,2 +1,2 @@\n \tint x;\n-int y = 1;\n+int y = 2;\n"
                                                "diff --git a/n.txt b/n.txt\n--- a/n.txt\n+++ b/n.txt\n"
                                                "@@ -1 +1 @@\n-old text\n+new text\n");
        const QList<FileMarkup> markup = markupDiff(files);
        QCOMPARE(markup.size(), 2);
        const QList<LineMarkup> &lines = markup[0].hunks[0];
        QCOMPARE(lines.size(), 3);
        // Offsets are into the line with its tab expanded.
        QVERIFY(!lines[0].syntax.isEmpty());
        QCOMPARE(lines[0].syntax.first().start, 4);
        QCOMPARE(lines[0].syntax.first().length, 3);
        QVERIFY(lines[0].changes.isEmpty());
        QCOMPARE(lines[1].changes, (QList<TextRange>{{8, 1}}));
        QCOMPARE(lines[2].changes, (QList<TextRange>{{8, 1}}));
        // An unknown language still gets its changed words.
        const QList<LineMarkup> &text = markup[1].hunks[0];
        QVERIFY(text[0].syntax.isEmpty());
        QCOMPARE(text[0].changes, (QList<TextRange>{{0, 3}}));
        QCOMPARE(text[1].changes, (QList<TextRange>{{0, 3}}));
    }

    void limits()
    {
        MarkupLimits limits;
        limits.highlightLines = 2;
        limits.wordDiffLines = 1;
        limits.lineLength = 40;
        const QByteArray longLine(50, 'x');
        const QList<FileDiff> files = parseDiff("diff --git a/a.cpp b/a.cpp\n--- a/a.cpp\n+++ b/a.cpp\n"
                                                "@@ -1,3 +1,3 @@\n-int a;\n-int b;\n-int c;\n+int A;\n+int B;\n+int C;\n");
        const QList<LineMarkup> lines = markupDiff(files, limits).first().hunks.first();
        QVERIFY(!lines[0].syntax.isEmpty());
        QVERIFY(!lines[1].syntax.isEmpty());
        for (int l = 2; l < 6; ++l)
            QVERIFY2(lines[l].syntax.isEmpty(), qPrintable(QString::number(l)));
        QVERIFY(!lines[0].changes.isEmpty() && !lines[3].changes.isEmpty());
        QVERIFY(lines[1].changes.isEmpty() && lines[4].changes.isEmpty());

        // Over-long lines are skipped, and what follows them is still colored.
        const QList<LineMarkup> content = markupContent(u"a.cpp"_s,
                                                        {QString::fromLatin1(longLine), u"int x;"_s}, limits);
        QCOMPARE(content.size(), 2);
        QVERIFY(content[0].syntax.isEmpty());
        QVERIFY(!content[1].syntax.isEmpty());
        QVERIFY(markupContent(u"notes.txt"_s, {u"int x;"_s}).isEmpty());
    }

    void modelRole()
    {
        const QList<FileDiff> files = parseDiff("diff --git a/a.cpp b/a.cpp\n--- a/a.cpp\n+++ b/a.cpp\n"
                                                "@@ -1 +1 @@\n-return 1;\n+return 2;\n");
        DiffModel model;
        model.setFiles(files, markupDiff(files));
        // File header, hunk header, the removed and the added line.
        QCOMPARE(model.rowCount(), 4);
        const LineMarkup added = model.data(model.index(3), DiffModel::MarkupRole).value<LineMarkup>();
        QCOMPARE(model.data(model.index(3), DiffModel::TextRole).toString(), u"return 2;"_s);
        QVERIFY(!added.syntax.isEmpty());
        QCOMPARE(added.changes, (QList<TextRange>{{7, 1}}));
        QVERIFY(model.data(model.index(0), DiffModel::MarkupRole).value<LineMarkup>().isEmpty());

        // Without markup, rows just have none.
        model.setFiles(files);
        QVERIFY(model.data(model.index(3), DiffModel::MarkupRole).value<LineMarkup>().isEmpty());

        const QStringList lines{u"# Title"_s, u"text"_s};
        model.setContent(u"README.md"_s, lines, false, markupContent(u"README.md"_s, lines));
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.data(model.index(0), DiffModel::MarkupRole).value<LineMarkup>().syntax.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHighlight)
#include "tst_highlight.moc"
