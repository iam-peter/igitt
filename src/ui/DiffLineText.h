#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QQuickItem>
#include <QRectF>
#include <QTextLayout>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

// One line of a diff or a file, as plain text: nothing in it is ever read
// as markup. Colors its syntax and marks the words that changed from a
// LineMarkup (see DiffModel's markup role), in colors for a light or a
// dark palette.
class DiffLineText : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // Offsets in `markup` count from here, past the diff's +/- marker.
    Q_PROPERTY(int markupOffset READ markupOffset WRITE setMarkupOffset NOTIFY markupChanged)
    Q_PROPERTY(QVariant markup READ markup WRITE setMarkup NOTIFY markupChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorsChanged)
    // Behind the changed words.
    Q_PROPERTY(QColor changeColor READ changeColor WRITE setChangeColor NOTIFY colorsChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY colorsChanged)
    Q_PROPERTY(bool highlight READ highlight WRITE setHighlight NOTIFY markupChanged)
    Q_PROPERTY(bool wordDiff READ wordDiff WRITE setWordDiff NOTIFY markupChanged)
    // What is drawn, for tests.
    Q_PROPERTY(int coloredSpans READ coloredSpans NOTIFY laidOut)
    Q_PROPERTY(int markedChanges READ markedChanges NOTIFY laidOut)

public:
    explicit DiffLineText(QQuickItem *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &text);
    int markupOffset() const { return m_markupOffset; }
    void setMarkupOffset(int offset);
    QVariant markup() const { return m_markup; }
    void setMarkup(const QVariant &markup);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor changeColor() const { return m_changeColor; }
    void setChangeColor(const QColor &color);
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    bool highlight() const { return m_highlight; }
    void setHighlight(bool highlight);
    bool wordDiff() const { return m_wordDiff; }
    void setWordDiff(bool wordDiff);
    int coloredSpans() const { return m_coloredSpans; }
    int markedChanges() const { return int(m_changeRects.size()); }

signals:
    void textChanged();
    void markupChanged();
    void fontChanged();
    void colorsChanged();
    void laidOut();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
    void invalidate();

    QString m_text;
    int m_markupOffset = 0;
    QVariant m_markup;
    QFont m_font;
    QColor m_color = Qt::black;
    QColor m_changeColor = Qt::transparent;
    bool m_dark = false;
    bool m_highlight = true;
    bool m_wordDiff = true;

    QTextLayout m_layout;
    QList<QRectF> m_changeRects;
    int m_coloredSpans = 0;
};
