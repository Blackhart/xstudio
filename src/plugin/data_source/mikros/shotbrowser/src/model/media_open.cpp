// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_open.cpp
 * @brief Decides the sources, then mails the paths back to the panel.
 */

#include "model/media_open.hpp"

#include "model/log.hpp"
#include "model/media_fetch.hpp"
#include "model/media_files.hpp"
#include "model/media_plan.hpp"

#include "xstudio/utility/json_store.hpp"

#include <thread>
#include <utility>

namespace xstudio::mikros_shotbrowser {
namespace {

/**
 * @brief Builds the ready message for one request.
 * @param request Paths as ShotGrid sent them.
 * @return Sources that can be added. Both paths stay empty when nothing is usable.
 */
OpenReady resolve(const OpenRequest &request) {
    const std::string frames = sequenceSpec(request.frames, request.frame_range);
    const bool movieOnDisk = regularFile(request.movie);
    const bool hasUrl = !request.uploaded_movie.empty();
    const SourceChoice choice = chooseSources(!frames.empty(), movieOnDisk, hasUrl);

    OpenReady ready;
    ready.version_id = request.version_id;
    ready.token = request.token;
    ready.select = request.select;
    ready.name = request.name;
    ready.playlist_uuid = request.playlist_uuid;
    if (choice.frames) {
        ready.frames_spec = frames;
    }
    if (choice.movieFile) {
        ready.movie_path = request.movie;
    } else if (choice.movieUrl) {
        const std::filesystem::path cache =
            movieCachePath(request.project_id, request.version_id);
        if (!cache.empty() && !regularFile(cache.string())) {
            writeMovie(cache, fetchMovie(request.uploaded_movie)); ///< Signed URL, saved once.
        }
        if (regularFile(cache.string())) {
            ready.movie_path = cache.string();
        }
    }
    return ready;
}

/**
 * @brief One log line for the sources that will be added.
 * @param ready Resolved paths.
 */
void logOpen(const OpenReady &ready) {
    const char *movie = "none";
    if (!ready.movie_path.empty()) {
        movie = ready.movie_path.find("mikros_shotbrowser/movies") == std::string::npos ? "file"
                                                                                          : "url";
    }
    log::info(
        "open versionId=" + std::to_string(ready.version_id) +
        " frames=" + std::string(ready.frames_spec.empty() ? "no" : "yes") +
        " movie=" + movie);
}

} // namespace

void resolveOpen(caf::weak_actor_ptr panel, OpenRequest request) {
    std::thread([panel = std::move(panel), request = std::move(request)]() mutable {
        try {
            const OpenReady ready = resolve(request);
            logOpen(ready); ///< Say why nothing was opened, or which sources remain.
            if (ready.frames_spec.empty() && ready.movie_path.empty()) {
                return;
            }
            auto handle = panel.lock();
            if (!handle) {
                return; ///< The panel closed while the file was checked.
            }
            nlohmann::json body = {
                {"versionId", ready.version_id},
                {"token", ready.token},
                {"select", ready.select},
                {"name", ready.name},
                {"playlistUuid", ready.playlist_uuid},
                {"framesSpec", ready.frames_spec},
                {"moviePath", ready.movie_path}};
            caf::anon_mail(
                xstudio::data_source::put_data_atom_v,
                ready.token,
                std::string("open_media"),
                request.project_id,
                false,
                std::int64_t{0},
                false,
                std::int64_t{0},
                false,
                std::int64_t{0},
                false,
                utility::JsonStore(std::move(body)))
                .send(caf::actor_cast<caf::actor>(handle));
        } catch (const std::exception &caught) {
            log::failure(caught);
        }
    }).detach();
}

} // namespace xstudio::mikros_shotbrowser
