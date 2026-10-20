#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>

// What a piece of source text is. The view gives each a color of its light
// or dark theme, so highlighting never depends on the palette.
enum class TokenStyle : quint8 {
    Normal,
    Keyword,
    Type,
    Function,
    String,
    Number,
    Comment,
    Preprocessor,
    Heading,  // Markdown
    Emphasis, // Markdown
};

struct StyleSpan
{
    int start = 0;
    int length = 0;
    TokenStyle style = TokenStyle::Normal;

    friend bool operator==(const StyleSpan &, const StyleSpan &) = default;
};

// Highlights a file one line at a time; each line continues where the
// previous one ended, inside a block comment or a multi-line string. Feed
// it the lines in order, and reset() it where the text jumps.
//
// It uses KSyntaxHighlighting when igitt was built with it, and otherwise
// a small built-in highlighter for C++, QML/JavaScript, CMake, Python and
// Markdown. Not thread-safe, but every thread may have its own.
class SyntaxHighlighter
{
public:
    enum class Engine { Auto, Builtin };

    explicit SyntaxHighlighter(const QString &path, Engine engine = Engine::Auto);
    ~SyntaxHighlighter();
    SyntaxHighlighter(const SyntaxHighlighter &) = delete;
    SyntaxHighlighter &operator=(const SyntaxHighlighter &) = delete;

    // Whether the language of `path` is known; if not, lines get no spans.
    bool isValid() const { return m_backend != nullptr; }
    QString language() const;

    // Spans in order and not overlapping; text between them is Normal.
    QList<StyleSpan> highlightLine(QStringView line);
    void reset();

    // Whether Engine::Auto can use KSyntaxHighlighting.
    static bool hasKSyntaxHighlighting();

    class Backend;

private:
    std::unique_ptr<Backend> m_backend;
};
