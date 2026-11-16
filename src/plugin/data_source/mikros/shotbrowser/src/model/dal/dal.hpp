// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.hpp
 * @brief Data-access layer: sole plugin gateway to the pms library.
 */
#pragma once

#include "model/poco/asset.hpp"
#include "model/poco/playlist.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"
#include "model/poco/version.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser::dal {

/**
 * @brief One page of POCO rows with an optional continuation cursor.
 * @tparam Row POCO type with `id` and `name` members.
 */
template <typename Row> struct RowPage {
    std::vector<Row> rows;                  ///< Rows in this page, catalog order.
    std::optional<std::string> next_cursor; ///< Pass back as `cursor` for the next page.
};

/**
 * @brief One page of projects from the shared pms session.
 *
 * Call from a worker thread when the backend may block (FakeBackend latency).
 * Pass next_cursor to the next call.
 *
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of projects. Empty when the lookup fails.
 */
RowPage<poco::Project> projects(
    std::size_t limit, std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of sequences belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of sequences. Empty when none match or the lookup fails.
 */
RowPage<poco::Sequence> sequences(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of shots belonging to a project, optionally narrowed to one sequence.
 *
 * With no sequence id, returns shots of the whole project. With a sequence id,
 * returns only shots of that sequence. Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @param sequenceId Domain sequence `id`, or empty for the whole project.
 * @return Page of shots. Empty when none match or the lookup fails.
 */
RowPage<poco::Shot> shots(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt,
    std::optional<std::int64_t> sequenceId = std::nullopt);

/**
 * @brief One page of assets belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of assets. Empty when none match or the lookup fails.
 */
RowPage<poco::Asset> assets(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of playlists belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of playlists. Empty when none match or the lookup fails.
 */
RowPage<poco::Playlist> playlists(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of versions for a project.
 *
 * Strong parent (at most one): In(shot), else In(sequence), else In(asset),
 * else In(project) when no playlist either. Playlist is an optional weak AND
 * filter. Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id` (AND with the strong parent).
 * @return Page of versions. Empty when none match or the lookup fails.
 */
RowPage<poco::Version> versions(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt,
    std::optional<std::int64_t> sequenceId = std::nullopt,
    std::optional<std::int64_t> shotId = std::nullopt,
    std::optional<std::int64_t> assetId = std::nullopt,
    std::optional<std::int64_t> playlistId = std::nullopt);

/**
 * @brief Drops every page stored in the shared CachingBackend.
 *
 * Call on explicit refresh or when the active project changes in a way that
 * should not reuse prior answers.
 */
void clearCache();

} // namespace xstudio::mikros_shotbrowser::dal
