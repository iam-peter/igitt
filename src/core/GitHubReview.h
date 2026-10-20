#pragma once

#include "ReviewProvider.h"

#include <optional>

// GitHub, asked through its command line tool gh, so the user's `gh auth
// login` is all it takes and igitt stores no token. One GraphQL query per
// refresh gets the branch's pull request with its review decision, checks
// and review threads.

// The GraphQL query sent with `gh api graphql`, taking owner, name and
// branch.
QString gitHubQuery();

// The pull request for a branch from the answer to gitHubQuery(): an open
// one, else the most recently updated. Pull requests whose branch lives in
// another fork than `owner`'s are left out; those in the parent repository
// are included. nullopt with `error` empty if there is none.
std::optional<BranchReview> parseGitHubPullRequests(const QByteArray &json, const QString &owner, QString *error);

// `gh pr create` arguments for what `target.review` asks for, without
// `--repo`.
QStringList gitHubCreateArgs(const PushTarget &target);

const ReviewProvider &gitHubProvider();
