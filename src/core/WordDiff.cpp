#include "WordDiff.h"

#include <QHashFunctions>

#include <algorithm>
#include <vector>

namespace {

// Past this many differing words per side, comparing costs more than it
// helps: such lines are rewritten, not edited.
constexpr qsizetype MaxTokens = 300;

enum class TokenKind { Word, Space, Other };

TokenKind kindOf(QChar c)
{
    if (c.isLetterOrNumber() || c == u'_')
        return TokenKind::Word;
    return c.isSpace() ? TokenKind::Space : TokenKind::Other;
}

// Words, runs of white space, and single other characters.
QList<TextRange> tokenize(QStringView text)
{
    QList<TextRange> tokens;
    qsizetype i = 0;
    while (i < text.size()) {
        const TokenKind kind = kindOf(text.at(i));
        qsizetype j = i + 1;
        if (kind != TokenKind::Other) {
            while (j < text.size() && kindOf(text.at(j)) == kind)
                ++j;
        }
        tokens.append({int(i), int(j - i)});
        i = j;
    }
    return tokens;
}

QStringView tokenText(QStringView text, const TextRange &token)
{
    return text.mid(token.start, token.length);
}

bool isSpace(QStringView text, const TextRange &token)
{
    return text.at(token.start).isSpace();
}

int nonSpaceLength(QStringView text)
{
    return int(std::count_if(text.cbegin(), text.cend(), [](QChar c) { return !c.isSpace(); }));
}

// Joins unmatched tokens into ranges; ranges only white space apart become
// one, which reads better than a mark per word.
QList<TextRange> changedRanges(QStringView text, const QList<TextRange> &tokens, const std::vector<bool> &matched)
{
    QList<TextRange> ranges;
    bool join = false; // only matched white space since the last range
    for (qsizetype t = 0; t < tokens.size(); ++t) {
        const TextRange &token = tokens[t];
        if (matched[t]) {
            join = join && isSpace(text, token);
            continue;
        }
        if (join)
            ranges.last().length = token.start + token.length - ranges.last().start;
        else
            ranges.append(token);
        join = true;
    }
    return ranges;
}

} // namespace

WordChanges diffWords(QStringView before, QStringView after)
{
    const QList<TextRange> a = tokenize(before);
    const QList<TextRange> b = tokenize(after);
    std::vector<bool> matchedA(a.size(), false);
    std::vector<bool> matchedB(b.size(), false);

    // Common tokens at either end, then the longest common subsequence of
    // what is left between.
    qsizetype prefix = 0;
    while (prefix < a.size() && prefix < b.size() && tokenText(before, a[prefix]) == tokenText(after, b[prefix])) {
        matchedA[prefix] = matchedB[prefix] = true;
        ++prefix;
    }
    qsizetype suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix
           && tokenText(before, a[a.size() - 1 - suffix]) == tokenText(after, b[b.size() - 1 - suffix])) {
        matchedA[a.size() - 1 - suffix] = matchedB[b.size() - 1 - suffix] = true;
        ++suffix;
    }
    const qsizetype m = a.size() - prefix - suffix;
    const qsizetype n = b.size() - prefix - suffix;
    if (m == 0 && n == 0)
        return {};
    if (m > MaxTokens || n > MaxTokens)
        return {};

    if (m > 0 && n > 0) {
        // Hashes first, so most comparisons are of two numbers.
        std::vector<size_t> hashA(m);
        std::vector<size_t> hashB(n);
        for (qsizetype i = 0; i < m; ++i)
            hashA[i] = qHash(tokenText(before, a[prefix + i]));
        for (qsizetype j = 0; j < n; ++j)
            hashB[j] = qHash(tokenText(after, b[prefix + j]));
        const auto same = [&](qsizetype i, qsizetype j) {
            return hashA[i] == hashB[j] && tokenText(before, a[prefix + i]) == tokenText(after, b[prefix + j]);
        };

        // lcs[i][j]: common length of the middle tokens from i and from j on.
        std::vector<quint16> lcs((m + 1) * (n + 1), 0);
        const auto at = [n](qsizetype i, qsizetype j) { return i * (n + 1) + j; };
        for (qsizetype i = m - 1; i >= 0; --i) {
            for (qsizetype j = n - 1; j >= 0; --j) {
                if (same(i, j))
                    lcs[at(i, j)] = lcs[at(i + 1, j + 1)] + 1;
                else
                    lcs[at(i, j)] = std::max(lcs[at(i + 1, j)], lcs[at(i, j + 1)]);
            }
        }
        qsizetype i = 0;
        qsizetype j = 0;
        while (i < m && j < n) {
            if (same(i, j)) {
                matchedA[prefix + i] = matchedB[prefix + j] = true;
                ++i;
                ++j;
            } else if (lcs[at(i + 1, j)] >= lcs[at(i, j + 1)]) {
                ++i;
            } else {
                ++j;
            }
        }
    }

    // Lines sharing little more than punctuation are better left whole.
    int common = 0;
    for (qsizetype t = 0; t < a.size(); ++t) {
        if (matchedA[t])
            common += nonSpaceLength(tokenText(before, a[t]));
    }
    // Nothing in common at all, as for text replacing a blank line, is no
    // edit either; a change of white space only is.
    const int lengthA = nonSpaceLength(before);
    const int lengthB = nonSpaceLength(after);
    if (common * 2 < std::min(lengthA, lengthB) || (common == 0 && lengthA + lengthB > 0))
        return {};

    return {changedRanges(before, a, matchedA), changedRanges(after, b, matchedB)};
}

QList<std::pair<int, int>> pairChangedLines(const QList<DiffLine> &lines)
{
    QList<std::pair<int, int>> pairs;
    const int count = int(lines.size());
    int i = 0;
    while (i < count) {
        if (lines[i].kind != DiffLine::Removed) {
            ++i;
            continue;
        }
        QList<int> removed;
        QList<int> added;
        // "\ No newline at end of file" may follow the last line of either run.
        for (; i < count && lines[i].kind != DiffLine::Added && lines[i].kind != DiffLine::Context; ++i) {
            if (lines[i].kind == DiffLine::Removed)
                removed.append(i);
        }
        for (; i < count && lines[i].kind != DiffLine::Removed && lines[i].kind != DiffLine::Context; ++i) {
            if (lines[i].kind == DiffLine::Added)
                added.append(i);
        }
        for (qsizetype k = 0; k < std::min(removed.size(), added.size()); ++k)
            pairs.append({removed[k], added[k]});
    }
    return pairs;
}
