#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

// Commit messages as title, body and trailers (`Key: value` lines in one
// block at the end, as `git interpret-trailers` sees them).
class MessageFormat : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    struct Trailer
    {
        QString key;
        QString value;
        bool operator==(const Trailer &) const = default;
    };
    struct Parts
    {
        QString title;
        QString body;
        QList<Trailer> trailers;
    };

    using QObject::QObject;

    static Parts split(const QString &message);
    // Title, blank line, body, blank line, one contiguous trailer block;
    // empty parts and trailers without a value are left out. The body is
    // wrapped at `wrapWidth` unless it is 0.
    static QString join(const Parts &parts, int wrapWidth = 0);
    // Fills paragraphs to `width` columns. Bullets ("- ", "* ", "1. ") get
    // a hanging indent; indented lines, like code, are left alone, and so
    // are words longer than a line, like URLs.
    static QString wrap(const QString &text, int width);
    static int overlongLines(const QString &text, int width);

    // For QML: {title, body, trailers: [{key, value}]}.
    Q_INVOKABLE QVariantMap parse(const QString &message) const;
    Q_INVOKABLE QString compose(const QString &title, const QString &body, const QVariantList &trailers,
                                int wrapWidth) const;
    Q_INVOKABLE QString wrapText(const QString &text, int width) const { return wrap(text, width); }
    Q_INVOKABLE int countOverlongLines(const QString &text, int width) const { return overlongLines(text, width); }
};
