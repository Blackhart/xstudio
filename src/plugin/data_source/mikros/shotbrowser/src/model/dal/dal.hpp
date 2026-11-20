// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.hpp
 * @brief Data-access layer: sole Shot Browser gateway to the shared pms library.
 */
#pragma once

#include "model/context/shotbrowser_state.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/playlist.hpp"
#include "model/poco/playlist_version_link.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence_shots.hpp"
#include "model/poco/version.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser::dal {

using ::xstudio::mikros_shotbrowser::BrowserMode;
using ::xstudio::mikros_shotbrowser::ResultView;

/**
 * @brief Creates the shot browser context.
 *
 * The plugin calls this once, at launch. The context then lives for the process.
 */
void initialize();

/**
 * @brief Id of the project the panel is showing.
 * @return The project id, or `-1` when none is selected.
 */
std::int64_t selectedProject();

/**
 * @brief Remembers the project the user picked.
 * @param projectId Project id, or `-1` when the combo is on None.
 */
void setSelectedProject(std::int64_t projectId);

/**
 * @brief Id of the sequence the panel is showing.
 * @return The sequence id, or `-1` when none is selected.
 */
std::int64_t selectedSequence();

/**
 * @brief Remembers the sequence the user picked.
 * @param sequenceId Sequence id, or `-1` when none is selected.
 */
void setSelectedSequence(std::int64_t sequenceId);

/**
 * @brief Id of the shot the panel is showing.
 * @return The shot id, or `-1` when none is selected.
 */
std::int64_t selectedShot();

/**
 * @brief Remembers the shot the user picked.
 * @param shotId Shot id, or `-1` when none is selected.
 */
void setSelectedShot(std::int64_t shotId);

/**
 * @brief Id of the asset the panel is showing.
 * @return The asset id, or `-1` when none is selected.
 */
std::int64_t selectedAsset();

/**
 * @brief Remembers the asset the user picked.
 * @param assetId Asset id, or `-1` when none is selected.
 */
void setSelectedAsset(std::int64_t assetId);

/**
 * @brief Left tab the panel is showing.
 * @return `Shot` or `Asset`. The default is `Shot`.
 */
BrowserMode browserMode();

/**
 * @brief Remembers the left tab the user picked.
 * @param mode `Shot` or `Asset`.
 */
void setBrowserMode(BrowserMode mode);

/**
 * @brief Right tab the panel is showing.
 * @return `Versions` or `Playlist`. The default is `Versions`.
 */
ResultView resultView();

/**
 * @brief Remembers the right tab the user picked.
 * @param view `Versions` or `Playlist`.
 */
void setResultView(ResultView view);

/**
 * @brief Page size for catalog reads.
 *
 * The default is 50 for every backend. `MIKROS_PMS_PAGE_SIZE` replaces it.
 * A value above 500 is rejected: that is the largest page ShotGrid accepts.
 *
 * @return Rows requested per page.
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
 * @brief One page of sequences for a project, each with its shots.
 *
 * The shots travel on the sequence. Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum sequences in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @return Page of sequences. Empty when none match or the lookup fails.
 */
RowPage<poco::SequenceShots> findSequenceShots(
    std::int64_t projectId,
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
 * A playlist depends only on its project. Each row carries `version_count` from
 * the playlist's `versions` link list. Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @param nameOrId Optional exact id (all digits) or name substring.
 * @return Page of playlists. Empty when none match or the lookup fails.
 */
RowPage<poco::Playlist> findPlaylists(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt,
    std::optional<std::string> nameOrId = std::nullopt);

/**
 * @brief One page of version list rows for a project.
 *
 * Strong parent (at most one): In(shot), else In(sequence), else In(asset),
 * else In(project). Playlist is an optional weak AND on that parent.
 * Omits image and media fields so ShotGrid does not mint signed URLs.
 * Pass next_cursor to the next call.
 *
 * @param projectId Domain project `id`.
 * @param limit Maximum rows in the page. Must be greater than zero.
 * @param cursor Continuation from a prior RowPage::next_cursor, or empty.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id`.
 * @param nameOrId Optional exact id (all digits) or name substring.
 * @return Page of list elements. Empty when none match or the lookup fails.
 */
RowPage<poco::VersionListElement> findVersionListElements(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor = std::nullopt,
    std::optional<std::int64_t> sequenceId = std::nullopt,
    std::optional<std::int64_t> shotId = std::nullopt,
    std::optional<std::int64_t> assetId = std::nullopt,
    std::optional<std::int64_t> playlistId = std::nullopt,
    std::optional<std::string> nameOrId = std::nullopt);

/**
 * @brief Version membership of one playlist from its `versions` links field.
 * @param playlistId Domain playlist `id`.
 * @return Ordered link stubs. Empty when the playlist is missing or the lookup fails.
 */
std::vector<poco::PlaylistVersionLink> findPlaylistVersionLinks(std::int64_t playlistId);

/**
 * @brief Card rows for the given version ids (`Fields` without image or media).
 *
 * Builds an OR filter on `id`. Results are reordered to match `versionIds`.
 * Missing ids are omitted from the result.
 *
 * @param versionIds Domain version ids. Empty yields an empty vector.
 * @return List elements that matched, in `versionIds` order.
 */
std::vector<poco::VersionListElement>
findVersionListElementsByIds(const std::vector<std::int64_t> &versionIds);

/**
 * @brief Image URL or path for one version (`Fields(image)` only).
 * @param versionId Domain version `id`.
 * @return Thumb row, or empty id when missing or the lookup fails.
 */
poco::VersionThumb findVersionThumb(std::int64_t versionId);

/**
 * @brief Image URL or path for many versions in one find (`Fields(image)` only).
 * @param versionIds Domain version ids. Empty yields an empty vector.
 * @return Thumb rows that matched. Missing ids are omitted.
 */
std::vector<poco::VersionThumb> findVersionThumbs(const std::vector<std::int64_t> &versionIds);

/**
 * @brief Signed uploaded-movie URLs for many versions in one find.
 * @param versionIds Domain version ids. Empty yields an empty vector.
 * @return Transcoded rows that matched. Missing ids are omitted.
 */
std::vector<poco::VersionTranscodedMedia>
findVersionTranscodedMovies(const std::vector<std::int64_t> &versionIds);

/**
 * @brief How many sequences belong to a project.
 * @param projectId Domain project `id`.
 * @return Match count, or `0` when the lookup fails.
 */
int getNumberOfSequences(std::int64_t projectId);

/**
 * @brief How many shots belong to a project, or to one sequence.
 *
 * When `sequenceId` is set, counts shots in that sequence. Otherwise counts
 * every shot under the project.
 *
 * @param projectId Domain project `id`. Used when `sequenceId` is empty.
 * @param sequenceId Optional sequence `id`.
 * @return Match count, or `0` when the lookup fails.
 */
int getNumberOfShots(
    std::int64_t projectId, std::optional<std::int64_t> sequenceId = std::nullopt);

/**
 * @brief How many assets belong to a project.
 * @param projectId Domain project `id`.
 * @return Match count, or `0` when the lookup fails.
 */
int getNumberOfAssets(std::int64_t projectId);

/**
 * @brief How many playlists belong to a project.
 * @param projectId Domain project `id`.
 * @param nameOrId Optional exact id (all digits) or name substring.
 * @return Match count, or `0` when the lookup fails.
 */
int getNumberOfPlaylists(
    std::int64_t projectId, std::optional<std::string> nameOrId = std::nullopt);

/**
 * @brief How many versions match the same parents as `findVersionListElements`.
 *
 * Strong parent (at most one): shot, else sequence, else asset, else project.
 * Playlist is an optional weak AND on that parent.
 *
 * @param projectId Domain project `id`.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id`.
 * @param nameOrId Optional exact id (all digits) or name substring.
 * @return Match count, or `0` when the lookup fails.
 */
int getNumberOfVersions(
    std::int64_t projectId,
    std::optional<std::int64_t> sequenceId = std::nullopt,
    std::optional<std::int64_t> shotId = std::nullopt,
    std::optional<std::int64_t> assetId = std::nullopt,
    std::optional<std::int64_t> playlistId = std::nullopt,
    std::optional<std::string> nameOrId = std::nullopt);

} // namespace xstudio::mikros_shotbrowser::dal
