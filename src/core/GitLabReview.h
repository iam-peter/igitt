#pragma once

#include "ReviewProvider.h"

#include <QJsonObject>

#include <optional>

// GitLab, asked through its command line tool glab with `glab api`, so the
// user's `glab auth login` is all it takes and igitt stores no token. A
// refresh lists the branch's merge requests, then reads the chosen one with
// its pipeline, its approvals and its discussions.

// The merge request to show from the list GitLab returns for a source
// branch: an open one, else the most recently updated; nullopt if the list
// is empty, with `error` set if it is not a list.
std::optional<BranchReview> parseGitLabMergeRequests(const QByteArray &json, QString *error);
// Fills `review` from a single merge request, which has its head pipeline.
void readGitLabMergeRequest(BranchReview &review, const QJsonObject &mr);
// From .../merge_requests/:iid/approvals.
void readGitLabApprovals(BranchReview &review, const QByteArray &json);
// Discussions with a resolvable note not resolved yet; nullopt if `json`
// is not a list of discussions.
std::optional<int> countGitLabUnresolved(const QByteArray &json);

// `-o merge_request....` push options for `options`, or `error` if one
// cannot be sent.
QStringList gitLabPushOptions(const ReviewOptions &options, QString *error);

const ReviewProvider &gitLabProvider();
