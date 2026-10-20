#include "DiffLineText.h"

#include "core/DiffMarkup.h"

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QTextCharFormat>

namespace {

// After GitHub's light and dark themes; readable on the added and removed
// line backgrounds of either.
QColor styleColor(TokenStyle style, bool dark)
{
    switch (style) {
    case TokenStyle::Keyword: return QColor(dark ? 0xff7b72 : 0xcf222e);
    case TokenStyle::Type: return QColor(dark ? 0xffa657 : 0x953800);
    case TokenStyle::Function: return QColor(dark ? 0xd2a8ff : 0x8250df);
    case TokenStyle::String: return QColor(dark ? 0xa5d6ff : 0x0a3069);
    case TokenStyle::Number: return QColor(dark ? 0x79c0ff : 0x0550ae);
    case TokenStyle::Comment: return QColor(dark ? 0x8b949e : 0x6e7781);
    case TokenStyle::Preprocessor: return QColor(dark ? 0x7ee787 : 0x116329);
    case TokenStyle::Heading: return QColor(dark ? 0x79c0ff : 0x0550ae);
    case TokenStyle::Emphasis:
    case TokenStyle::Normal:
        break;
    }
    return {};
}

} // namespace

DiffLineText::DiffLineText(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    m_layout.setTextOption(option);
}

void DiffLineText::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    invalidate();
    emit textChanged();
}

void DiffLineText::setMarkupOffset(int offset)
{
    if (m_markupOffset == offset)
        return;
    m_markupOffset = offset;
    invalidate();
    emit markupChanged();
}

void DiffLineText::setMarkup(const QVariant &markup)
{
    m_markup = markup;
    invalidate();
    emit markupChanged();
}

void DiffLineText::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    invalidate();
    emit fontChanged();
}

void DiffLineText::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    update();
    emit colorsChanged();
}

void DiffLineText::setChangeColor(const QColor &color)
{
    if (m_changeColor == color)
        return;
    m_changeColor = color;
    update();
    emit colorsChanged();
}

void DiffLineText::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    invalidate();
    emit colorsChanged();
}

void DiffLineText::setHighlight(bool highlight)
{
    if (m_highlight == highlight)
        return;
    m_highlight = highlight;
    invalidate();
    emit markupChanged();
}

void DiffLineText::setWordDiff(bool wordDiff)
{
    if (m_wordDiff == wordDiff)
        return;
    m_wordDiff = wordDiff;
    invalidate();
    emit markupChanged();
}

void DiffLineText::invalidate()
{
    // Laid out once per frame, however many properties a delegate sets.
    polish();
}

void DiffLineText::updatePolish()
{
    const LineMarkup markup = m_markup.value<LineMarkup>();
    const int length = int(m_text.size());
    // A range of the markup, clamped to the text.
    const auto clamp = [this, length](int start, int count) -> std::pair<int, int> {
        const int from = std::clamp(start + m_markupOffset, 0, length);
        return {from, std::clamp(start + m_markupOffset + count, from, length) - from};
    };

    QList<QTextLayout::FormatRange> formats;
    if (m_highlight) {
        for (const StyleSpan &span : markup.syntax) {
            const auto [start, count] = clamp(span.start, span.length);
            if (count == 0)
                continue;
            QTextCharFormat format;
            if (const QColor color = styleColor(span.style, m_dark); color.isValid())
                format.setForeground(color);
            if (span.style == TokenStyle::Heading)
                format.setFontWeight(QFont::Bold);
            if (span.style == TokenStyle::Emphasis)
                format.setFontItalic(true);
            formats.append({start, count, format});
        }
    }
    m_coloredSpans = int(formats.size());

    m_layout.clearLayout();
    m_layout.setText(m_text);
    m_layout.setFont(m_font);
    m_layout.setFormats(formats);
    m_layout.beginLayout();
    QTextLine line = m_layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(1e6); // no wrapping; the list scrolls sideways
        line.setPosition({0, 0});
    }
    m_layout.endLayout();

    m_changeRects.clear();
    if (m_wordDiff && line.isValid()) {
        for (const TextRange &change : markup.changes) {
            const auto [start, count] = clamp(change.start, change.length);
            if (count == 0)
                continue;
            const qreal x1 = line.cursorToX(start);
            const qreal x2 = line.cursorToX(start + count);
            m_changeRects.append(QRectF(x1, 0, x2 - x1, line.height()));
        }
    }

    setImplicitSize(line.isValid() ? line.naturalTextWidth() : 0,
                    line.isValid() ? line.height() : QFontMetricsF(m_font).height());
    update();
    emit laidOut();
}

QSGNode *DiffLineText::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old ? old : new QSGNode;
    while (QSGNode *child = root->firstChild()) {
        root->removeChildNode(child);
        delete child;
    }
    for (const QRectF &rect : std::as_const(m_changeRects)) {
        QSGRectangleNode *mark = window()->createRectangleNode();
        mark->setRect(rect);
        mark->setColor(m_changeColor);
        root->appendChildNode(mark);
    }
    if (!m_text.isEmpty()) {
        QSGTextNode *text = window()->createTextNode();
        text->setColor(m_color);
        if (QQuickWindow::textRenderType() == QQuickWindow::NativeTextRendering)
            text->setRenderType(QSGTextNode::NativeRendering);
        text->addTextLayout({0, 0}, &m_layout);
        root->appendChildNode(text);
    }
    return root;
}
