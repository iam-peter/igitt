#pragma once

#include "DiffParser.h"
#include "FoldEngine.h"
#include "GitRunner.h"
#include "PatchBuilder.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

struct FoldSuggestion
{
    enum Status { None, Single, Ambiguous };

    Status status = None;
    QString target;      // Single: the commit to fold into
    QStringList targets; // Ambiguous: the commits involved, newest first
    QString reason;      // None: why there is no suggestion
};

// Suggests the commit selected lines belong in, as `git absorb` does: the
// newest local commit that last touched the lines a change modifies or
// borders. Bordering lines count because git's merge treats changes to
// adjacent lines as a conflict, so no older commit could take the change.
//
// Only commits a fold can target are candidates: the first-parent chain from
// HEAD down to the first merge commit or the merge base with the upstream
// branch, whichever comes first. Lines older than that are free to go into
// any candidate.
class FoldSuggester
{
    Q_DECLARE_TR_FUNCTIONS(FoldSuggester)

public:
    // A plain value: analyze() on the git thread, then hand it over to the
    // UI thread for suggest().
    FoldSuggester() = default;
    explicit FoldSuggester(const GitRunner &git);

    // Blames what every change in `files` touches, so suggest() is cheap.
    // Call again whenever the diff or HEAD changes.
    void analyze(const QList<FileDiff> &files, FoldBase base);
    void clear();

    // `selection` is indexed by file, like the `files` passed to analyze().
    FoldSuggestion suggest(const QList<LineSelection> &selection) const;

    // HEAD first.
    const QStringList &candidates() const { return m_candidates; }

private:
    enum class Origin {
        Free,    // only touches history older than the candidates
        Local,   // newest candidate touching it is `candidate`
        Blocked, // touches lines that are not committed yet
    };
    struct LineOrigin
    {
        Origin origin = Origin::Free;
        int candidate = -1;
    };
    using FileOrigins = QList<QList<LineOrigin>>; // [hunk][line]

    void loadCandidates();
    void analyzeFile(const FileDiff &file, FoldBase base, FileOrigins &origins) const;

    GitRunner m_git;
    QStringList m_candidates;
    QString m_boundary; // blame stops here; empty if the candidates reach the root
    QList<FileOrigins> m_origins;
};
