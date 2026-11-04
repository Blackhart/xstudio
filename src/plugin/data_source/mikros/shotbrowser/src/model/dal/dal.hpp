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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser::dal {

/**
 * @brief One page of version names with an optional continuation cursor.
 */
struct NamePage {
    std::vector<std::string> names;         ///< Names in this page, catalog order.
    std::optional<std::string> next_cursor; ///< Pass to the next versionNames call.
};

/**
 * @brief Projects from the shared pms session.
 *
 * Call from a worker thread when the backend may block (FakeBackend latency).
 *
 * @return Projects in catalog order. Empty when the lookup fails.
 */
std::vector<poco::Project> projects();

/**
 * @brief Sequences belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @return Sequences in catalog order. Empty when none match or the lookup fails.
 */
std::vector<poco::Sequence> sequences(std::int64_t projectId);

/**
 * @brief Shots belonging to a sequence.
 *
 * @param sequenceId Domain sequence `id`.
 * @return Shots in catalog order. Empty when none match or the lookup fails.
 */
std::vector<poco::Shot> shots(std::int64_t sequenceId);

/**
 * @brief Assets belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @return Assets in catalog order. Empty when none match or the lookup fails.
 */
std::vector<poco::Asset> assets(std::int64_t projectId);

/**
 * @brief Playlists belonging to a project.
 *
 * @param projectId Domain project `id`.
 * @return Playlists in catalog order. Empty when none match or the lookup fails.
 */
std::vector<poco::Playlist> playlists(std::int64_t projectId);

/**
 * @brief One page of version names for a project.
 *
 * Strong parent (at most one): In(shot), else In(sequence), else In(asset),
 * else In(project) when no playlist either. Playlist is an optional weak AND
 * filter. Pass next_cursor to After for the following page.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum names in the page. Must be greater than zero.
 * @param cursor Continuation from a prior NamePage::next_cursor, or empty.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id` (AND with the strong parent).
 * @return Page of names and an optional next cursor. Empty on failure.
 */
NamePage versionNames(
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
