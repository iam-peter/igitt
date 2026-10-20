#include "MessageFormat.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &trailerLine()
{
    static const QRegularExpression re(QStringLiteral("^([A-Za-z0-9][A-Za-z0-9-]*):\\s*(.*)$"));
    return re;
}

const QRegularExpression &bulletLine()
{
    static const QRegularExpression re(QStringLiteral("^(\\s*)([-*+]|\\d+[.)])\\s+(.*)$"));
    return re;
}

bool isIndented(const QString &line)
{
    return line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
}

// Greedy fill; a word longer than the line gets a line of its own.
void fill(const QStringList &words, const QString &first, const QString &rest, int width, QStringList &out)
{
    QString line = first;
    bool hasWord = false;
    for (const QString &word : words) {
        if (hasWord && line.size() + 1 + word.size() > width) {
            out.append(line);
            line = rest + word;
        } else {
            line += (hasWord ? QStringLiteral(" ") : QString()) + word;
        }
        hasWord = true;
    }
    out.append(line);
}

QStringList words(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

} // namespace

MessageFormat::Parts MessageFormat::split(const QString &message)
{
    Parts parts;
    QStringList paragraphs = message.trimmed().split(QRegularExpression(QStringLiteral("\\n[ \\t]*\\n")));
    if (paragraphs.isEmpty())
        return parts;
    // The title is the first paragraph; git joins a wrapped one into a line.
    parts.title = words(paragraphs.takeFirst()).join(QLatin1Char(' '));

    if (!paragraphs.isEmpty()) {
        const QStringList lines = paragraphs.last().split(QLatin1Char('\n'));
        QList<Trailer> trailers;
        bool isTrailerBlock = true;
        for (const QString &line : lines) {
            const QRegularExpressionMatch m = trailerLine().match(line);
            if (m.hasMatch())
                trailers.append({m.captured(1), m.captured(2).trimmed()});
            else if (isIndented(line) && !trailers.isEmpty())
                trailers.last().value += QLatin1Char(' ') + line.trimmed(); // folded value
            else
                isTrailerBlock = false;
        }
        if (isTrailerBlock && !trailers.isEmpty()) {
            parts.trailers = trailers;
            paragraphs.removeLast();
        }
    }
    parts.body = paragraphs.join(QStringLiteral("\n\n"));
    return parts;
}

QString MessageFormat::join(const Parts &parts, int wrapWidth)
{
    QStringList blocks{parts.title.simplified()};
    QString body = parts.body;
    while (!body.isEmpty() && body.back().isSpace())
        body.chop(1);
    while (body.startsWith(QLatin1Char('\n')))
        body.remove(0, 1);
    if (!body.isEmpty())
        blocks.append(wrapWidth > 0 ? wrap(body, wrapWidth) : body);

    QStringList trailers;
    for (const Trailer &trailer : parts.trailers) {
        if (!trailer.key.trimmed().isEmpty() && !trailer.value.trimmed().isEmpty())
            trailers.append(trailer.key.trimmed() + QStringLiteral(": ") + trailer.value.trimmed());
    }
    if (!trailers.isEmpty())
        blocks.append(trailers.join(QLatin1Char('\n')));
    return blocks.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

QString MessageFormat::wrap(const QString &text, int width)
{
    if (width <= 0)
        return text;
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList out;
    for (qsizetype i = 0; i < lines.size();) {
        const QString &line = lines[i];
        if (line.trimmed().isEmpty()) {
            out.append(QString());
            ++i;
            continue;
        }
        if (const QRegularExpressionMatch bullet = bulletLine().match(line); bullet.hasMatch()) {
            const QString prefix = bullet.captured(1) + bullet.captured(2) + QLatin1Char(' ');
            QStringList item = words(bullet.captured(3));
            // Continuation lines are indented; anything else ends the item.
            for (++i; i < lines.size() && !lines[i].trimmed().isEmpty() && isIndented(lines[i])
                      && !bulletLine().match(lines[i]).hasMatch();
                 ++i)
                item += words(lines[i]);
            fill(item, prefix, QString(prefix.size(), QLatin1Char(' ')), width, out);
            continue;
        }
        if (isIndented(line)) {
            out.append(line); // preformatted, e.g. code or a log excerpt
            ++i;
            continue;
        }
        QStringList paragraph;
        for (; i < lines.size() && !lines[i].trimmed().isEmpty() && !isIndented(lines[i])
               && !bulletLine().match(lines[i]).hasMatch();
             ++i)
            paragraph += words(lines[i]);
        fill(paragraph, {}, {}, width, out);
    }
    return out.join(QLatin1Char('\n'));
}

int MessageFormat::overlongLines(const QString &text, int width)
{
    int n = 0;
    for (const QString &line : text.split(QLatin1Char('\n')))
        n += line.size() > width ? 1 : 0;
    return n;
}

QVariantMap MessageFormat::parse(const QString &message) const
{
    const Parts parts = split(message);
    QVariantList trailers;
    for (const Trailer &trailer : parts.trailers)
        trailers.append(QVariantMap{{QStringLiteral("key"), trailer.key}, {QStringLiteral("value"), trailer.value}});
    return {
        {QStringLiteral("title"), parts.title},
        {QStringLiteral("body"), parts.body},
        {QStringLiteral("trailers"), trailers},
    };
}

QString MessageFormat::compose(const QString &title, const QString &body, const QVariantList &trailers,
                               int wrapWidth) const
{
    Parts parts{title, body, {}};
    for (const QVariant &trailer : trailers) {
        const QVariantMap map = trailer.toMap();
        parts.trailers.append({map.value(QStringLiteral("key")).toString(),
                               map.value(QStringLiteral("value")).toString()});
    }
    return join(parts, wrapWidth);
}
