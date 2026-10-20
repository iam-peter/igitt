#pragma once

#include "FoldEngine.h"
#include "GitRunner.h"
#include "StateLoader.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

// One action of a plan, with the inputs needed to simulate it again.
struct PlanEntry
{
    enum Kind {
        StagePaths,   // git add, recorded as the index entries it produced
        UnstagePaths, // git reset <head> -- paths
        UnstageAll,
        ApplyPatch,   // stage or unstage lines
        Commit,
        Fold,
    };

    int id = 0;
    Kind kind = ApplyPatch;
    QString title;

    QStringList paths;    // StagePaths (empty: everything), UnstagePaths
    QByteArray indexInfo; // StagePaths, after the first simulation
    QByteArray patch;     // ApplyPatch, Fold
    bool reverse = false; // ApplyPatch
    QString message;      // Commit
    bool amend = false;   // Commit
    QString target;       // Fold: a commit id from before the plan, or "plan:<id>"
    FoldBase base = FoldBase::Index;

    // From the latest simulation.
    QString log;   // the commands it ran, with their input
    QString error; // set when it no longer applies after an earlier entry was removed
};

// A plan: write actions simulated on a scratch index and a simulated HEAD,
// published together by execute() or not at all.
//
// None of the actions touch the working tree, so the simulation is exact:
// execute() does not run them again, it publishes the reviewed result. It
// moves HEAD only if HEAD is still where planning began, and replaces the
// index under git's own index lock only if nobody changed it meanwhile.
//
// Commit hooks (pre-commit, prepare-commit-msg, commit-msg) run when a
// commit is planned, so the plan shows the message they produce.
//
// Not thread-safe; only the git thread uses it.
class PlanSession
{
    Q_DECLARE_TR_FUNCTIONS(PlanSession)

public:
    // Snapshots HEAD and the index of the repository `git` points at.
    bool begin(const GitRunner &git, QString &error);

    // Simulates `entry` on top of the plan and appends it. If it fails,
    // the plan stays as it was.
    bool add(PlanEntry entry, QString &error);
    // Drops an entry and simulates the remaining ones from the start;
    // entries that no longer apply get an error.
    void remove(int id);
    bool execute(QString &error);

    bool active() const { return m_active; }
    // The simulated repository, or the real one if begin() failed.
    StateSource source() const;
    QList<PlanEntry> entries() const { return m_entries; }
    bool executable() const;
    QString head() const { return m_head; }
    QString baselineHead() const { return m_baselineHead; }
    // The commands execute() will run.
    QString publishSummary() const;

private:
    bool simulate(PlanEntry &entry, QString &error);
    bool simulateCommit(PlanEntry &entry, QString &error);
    bool simulateFold(PlanEntry &entry, QString &error);
    GitResult run(PlanEntry &entry, const QStringList &args, const QByteArray &input = {},
                  const QProcessEnvironment &env = {});
    void rewrite(const QString &oldId, const QString &newId);
    QString resolve(const QString &id) const;
    QString stableId(const QString &sha) const;
    QString indexPath(const QString &name) const;

    QTemporaryDir m_dir;
    GitRunner m_real;
    GitRunner m_git; // works on the scratch index
    QString m_indexPath; // the repository's real index
    QString m_baselineHead;
    QString m_baselineTree;
    QString m_head;
    // Where commits from before the plan, and commits the plan created,
    // are now; rewritten by folds and amends.
    QHash<QString, QString> m_resolved;
    QList<PlanEntry> m_entries;
    int m_nextId = 1;
    bool m_active = false;
};
