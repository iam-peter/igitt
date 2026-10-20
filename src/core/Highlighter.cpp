#include "Highlighter.h"

#include <QFileInfo>
#include <QSet>

#include <optional>

#ifdef IGITT_HAVE_KSYNTAXHIGHLIGHTING
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#endif

class SyntaxHighlighter::Backend
{
public:
    virtual ~Backend() = default;
    virtual QString language() const = 0;
    virtual QList<StyleSpan> highlightLine(QStringView line) = 0;
    virtual void reset() = 0;
};

using namespace Qt::StringLiterals;

namespace {

using Backend = SyntaxHighlighter::Backend;

enum class Language { Cpp, Js, CMake, Python, Markdown };

std::optional<Language> languageFor(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName().toLower();
    if (name == u"cmakelists.txt")
        return Language::CMake;
    const QString suffix = info.suffix().toLower();
    static const QSet<QString> cpp{u"c"_s, u"cc"_s, u"cpp"_s, u"cxx"_s, u"c++"_s, u"h"_s, u"hh"_s,
                                   u"hpp"_s, u"hxx"_s, u"h++"_s, u"ipp"_s, u"inl"_s, u"tpp"_s,
                                   u"m"_s, u"mm"_s};
    static const QSet<QString> js{u"qml"_s, u"js"_s, u"mjs"_s, u"cjs"_s, u"jsx"_s, u"ts"_s, u"tsx"_s,
                                  u"json"_s};
    if (cpp.contains(suffix))
        return Language::Cpp;
    if (js.contains(suffix))
        return Language::Js;
    if (suffix == u"cmake")
        return Language::CMake;
    if (suffix == u"py" || suffix == u"pyw" || suffix == u"pyi")
        return Language::Python;
    if (suffix == u"md" || suffix == u"markdown")
        return Language::Markdown;
    return std::nullopt;
}

QSet<QString> words(std::initializer_list<const char *> list)
{
    QSet<QString> set;
    for (const char *w : list)
        set.insert(QString::fromLatin1(w));
    return set;
}

const QSet<QString> &cppKeywords()
{
    static const QSet<QString> set = words({
        "alignas", "alignof", "and", "asm", "auto", "break", "case", "catch", "class", "concept", "const",
        "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield",
        "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
        "not", "nullptr", "operator", "or", "override", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
        "union", "using", "virtual", "volatile", "while", "emit", "signals", "slots", "Q_SIGNALS", "Q_SLOTS",
        "Q_EMIT", "NULL",
    });
    return set;
}

const QSet<QString> &cppTypes()
{
    static const QSet<QString> set = words({
        "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short", "signed",
        "unsigned", "void", "wchar_t", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "qsizetype",
        "qint8", "qint16", "qint32", "qint64", "quint8", "quint16", "quint32", "quint64", "qreal", "uchar",
        "ushort", "uint", "ulong",
    });
    return set;
}

const QSet<QString> &jsKeywords()
{
    static const QSet<QString> set = words({
        "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "enum", "export", "extends", "false", "finally", "for", "from", "function", "if",
        "import", "in", "instanceof", "let", "new", "null", "of", "return", "static", "super", "switch", "this",
        "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with", "yield",
        // QML
        "property", "signal", "readonly", "required", "alias", "pragma", "component", "on", "interface",
        "type", "implements",
    });
    return set;
}

const QSet<QString> &jsTypes()
{
    // Not color, font, size and the like: as property names they are far
    // more common than as types.
    static const QSet<QString> set = words({
        "int", "real", "double", "bool", "string", "number", "boolean", "any", "unknown", "never",
    });
    return set;
}

const QSet<QString> &cmakeKeywords()
{
    // Compared in lower case: CMake commands ignore case.
    static const QSet<QString> set = words({
        "if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile", "function", "endfunction",
        "macro", "endmacro", "return", "break", "continue", "block", "endblock",
    });
    return set;
}

const QSet<QString> &cmakeArguments()
{
    static const QSet<QString> set = words({
        "AND", "OR", "NOT", "STREQUAL", "EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL", "MATCHES",
        "DEFINED", "EXISTS", "COMMAND", "TARGET", "TEST", "IN_LIST", "VERSION_LESS", "VERSION_GREATER",
        "VERSION_EQUAL", "VERSION_LESS_EQUAL", "VERSION_GREATER_EQUAL", "PUBLIC", "PRIVATE", "INTERFACE",
        "REQUIRED", "COMPONENTS", "OPTIONAL_COMPONENTS", "CONFIG", "QUIET", "OPTIONAL", "STATIC", "SHARED",
        "MODULE", "OBJECT", "IMPORTED", "ALIAS", "SOURCES", "PROPERTIES", "DESTINATION", "FILES", "DIRECTORY",
        "TARGETS", "RUNTIME", "LIBRARY", "ARCHIVE", "DEPENDS", "WORKING_DIRECTORY", "CACHE", "STRING", "BOOL",
        "PATH", "FILEPATH", "INTERNAL", "FORCE", "PARENT_SCOPE", "ON", "OFF", "TRUE", "FALSE", "YES", "NO",
        "NAMES", "PATHS", "HINTS", "NAME", "LANGUAGES", "VERSION", "ENVIRONMENT", "URI", "QML_FILES",
        "RESOURCES", "APPEND", "REMOVE_ITEM", "GLOB", "GLOB_RECURSE", "STATUS", "WARNING", "FATAL_ERROR",
        "SEND_ERROR", "AUTHOR_WARNING", "DEPRECATION",
    });
    return set;
}

const QSet<QString> &pythonKeywords()
{
    static const QSet<QString> set = words({
        "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
        "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "match",
        "case",
    });
    return set;
}

const QSet<QString> &pythonTypes()
{
    static const QSet<QString> set = words({
        "int", "float", "complex", "str", "bytes", "bytearray", "bool", "list", "dict", "set", "frozenset",
        "tuple", "object", "type", "range",
    });
    return set;
}

bool isIdentStart(QChar c, Language lang)
{
    return c.isLetter() || c == u'_' || (lang == Language::Js && c == u'$');
}

bool isIdentChar(QChar c, Language lang)
{
    return c.isLetterOrNumber() || c == u'_' || (lang == Language::Js && c == u'$');
}

// A lexer for the languages above, good enough to color a diff. What it
// carries from line to line is the construct the previous line left open.
class BuiltinBackend final : public Backend
{
public:
    explicit BuiltinBackend(Language lang)
        : m_lang(lang)
    {}

    QString language() const override
    {
        switch (m_lang) {
        case Language::Cpp: return QStringLiteral("C++");
        case Language::Js: return QStringLiteral("QML/JavaScript");
        case Language::CMake: return QStringLiteral("CMake");
        case Language::Python: return QStringLiteral("Python");
        case Language::Markdown: return QStringLiteral("Markdown");
        }
        return {};
    }

    void reset() override
    {
        m_open = Open::None;
        m_close.clear();
    }

    QList<StyleSpan> highlightLine(QStringView line) override
    {
        m_out.clear();
        if (m_lang == Language::Markdown)
            markdownLine(line);
        else
            codeLine(line);
        return std::exchange(m_out, {});
    }

private:
    enum class Open : quint8 {
        None,
        Comment, // ends at m_close
        String,  // ends at m_close
        Template, // a JavaScript template literal, with escapes
        Fence,   // Markdown fenced code, ends at a line starting with m_close
    };

    void mark(qsizetype start, qsizetype end, TokenStyle style)
    {
        if (end <= start || style == TokenStyle::Normal)
            return;
        if (!m_out.isEmpty() && m_out.last().style == style && m_out.last().start + m_out.last().length == start) {
            m_out.last().length += int(end - start);
            return;
        }
        m_out.append({int(start), int(end - start), style});
    }

    // Continues an open construct from the line before; returns where code
    // resumes, or -1 if the whole line belongs to it.
    qsizetype continueOpen(QStringView line)
    {
        switch (m_open) {
        case Open::None:
        case Open::Fence:
            return 0;
        case Open::Comment:
        case Open::String: {
            const TokenStyle style = m_open == Open::Comment ? TokenStyle::Comment : TokenStyle::String;
            const qsizetype end = line.indexOf(m_close);
            if (end < 0) {
                mark(0, line.size(), style);
                return -1;
            }
            const qsizetype resume = end + m_close.size();
            mark(0, resume, style);
            reset();
            return resume;
        }
        case Open::Template: {
            bool closed = false;
            const qsizetype end = scanQuoted(line, 0, u'`', &closed);
            mark(0, end, TokenStyle::String);
            if (!closed)
                return -1;
            reset();
            return end;
        }
        }
        return 0;
    }

    // Returns the index just past the closing `quote`, honoring backslash
    // escapes, or the line's length if the line ends first.
    static qsizetype scanQuoted(QStringView line, qsizetype from, QChar quote, bool *closed)
    {
        for (qsizetype i = from; i < line.size(); ++i) {
            if (line.at(i) == u'\\') {
                ++i;
                continue;
            }
            if (line.at(i) == quote) {
                *closed = true;
                return i + 1;
            }
        }
        *closed = false;
        return line.size();
    }

    // A string or comment that may end on a later line.
    qsizetype openUntil(QStringView line, qsizetype start, qsizetype from, const QString &close, Open kind)
    {
        const TokenStyle style = kind == Open::Comment ? TokenStyle::Comment : TokenStyle::String;
        const qsizetype end = line.indexOf(close, from);
        if (end < 0) {
            mark(start, line.size(), style);
            m_open = kind;
            m_close = close;
            return line.size();
        }
        mark(start, end + close.size(), style);
        return end + close.size();
    }

    static qsizetype skipSpaces(QStringView line, qsizetype i)
    {
        while (i < line.size() && line.at(i).isSpace())
            ++i;
        return i;
    }

    // "[[", "[=[", ... at `i`: the matching close, or empty.
    static QString bracketClose(QStringView line, qsizetype i)
    {
        if (i >= line.size() || line.at(i) != u'[')
            return {};
        qsizetype j = i + 1;
        while (j < line.size() && line.at(j) == u'=')
            ++j;
        if (j >= line.size() || line.at(j) != u'[')
            return {};
        return u']' + QString(j - i - 1, u'=') + u']';
    }

    TokenStyle classify(const QString &word, QStringView line, qsizetype end)
    {
        const qsizetype next = skipSpaces(line, end);
        const bool call = next < line.size() && line.at(next) == u'(';
        switch (m_lang) {
        case Language::Cpp:
            if (cppKeywords().contains(word))
                return TokenStyle::Keyword;
            if (cppTypes().contains(word)
                || ((word.endsWith(u"_t") && (word.startsWith(u"int") || word.startsWith(u"uint")))))
                return TokenStyle::Type;
            if (word.size() > 2 && word.startsWith(u"Q_") && word == word.toUpper())
                return TokenStyle::Preprocessor;
            break;
        case Language::Js:
            if (jsKeywords().contains(word))
                return TokenStyle::Keyword;
            if (jsTypes().contains(word) || word.at(0).isUpper())
                return TokenStyle::Type;
            break;
        case Language::CMake:
            if (cmakeKeywords().contains(word.toLower()))
                return TokenStyle::Keyword;
            if (call)
                return TokenStyle::Function;
            if (cmakeArguments().contains(word))
                return TokenStyle::Keyword;
            return TokenStyle::Normal;
        case Language::Python:
            if (m_previousWord == u"def")
                return TokenStyle::Function;
            if (m_previousWord == u"class")
                return TokenStyle::Type;
            if (pythonKeywords().contains(word))
                return TokenStyle::Keyword;
            if (pythonTypes().contains(word))
                return TokenStyle::Type;
            break;
        case Language::Markdown:
            break;
        }
        return call ? TokenStyle::Function : TokenStyle::Normal;
    }

    void codeLine(QStringView line)
    {
        qsizetype i = continueOpen(line);
        if (i < 0)
            return;
        m_previousWord.clear();
        const qsizetype n = line.size();

        // A preprocessor directive, a Python decorator.
        const qsizetype first = skipSpaces(line, i);
        if (i == 0 && first < n) {
            if (m_lang == Language::Cpp && line.at(first) == u'#') {
                qsizetype j = skipSpaces(line, first + 1);
                while (j < n && isIdentChar(line.at(j), m_lang))
                    ++j;
                mark(first, j, TokenStyle::Preprocessor);
                const QStringView directive = line.mid(first + 1, j - first - 1).trimmed();
                i = j;
                if (directive == u"include" || directive == u"import") {
                    const qsizetype open = skipSpaces(line, j);
                    if (open < n && line.at(open) == u'<') {
                        const qsizetype close = line.indexOf(u'>', open);
                        i = close < 0 ? n : close + 1;
                        mark(open, i, TokenStyle::String);
                    }
                }
            } else if (m_lang == Language::Python && line.at(first) == u'@') {
                qsizetype j = first + 1;
                while (j < n && (isIdentChar(line.at(j), m_lang) || line.at(j) == u'.'))
                    ++j;
                mark(first, j, TokenStyle::Preprocessor);
                i = j;
            }
        }

        while (i < n) {
            const QChar c = line.at(i);
            const QChar next = i + 1 < n ? line.at(i + 1) : QChar();
            if (c.isSpace()) {
                ++i;
                continue;
            }

            // Comments.
            const bool cStyle = m_lang == Language::Cpp || m_lang == Language::Js;
            if (cStyle && c == u'/' && next == u'/') {
                mark(i, n, TokenStyle::Comment);
                return;
            }
            if (cStyle && c == u'/' && next == u'*') {
                i = openUntil(line, i, i + 2, QStringLiteral("*/"), Open::Comment);
                continue;
            }
            if ((m_lang == Language::CMake || m_lang == Language::Python) && c == u'#') {
                const QString close = m_lang == Language::CMake ? bracketClose(line, i + 1) : QString();
                if (!close.isEmpty()) {
                    i = openUntil(line, i, i + 1 + close.size(), close, Open::Comment);
                    continue;
                }
                mark(i, n, TokenStyle::Comment);
                return;
            }

            // Strings.
            if (m_lang == Language::CMake && c == u'[') {
                const QString close = bracketClose(line, i);
                if (!close.isEmpty()) {
                    i = openUntil(line, i, i + close.size(), close, Open::String);
                    continue;
                }
            }
            if (m_lang == Language::CMake && c == u'$') {
                // ${VAR}, $ENV{VAR}, $<generator expression>
                qsizetype j = i + 1;
                while (j < n && line.at(j).isLetter())
                    ++j;
                if (j < n && (line.at(j) == u'{' || (j == i + 1 && line.at(j) == u'<'))) {
                    const qsizetype end = line.indexOf(line.at(j) == u'{' ? u'}' : u'>', j);
                    const qsizetype stop = end < 0 ? n : end + 1;
                    mark(i, stop, TokenStyle::Type);
                    i = stop;
                    continue;
                }
            }
            if (c == u'"' || (c == u'\'' && m_lang != Language::CMake) || (c == u'`' && m_lang == Language::Js)) {
                i = quoted(line, i, i);
                continue;
            }

            // Numbers.
            if (c.isDigit() || (c == u'.' && next.isDigit())) {
                qsizetype j = i + 1;
                while (j < n) {
                    const QChar d = line.at(j);
                    if (d.isLetterOrNumber() || d == u'_' || d == u'.' || (d == u'\'' && m_lang == Language::Cpp)) {
                        ++j;
                    } else if ((d == u'+' || d == u'-') && QStringView(u"eEpP").contains(line.at(j - 1))
                               && !line.mid(i, 2).startsWith(u"0x", Qt::CaseInsensitive)) {
                        ++j;
                    } else {
                        break;
                    }
                }
                mark(i, j, TokenStyle::Number);
                i = j;
                continue;
            }

            // Words.
            if (isIdentStart(c, m_lang)) {
                qsizetype j = i + 1;
                while (j < n && isIdentChar(line.at(j), m_lang))
                    ++j;
                const QString word = line.mid(i, j - i).toString();
                if (j < n && stringPrefix(word, line.at(j))) {
                    i = quoted(line, i, j);
                    continue;
                }
                mark(i, j, classify(word, line, j));
                m_previousWord = word;
                i = j;
                continue;
            }
            ++i;
        }
    }

    // Whether `word` directly before `quote` makes it a prefixed string
    // literal: u8"...", R"(...)", f'...'.
    bool stringPrefix(const QString &word, QChar quote) const
    {
        if (m_lang == Language::Cpp && quote == u'"') {
            static const QSet<QString> prefixes = words({"L", "u", "U", "u8", "R", "LR", "uR", "UR", "u8R"});
            return prefixes.contains(word);
        }
        if (m_lang == Language::Python && (quote == u'"' || quote == u'\'')) {
            static const QSet<QString> prefixes = words({"r", "b", "f", "u", "rb", "br", "fr", "rf"});
            return prefixes.contains(word.toLower());
        }
        return false;
    }

    // A string literal from `start`, its quote at `quote`; returns where it
    // ends.
    qsizetype quoted(QStringView line, qsizetype start, qsizetype quote)
    {
        const qsizetype n = line.size();
        const QChar q = line.at(quote);
        // C++ raw string: R"delimiter( ... )delimiter"
        if (m_lang == Language::Cpp && quote > start && line.at(quote - 1) == u'R') {
            const qsizetype paren = line.indexOf(u'(', quote);
            if (paren > 0) {
                const QString close = u')' + line.mid(quote + 1, paren - quote - 1).toString() + u'"';
                return openUntil(line, start, paren + 1, close, Open::String);
            }
        }
        // Python triple quotes.
        if (m_lang == Language::Python && quote + 2 < n && line.at(quote + 1) == q && line.at(quote + 2) == q) {
            const QString close(3, q);
            return openUntil(line, start, quote + 3, close, Open::String);
        }
        bool closed = false;
        const qsizetype end = scanQuoted(line, quote + 1, q, &closed);
        mark(start, end, TokenStyle::String);
        // Only template literals go on past the end of the line.
        if (!closed && q == u'`')
            m_open = Open::Template;
        return end;
    }

    void markdownLine(QStringView line)
    {
        const qsizetype n = line.size();
        const qsizetype first = skipSpaces(line, 0);
        const QStringView rest = line.mid(first);

        if (m_open == Open::Fence) {
            mark(0, n, TokenStyle::String);
            if (rest.startsWith(m_close))
                reset();
            return;
        }
        qsizetype i = continueOpen(line);
        if (i < 0)
            return;
        if (i == 0 && first <= 3) {
            if (rest.startsWith(u"```") || rest.startsWith(u"~~~")) {
                qsizetype j = 0;
                while (j < rest.size() && rest.at(j) == rest.at(0))
                    ++j;
                m_open = Open::Fence;
                m_close = rest.left(j).toString();
                mark(0, n, TokenStyle::String);
                return;
            }
            if (rest.startsWith(u'#')) {
                qsizetype level = 0;
                while (level < rest.size() && rest.at(level) == u'#')
                    ++level;
                if (level <= 6 && (level == rest.size() || rest.at(level).isSpace())) {
                    mark(0, n, TokenStyle::Heading);
                    return;
                }
            }
            if (rest.startsWith(u'>')) {
                mark(0, n, TokenStyle::Comment);
                return;
            }
        }
        // A list item's marker.
        if (i == 0 && first < n) {
            qsizetype j = first;
            if (QStringView(u"-*+").contains(line.at(j))) {
                ++j;
            } else {
                while (j < n && line.at(j).isDigit())
                    ++j;
                if (j > first && j < n && (line.at(j) == u'.' || line.at(j) == u')'))
                    ++j;
                else
                    j = first;
            }
            if (j > first && j < n && line.at(j) == u' ') {
                mark(first, j, TokenStyle::Keyword);
                i = j;
            }
        }

        while (i < n) {
            const QChar c = line.at(i);
            if (c == u'\\') {
                i += 2;
                continue;
            }
            if (c == u'`') {
                qsizetype j = i;
                while (j < n && line.at(j) == u'`')
                    ++j;
                const QString ticks(j - i, u'`');
                const qsizetype end = line.indexOf(ticks, j);
                if (end < 0) {
                    i = j;
                    continue;
                }
                mark(i, end + ticks.size(), TokenStyle::String);
                i = end + ticks.size();
                continue;
            }
            if (c == u'<' && line.mid(i).startsWith(u"<!--")) {
                i = openUntil(line, i, i + 4, QStringLiteral("-->"), Open::Comment);
                continue;
            }
            if ((c == u'*' || c == u'_') && i + 1 < n) {
                const qsizetype run = (line.at(i + 1) == c) ? 2 : 1;
                const QString marker(run, c);
                const qsizetype inner = i + run;
                const bool opens = inner < n && !line.at(inner).isSpace()
                                   && (c == u'*' || i == 0 || !line.at(i - 1).isLetterOrNumber());
                const qsizetype end = opens ? line.indexOf(marker, inner + 1) : -1;
                if (end > inner && !line.at(end - 1).isSpace()) {
                    mark(i, end + run, TokenStyle::Emphasis);
                    i = end + run;
                    continue;
                }
                i = inner;
                continue;
            }
            if (c == u']' && i + 1 < n && line.at(i + 1) == u'(') {
                const qsizetype end = line.indexOf(u')', i + 2);
                if (end > 0) {
                    mark(i + 2, end, TokenStyle::Type);
                    i = end + 1;
                    continue;
                }
            }
            ++i;
        }
    }

    Language m_lang;
    Open m_open = Open::None;
    QString m_close;
    QString m_previousWord;
    QList<StyleSpan> m_out;
};

#ifdef IGITT_HAVE_KSYNTAXHIGHLIGHTING

namespace KSH = KSyntaxHighlighting;

TokenStyle styleFor(KSH::Theme::TextStyle style)
{
    switch (style) {
    case KSH::Theme::Keyword:
    case KSH::Theme::ControlFlow:
        return TokenStyle::Keyword;
    case KSH::Theme::DataType:
    case KSH::Theme::BuiltIn:
    case KSH::Theme::Extension:
    case KSH::Theme::Attribute:
        return TokenStyle::Type;
    case KSH::Theme::Function:
        return TokenStyle::Function;
    case KSH::Theme::String:
    case KSH::Theme::VerbatimString:
    case KSH::Theme::SpecialString:
    case KSH::Theme::Char:
    case KSH::Theme::SpecialChar:
        return TokenStyle::String;
    case KSH::Theme::DecVal:
    case KSH::Theme::BaseN:
    case KSH::Theme::Float:
    case KSH::Theme::Constant:
        return TokenStyle::Number;
    case KSH::Theme::Comment:
    case KSH::Theme::Documentation:
    case KSH::Theme::CommentVar:
    case KSH::Theme::RegionMarker:
        return TokenStyle::Comment;
    case KSH::Theme::Preprocessor:
    case KSH::Theme::Import:
    case KSH::Theme::Annotation:
        return TokenStyle::Preprocessor;
    default:
        return TokenStyle::Normal;
    }
}

// Loading the definitions takes a while; each thread keeps its own
// repository, as KSyntaxHighlighting is not thread-safe.
KSH::Repository &definitions()
{
    thread_local KSH::Repository repository;
    return repository;
}

class KshBackend final : public Backend, private KSH::AbstractHighlighter
{
public:
    explicit KshBackend(const KSH::Definition &definition)
    {
        setDefinition(definition);
    }

    QString language() const override { return definition().name(); }

    void reset() override { m_state = {}; }

    QList<StyleSpan> highlightLine(QStringView line) override
    {
        m_out.clear();
        m_state = AbstractHighlighter::highlightLine(line, m_state);
        return std::exchange(m_out, {});
    }

private:
    void applyFormat(int offset, int length, const KSH::Format &format) override
    {
        const TokenStyle style = styleFor(format.textStyle());
        if (style == TokenStyle::Normal || length <= 0)
            return;
        if (!m_out.isEmpty() && m_out.last().style == style && m_out.last().start + m_out.last().length == offset)
            m_out.last().length += length;
        else
            m_out.append({offset, length, style});
    }

    KSH::State m_state;
    QList<StyleSpan> m_out;
};

#endif

} // namespace

SyntaxHighlighter::SyntaxHighlighter(const QString &path, Engine engine)
{
#ifdef IGITT_HAVE_KSYNTAXHIGHLIGHTING
    if (engine == Engine::Auto) {
        const KSH::Definition definition = definitions().definitionForFileName(path);
        if (definition.isValid()) {
            m_backend = std::make_unique<KshBackend>(definition);
            return;
        }
    }
#else
    Q_UNUSED(engine)
#endif
    if (const std::optional<Language> lang = languageFor(path))
        m_backend = std::make_unique<BuiltinBackend>(*lang);
}

SyntaxHighlighter::~SyntaxHighlighter() = default;

QString SyntaxHighlighter::language() const
{
    return m_backend ? m_backend->language() : QString();
}

QList<StyleSpan> SyntaxHighlighter::highlightLine(QStringView line)
{
    return m_backend ? m_backend->highlightLine(line) : QList<StyleSpan>();
}

void SyntaxHighlighter::reset()
{
    if (m_backend)
        m_backend->reset();
}

bool SyntaxHighlighter::hasKSyntaxHighlighting()
{
#ifdef IGITT_HAVE_KSYNTAXHIGHLIGHTING
    return true;
#else
    return false;
#endif
}
