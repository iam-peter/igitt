#include "FoldSuggester.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

constexpr int MaxCandidates = 1000;

int lineCount(const QByteArray &content)
{
    if (content.isEmpty())
        return 0;
    return int(content.count('\n')) + (content.endsWith('\n') ? 0 : 1);
}

// Turns a set of line numbers into blame's -L arguments.
QStringList lineRanges(QList<int> lines)
{
    std::sort(lines.begin(), lines.end());
    QStringList args;
    for (qsizetype i = 0; i < lines.size();) {
        qsizetype j = i;
        while (j + 1 < lines.size() && lines[j + 1] == lines[j] + 1)
            ++j;
        args << QStringLiteral("-L") << QStringLiteral("%1,%2").arg(lines[i]).arg(lines[j]);
        i = j + 1;
    }
    return args;
}

struct Blame
{
    QHash<int, QString> shaByLine;
    QSet<QString> boundaries;
};

Blame parseBlame(const QByteArray &out)
{
    static const QRegularExpression header(QStringLiteral("^([0-9a-f]{40,64}) \\d+ (\\d+)"));
    Blame blame;
    QString current;
    for (const QByteArray &line : out.split('\n')) {
        if (line.startsWith('\t'))
            continue;
        if (line == "boundary") {
            blame.boundaries.insert(current);
            continue;
        }
        const QRegularExpressionMatch m = header.match(QString::fromLatin1(line));
        if (m.hasMatch()) {
            current = m.captured(1);
            blame.shaByLine.insert(m.captured(2).toInt(), current);
        }
    }
    return blame;
}

bool isNullId(const QString &sha)
{
    return std::all_of(sha.cbegin(), sha.cend(), [](QChar c) { return c == QLatin1Char('0'); });
}

} // namespace

FoldSuggester::FoldSuggester(const GitRunner &git)
    : m_git(git)
{
}

void FoldSuggester::clear()
{
    m_candidates.clear();
    m_boundary.clear();
    m_origins.clear();
}

void FoldSuggester::analyze(const QList<FileDiff> &files, FoldBase base, const QString &head)
{
    clear();
    m_head = head;
    loadCandidates(head);
    for (const FileDiff &file : files) {
        FileOrigins origins;
        for (const DiffHunk &hunk : file.hunks)
            origins.append(QList<LineOrigin>(hunk.lines.size()));
        if (!m_candidates.isEmpty())
            analyzeFile(file, base, origins);
        m_origins.append(origins);
    }
}

void FoldSuggester::loadCandidates(const QString &head)
{
    QStringList args{QStringLiteral("rev-list"), QStringLiteral("--first-parent"), QStringLiteral("--parents"),
                     QStringLiteral("--max-count=%1").arg(MaxCandidates), head};
    const GitResult upstream = m_git.run({QStringLiteral("merge-base"), head, QStringLiteral("@{upstream}")});
    if (upstream.ok())
        args << QLatin1Char('^') + QString::fromLatin1(upstream.out).trimmed();

    const GitResult result = m_git.run(args);
    if (!result.ok())
        return;
    QString lastParent;
    for (const QByteArray &line : result.out.split('\n')) {
        const QStringList ids = QString::fromLatin1(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (ids.isEmpty())
            continue;
        if (ids.size() > 2) {
            // A merge: folds cannot reach it or anything below it.
            m_boundary = ids.first();
            return;
        }
        m_candidates.append(ids.first());
        lastParent = ids.value(1);
    }
    m_boundary = lastParent;
}

void FoldSuggester::analyzeFile(const FileDiff &file, FoldBase base, FileOrigins &origins) const
{
    // A file without a base version has no history to blame; its lines can
    // go anywhere.
    if (file.isNew || file.isBinary || file.hunks.isEmpty())
        return;

    const auto blockAll = [&origins] {
        for (QList<LineOrigin> &hunk : origins)
            std::fill(hunk.begin(), hunk.end(), LineOrigin{Origin::Blocked, -1});
    };

    const QString spec = base == FoldBase::Head ? m_head + QLatin1Char(':') + file.oldPath
                                                : QStringLiteral(":") + file.oldPath;
    const GitResult content = m_git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), spec});
    if (!content.ok()) {
        // E.g. a file that is staged but not in HEAD yet.
        blockAll();
        return;
    }
    const int lines = lineCount(content.out);

    // The base lines each changed line touches: a removed line and its
    // neighbours, or the two lines an added line is inserted between.
    QList<QList<QList<int>>> touched(file.hunks.size());
    QSet<int> all;
    for (qsizetype h = 0; h < file.hunks.size(); ++h) {
        const DiffHunk &hunk = file.hunks[h];
        touched[h].resize(hunk.lines.size());
        int previous = hunk.oldCount == 0 ? hunk.oldStart : hunk.oldStart - 1;
        for (qsizetype l = 0; l < hunk.lines.size(); ++l) {
            const DiffLine &line = hunk.lines[l];
            QList<int> around;
            switch (line.kind) {
            case DiffLine::Context:
                previous = line.oldLine;
                break;
            case DiffLine::Removed:
                around = {line.oldLine - 1, line.oldLine, line.oldLine + 1};
                previous = line.oldLine;
                break;
            case DiffLine::Added:
                around = {previous, previous + 1};
                break;
            case DiffLine::NoNewline:
                break;
            }
            for (int n : std::as_const(around)) {
                if (n >= 1 && n <= lines) {
                    touched[h][l].append(n);
                    all.insert(n);
                }
            }
        }
    }
    if (all.isEmpty())
        return;

    // --root: blame flags root commits as boundaries otherwise, and a root
    // commit can be a candidate.
    QStringList args{QStringLiteral("blame"), QStringLiteral("--porcelain"), QStringLiteral("--root")};
    args += lineRanges(all.values());
    if (base == FoldBase::Index)
        args << QStringLiteral("--contents") << QStringLiteral("-");
    args << (m_boundary.isEmpty() ? m_head : m_boundary + QStringLiteral("..") + m_head)
         << QStringLiteral("--") << file.oldPath;
    const GitResult blamed = m_git.run(args, base == FoldBase::Index ? content.out : QByteArray());
    if (!blamed.ok()) {
        blockAll();
        return;
    }
    const Blame blame = parseBlame(blamed.out);

    for (qsizetype h = 0; h < touched.size(); ++h) {
        for (qsizetype l = 0; l < touched[h].size(); ++l) {
            LineOrigin origin;
            for (int n : std::as_const(touched[h][l])) {
                const QString sha = blame.shaByLine.value(n);
                if (isNullId(sha)) {
                    origin = {Origin::Blocked, -1};
                    break;
                }
                const int candidate = blame.boundaries.contains(sha) ? -1 : int(m_candidates.indexOf(sha));
                if (candidate >= 0 && (origin.origin == Origin::Free || candidate < origin.candidate))
                    origin = {Origin::Local, candidate};
            }
            origins[h][l] = origin;
        }
    }
}

FoldSuggestion FoldSuggester::suggest(const QList<LineSelection> &selection) const
{
    FoldSuggestion suggestion;
    QMap<int, bool> targets; // ordered by candidate index, newest first
    bool any = false;
    bool blocked = false;

    for (qsizetype f = 0; f < selection.size() && f < m_origins.size(); ++f) {
        for (qsizetype h = 0; h < selection[f].size() && h < m_origins[f].size(); ++h) {
            for (qsizetype l = 0; l < selection[f][h].size() && l < m_origins[f][h].size(); ++l) {
                if (!selection[f][h][l])
                    continue;
                any = true;
                const LineOrigin &origin = m_origins[f][h][l];
                if (origin.origin == Origin::Blocked)
                    blocked = true;
                else if (origin.origin == Origin::Local)
                    targets.insert(origin.candidate, true);
            }
        }
    }

    if (!any) {
        suggestion.reason = tr("Select the lines to fold first.");
    } else if (blocked) {
        suggestion.reason = tr("The selection touches lines that are not committed yet.");
    } else if (targets.isEmpty()) {
        suggestion.reason = m_candidates.isEmpty()
            ? tr("There are no local commits to fold into.")
            : tr("The selection only touches code older than your local commits; any of them can take it.");
    } else if (targets.size() == 1) {
        suggestion.status = FoldSuggestion::Single;
        suggestion.target = m_candidates[targets.firstKey()];
    } else {
        suggestion.status = FoldSuggestion::Ambiguous;
        for (auto it = targets.cbegin(); it != targets.cend(); ++it)
            suggestion.targets.append(m_candidates[it.key()]);
    }
    return suggestion;
}
