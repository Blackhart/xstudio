// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/media.hpp
 * @brief Resolve version media paths: sequence on disk, movie file, or signed-URL download.
 */
#pragma once

#include "model/poco/playlist_version_link.hpp"
#include "model/poco/version_media.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <caf/error.hpp>
#include <caf/event_based_actor.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Which sources are usable after a disk / URL check.
 */
struct SourceChoice {
    bool frames = false;    ///< Image sequence is on disk.
    bool movieFile = false; ///< `sg_path_to_movie` is on disk.
    bool movieUrl = false;  ///< Download `sg_uploaded_movie_mp4` because the movie file is not.
};

/**
 * @brief Paths ready to hand to an xSTUDIO media actor.
 */
struct ResolvedVersionPaths {
    std::string frames_spec; ///< Sequence path xSTUDIO accepts. Empty when none.
    std::string movie_path;  ///< Local movie file or downloaded cache. Empty when none.
};

/**
 * @brief Loads and resolves one version's media on a detached thread.
 *
 * Request and reply use `utility::JsonStore` (already registered with CAF).
 * Spawned detached so a blocking DAL / download does not occupy the panel actor.
 */
class RetrieveVersionMediaActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one retrieve request.
     * @param cfg CAF actor config.
     */
    explicit RetrieveVersionMediaActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs find + resolve and answers with a JsonStore of paths.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Spawns a detached retrieve actor and chains success / error callbacks.
 *
 * Internally uses CAF `request` / `then` with `JsonStore` messages.
 *
 * @param panel Panel actor that owns the request (must be an event-based actor).
 * @param versionId ShotGrid version id.
 * @param projectId Project id for the movie cache folder.
 * @param frameRange Optional sequence range override.
 * @param then_ok Called on the panel actor with resolved paths.
 * @param then_err Called on the panel actor when the request fails.
 */
void retrieveVersionMedia(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &frameRange,
    std::function<void(ResolvedVersionPaths)> then_ok,
    std::function<void(const caf::error &)> then_err = {});

/**
 * @brief Loads the version membership of one ShotGrid playlist on a detached thread.
 *
 * Request and reply use `utility::JsonStore` (already registered with CAF).
 * Spawned detached so a blocking DAL call does not occupy the panel actor.
 */
class RetrieveVersionsForPlaylistActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one playlist-versions request.
     * @param cfg CAF actor config.
     */
    explicit RetrieveVersionsForPlaylistActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs `findPlaylistVersionLinks` and answers with a JsonStore of stubs.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Spawns a detached actor that loads playlist version links from the DAL.
 *
 * @param panel Panel actor that owns the request (must be an event-based actor).
 * @param playlistId ShotGrid playlist id.
 * @param then_ok Called on the panel actor with ordered link stubs.
 * @param then_err Called on the panel actor when the request fails.
 */
void retrieveVersionsForPlaylist(
    plugin::StandardPlugin &panel,
    std::int64_t playlistId,
    std::function<void(std::vector<poco::PlaylistVersionLink>)> then_ok,
    std::function<void(const caf::error &)> then_err = {});

/**
 * @brief Gets or creates an xSTUDIO playlist, then activates it (inspected + viewport).
 *
 * Empty `name` resolves to the next free `Playlist N` via the session. A filled name
 * reuses that playlist when it already exists (no-op create), otherwise creates it.
 *
 * @param panel Panel actor used to reach the session.
 * @param name Target playlist name, or empty for `Playlist N`.
 * @param then_ok Called with the playlist uuid/actor and the resolved playlist name.
 * @param then_err Called when the session request fails.
 */
void create_playlist(
    plugin::StandardPlugin &panel,
    const std::string &name,
    std::function<void(utility::UuidActor, std::string)> then_ok,
    std::function<void(const caf::error &)> then_err = {});

/**
 * @brief Ensures one version's media is present in a playlist (no selection).
 *
 * When `versionId` is already stamped on media in `playlist`, returns that media.
 * Otherwise spawns sources, stamps the id, and adds the media.
 *
 * @param panel Panel actor that owns media actors.
 * @param playlist Target playlist.
 * @param paths Local frames spec and/or movie path from `retrieveVersionMedia`.
 * @param name Media display name (usually the version code).
 * @param versionId ShotGrid version id stored under `/metadata/mikros/version`.
 * @param then_ok Called with the media uuid and actor (existing or newly added).
 * @param then_err Called when rate, add, or listing media fails, or paths empty.
 */
void add_version_to_playlist(
    plugin::StandardPlugin &panel,
    const utility::UuidActor &playlist,
    const ResolvedVersionPaths &paths,
    const std::string &name,
    std::int64_t versionId,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err = {});

/**
 * @brief Finds media stamped with `versionId` in `playlist` and selects it in the editor.
 *
 * @param panel Panel actor that owns the request.
 * @param playlist Playlist that holds the media.
 * @param versionId ShotGrid version id to match under `/metadata/mikros/version`.
 * @param then_ok Called with the selected media.
 * @param then_err Called when listing, lookup, or select fails, or when no media matches.
 */
void selectAddedMedia(
    plugin::StandardPlugin &panel,
    const utility::UuidActor &playlist,
    std::int64_t versionId,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err = {});

/**
 * @brief Resolves media, gets or creates a playlist, adds the version, then selects it.
 *
 * Clears the ephemeral `preview` playlist first.
 *
 * @param panel Panel actor used for retrieve, session, and media spawn.
 * @param versionId ShotGrid version id.
 * @param projectId Project id for the movie cache folder.
 * @param name Version display name (media label).
 * @param playlistName Target playlist, or empty for the next `Playlist N`.
 * @param frameRange Optional sequence range override.
 */
void open_version(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &name,
    const std::string &playlistName,
    const std::string &frameRange);

/**
 * @brief Loads playlist version links, creates one xSTUDIO playlist, then adds and selects each.
 *
 * Clears the ephemeral `preview` playlist first. For each version:
 * `add_version_to_playlist` then `selectAddedMedia`, one at a time.
 *
 * @param panel Panel actor used for retrieve, session, and media spawn.
 * @param playlistId ShotGrid playlist id.
 * @param projectId Project id for the movie cache folder.
 * @param playlistName Target xSTUDIO playlist name (usually the ShotGrid playlist name).
 */
void open_playlist(
    plugin::StandardPlugin &panel,
    std::int64_t playlistId,
    std::int64_t projectId,
    const std::string &playlistName);

/**
 * @brief Name of the ephemeral single-click preview playlist (session-hidden).
 */
inline constexpr const char *kPreviewPlaylistName = "preview";

/**
 * @brief Loads one version into a hidden `preview` playlist and plays it in the viewport.
 *
 * Uses a session-hidden playlist (not in the playlist tree). Reuses one instance: clears
 * its media, adds the version, selects it, activates the viewport only (does not change
 * the inspected media list), then starts playback. A second call with the same version
 * id is a no-op. Bumps an internal generation so a slower earlier request cannot
 * overwrite a newer click.
 *
 * @param panel Panel actor used for retrieve, session, and media spawn.
 * @param versionId ShotGrid version id.
 * @param projectId Project id for the movie cache folder.
 * @param name Version display name (media label).
 * @param frameRange Optional sequence range override.
 */
void preview_version(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &name,
    const std::string &frameRange);

/**
 * @brief Deactivates the hidden `preview` playlist without destroying it.
 *
 * Restores the viewport to the inspected playlist, clears preview media, and
 * resets the previewed version id. The hidden playlist actor is kept for reuse:
 * it is linked to the session, so `send_exit` would propagate `user_shutdown`.
 * Idempotent. Aborts any in-flight `preview_version` by bumping the generation.
 *
 * @param panel Panel actor used to reach the session and clear media.
 */
void clear_preview(plugin::StandardPlugin &panel);

/**
 * @brief Drops `preview` when the viewport leaves that playlist.
 *
 * No-op when there is no preview, or when `viewed` is still the preview playlist.
 *
 * @param panel Panel actor used to clear.
 * @param viewed Current viewport media container.
 */
void on_preview_viewport_container_changed(
    plugin::StandardPlugin &panel, const utility::UuidActor &viewed);

/**
 * @brief True when `path` is a regular file.
 * @param path Filesystem path.
 * @return True when the path names an existing regular file.
 */
bool regularFile(const std::string &path);

/**
 * @brief Picks the sources that can be opened.
 * @param framesOnDisk True when the sequence exists on disk.
 * @param movieOnDisk True when `sg_path_to_movie` exists on disk.
 * @param hasUploadedUrl True when a signed movie URL is present.
 * @return The chosen frames / movie-file / movie-URL flags.
 */
SourceChoice chooseSources(bool framesOnDisk, bool movieOnDisk, bool hasUploadedUrl);

/**
 * @brief Sequence spec accepted by xSTUDIO when at least one frame is on disk.
 * @param path ShotGrid frames path (may use FileSeq syntax).
 * @param frameRange Optional range that bounds the sequence.
 * @return Path for xSTUDIO, or empty when nothing is on disk.
 */
std::string sequenceSpec(const std::string &path, const std::string &frameRange);

/**
 * @brief Cache file for one downloaded movie.
 * @param projectId Project id used in the cache folder.
 * @param versionId Version id used in the cache folder.
 * @return Absolute path under the user home cache, or empty when home is unknown.
 */
std::filesystem::path movieCachePath(std::int64_t projectId, std::int64_t versionId);

/**
 * @brief Writes `bytes` to `path` when the file is not already there.
 * @param path Destination file.
 * @param bytes Movie body.
 * @return True when the file exists after the call.
 */
bool writeMovie(const std::filesystem::path &path, const std::string &bytes);

/**
 * @brief Downloads `url` and returns the body bytes.
 * @param url Signed movie URL.
 * @return Body bytes, or empty on failure.
 */
std::string fetchMovie(const std::string &url);

/**
 * @brief Resolves frames and movie paths for one version (disk check + optional download).
 * @param media DAL row with frames / movie / signed URL.
 * @param projectId Project id for the movie cache folder.
 * @param frameRange Optional override for the sequence range. Empty uses `media.frame_range`.
 * @return Paths that exist locally after the call.
 */
ResolvedVersionPaths resolveVersionPaths(
    const poco::VersionMedia &media,
    std::int64_t projectId,
    const std::string &frameRange = {});

} // namespace xstudio::mikros_shotbrowser
