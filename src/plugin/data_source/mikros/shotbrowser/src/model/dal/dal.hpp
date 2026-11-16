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
 * @brief Page size for catalog reads.
 *
 * The fake backend stays at 5 so paging stays visible. ShotGrid uses 500, the
 * maximum its REST search accepts.
 *
 * @return Rows requested per page for the active backend.
 */
std::size_t readPageLimit();

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
RowPage<poco::Project> findProjects(
    std::size_t limit, std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of sequences belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of sequences. Empty when none match or the lookup fails.
 */
RowPage<poco::Sequence> findSequences(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of shots belonging to one sequence.
 *
 * Pass next_cursor to the next call.
 *
 * @param sequenceId Domain sequence `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of shots. Empty when none match or the lookup fails.
 */
RowPage<poco::Shot> findShots(
    std::int64_t sequenceId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of assets belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of assets. Empty when none match or the lookup fails.
 */
RowPage<poco::Asset> findAssets(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of playlists belonging to a project.
 *
 * A playlist depends only on its project. Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of playlists. Empty when none match or the lookup fails.
 */
RowPage<poco::Playlist> findPlaylists(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt);

/**
 * @brief One page of versions for a project.
 *
 * Strong parent (at most one): In(shot), else In(sequence), else In(asset),
 * else In(project). Playlist is an optional weak AND on that parent.
 * Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id`.
 * @return Page of versions. Empty when none match or the lookup fails.
 */
RowPage<poco::Version> findVersions(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt,
    std::optional<std::int64_t> sequenceId = std::nullopt,
    std::optional<std::int64_t> shotId = std::nullopt,
    std::optional<std::int64_t> assetId = std::nullopt,
    std::optional<std::int64_t> playlistId = std::nullopt);

} // namespace xstudio::mikros_shotbrowser::dal
