// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/open_media.cpp
 * @brief Double-click and menu: disk check, signed-movie download, and insert into the playlist.
 */

#include "viewModel/open_media.hpp"
#include "xstudio/media/media_actor.hpp"
#include "xstudio/utility/frame_list.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/media_reference.hpp"
#include "xstudio/utility/uuid.hpp"

#include <caf/actor_registry.hpp>
#include <caf/actor_system.hpp>

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>


namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Sources kept for one version after the disk check.
 *
 * A movie file on disk wins over the signed URL. The URL is only a fallback.
 */
struct SourceChoice {
    bool frames = false;    ///< Image sequence is on disk.
    bool movieFile = false; ///< `sg_path_to_movie` is on disk.
    bool movieUrl = false;  ///< Download `sg_uploaded_movie_mp4` because the movie file is not.
};

/**
 * @brief Picks the sources that can be opened.
 * @param framesOnDisk True when at least one frame of the sequence exists.
 * @param movieOnDisk True when the movie path is a regular file.
 * @param hasUploadedUrl True when ShotGrid sent a signed movie URL.
 * @return The sources to add. All three stay false when nothing can be opened.
 */
SourceChoice chooseSources(bool framesOnDisk, bool movieOnDisk, bool hasUploadedUrl) {
    SourceChoice choice;
    choice.frames = framesOnDisk; ///< Keep the sequence when a frame is on this site.
    choice.movieFile = movieOnDisk;
    choice.movieUrl = !movieOnDisk && hasUploadedUrl; ///< Signed URL only replaces a missing movie.
    return choice;
}

/**
 * @brief True when a ShotGrid frame range can be appended to a sequence path.
 * @param range Text such as `1001-1100` or `1001-1100x2`.
 * @return False for an empty or prose range.
 */
bool usableRange(const std::string &range) {
    static const std::regex pattern(R"(^[0-9]+-[0-9]+(x[0-9]+)?$)");
    return std::regex_match(range, pattern);
}

/**
 * @brief True when `path` is a regular file.
 * @param path Posix path from ShotGrid, or a cache file.
 * @return False when the path is empty, missing, or not a file.
 */
bool regularFile(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

/**
 * @brief Sequence spec accepted by xSTUDIO when at least one frame is on disk.
 *
 * `frameRange` is appended as an xSTUDIO range when it looks like `1001-1100`.
 * Otherwise the directory is scanned.
 * @param path ShotGrid sequence path, with `#`, `@`, or `{frame}`.
 * @param frameRange Version frame range. Empty scans the directory.
 * @return The spec, or empty when no frame exists.
 */
std::string sequenceSpec(const std::string &path, const std::string &frameRange) {
    if (path.empty()) {
        return {};
    }
    const bool ranged = usableRange(frameRange);
    const std::string spec = ranged && path.find('=') == std::string::npos ? path + "=" + frameRange
                                                                             : path;
    try {
        utility::FrameList frames;
        const caf::uri uri = utility::parse_cli_posix_path(spec, frames, !ranged);
        if (frames.empty()) {
            return {};
        }
        utility::MediaReference reference(uri, frames, false);
        const auto sample = reference.pick_representative_frame();
        if (!sample || !regularFile(utility::uri_to_posix_path(sample->first))) {
            return {}; ///< The pattern parsed, but no frame is on this site.
        }
        return spec;
    } catch (const std::exception &) {
        return {};
    }
}


/**
 * @brief Cache file for one downloaded movie.
 * @param projectId Project id. Zero when no project is selected.
 * @param versionId Version id.
 * @return `${HOME}/xStudio/mikros_shotbrowser/movies/<projectId>/<versionId>/movie.mp4`.
 *         Empty when the home directory is missing.
 */
std::filesystem::path movieCachePath(std::int64_t projectId, std::int64_t versionId) {
    const std::filesystem::path home = thumb::homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "xStudio" / "mikros_shotbrowser" / "movies" / std::to_string(projectId) /
           std::to_string(versionId) / "movie.mp4";
}

/**
 * @brief Writes `bytes` to `path` when the file is not already there.
 * @param path Cache file.
 * @param bytes Movie body.
 * @return True when `path` contains bytes afterwards.
 */
bool writeMovie(const std::filesystem::path &path, const std::string &bytes) {
    if (path.empty() || bytes.empty()) {
        return false;
    }
    if (regularFile(path.string())) {
        return true; ///< This version was downloaded for an earlier open.
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return regularFile(path.string());
}

/**
 * @brief Downloads `url` and returns the body bytes.
 * @param url Signed `sg_uploaded_movie_mp4` address.
 * @return The body, or empty when the request fails.
 */
std::string fetchMovie(const std::string &url) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setTransferTimeout(120000); ///< A movie is larger than a thumbnail.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); ///< Wait in the open thread. The panel actor stays free.
    std::string body;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        body.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    delete reply;
    return body;
}

/**
 * @brief What the panel asked to open, before the disk check.
 */
struct OpenRequest {
    std::int64_t version_id = 0;  ///< ShotGrid version id.
    std::int64_t project_id = 0;  ///< Selected project, used in the movie cache path.
    std::uint64_t token = 0;      ///< Matches the QML request that should be selected.
    bool select = false;          ///< True when the new media should become current.
    std::string name;             ///< Version code, used as the media name.
    std::string playlist_uuid;    ///< xSTUDIO playlist that receives the media.
    std::string frames;           ///< `sg_path_to_frames`.
    std::string frame_range;      ///< Range that bounds the sequence when it is present.
    std::string movie;            ///< `sg_path_to_movie`.
    std::string uploaded_movie;   ///< Signed `sg_uploaded_movie_mp4` URL.
};

/**
 * @brief Paths that survived the disk check, before they are mailed as JSON.
 */
struct CheckedMedia {
    std::int64_t version_id = 0; ///< ShotGrid version id, for the log line.
    std::uint64_t token = 0;     ///< Copied from the request.
    bool select = false;         ///< Copied from the request.
    std::string name;            ///< Media name.
    std::string playlist_uuid;   ///< Playlist actor uuid.
    std::string frames_spec;     ///< Sequence spec accepted by `parse_cli_posix_path`.
    std::string movie_path;      ///< Local movie file. Empty when there is no movie.
};

/**
 * @brief Checks the sequence and the movie, downloading the signed URL when needed.
 * @param request Paths as ShotGrid sent them.
 * @return Sources that can be added. Both paths stay empty when nothing is usable.
 */
CheckedMedia resolveMedia(const OpenRequest &request) {
    const std::string frames = sequenceSpec(request.frames, request.frame_range);
    const bool movieOnDisk = regularFile(request.movie);
    const bool hasUrl = !request.uploaded_movie.empty();
    const SourceChoice choice = chooseSources(!frames.empty(), movieOnDisk, hasUrl);

    CheckedMedia ready;
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
 * @brief Logs whether the open kept the sequence, a local movie, or a downloaded one.
 * @param ready Paths that survived the disk check.
 */
void logOpen(const CheckedMedia &ready) {
    const char *movie = "none";
    if (!ready.movie_path.empty()) {
        movie = ready.movie_path.find("mikros_shotbrowser/movies") == std::string::npos ? "file"
                                                                                          : "url";
    }
    trace::info(
        "open versionId=" + std::to_string(ready.version_id) +
        " frames=" + std::string(ready.frames_spec.empty() ? "no" : "yes") +
        " movie=" + movie);
}

/**
 * @brief Checks the files on a detached thread and mails the paths back to the panel.
 *
 * The download uses a `QEventLoop`, so it must not run on the panel actor.
 * @param panel Panel actor that receives the result. A dead panel is skipped.
 * @param request Paths and the target playlist.
 */
void resolveOpen(caf::weak_actor_ptr panel, OpenRequest request) {
    std::thread([panel = std::move(panel), request = std::move(request)]() mutable {
        try {
            const CheckedMedia ready = resolveMedia(request);
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
            trace::failure(caught);
        }
    }).detach();
}

} // namespace

namespace xstudio::mikros_shotbrowser {

OpenReady
openReady(const utility::JsonStore &payload) {
    OpenReady ready;
    ready.version_id = payload.value("versionId", std::int64_t{0});
    ready.token = payload.value("token", std::uint64_t{0});
    ready.select = payload.value("select", false);
    ready.name = payload.value("name", std::string());
    ready.playlist_uuid = payload.value("playlistUuid", std::string());
    ready.frames_spec = payload.value("framesSpec", std::string());
    ready.movie_path = payload.value("moviePath", std::string());
    return ready;
}

void startOpen(PanelState &state, const std::string &text) {
    if (text.empty()) {
        return;
    }
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(text);
    } catch (const std::exception &) {
        log::error("open request is not json");
        return;
    }
    if (!body.is_object()) {
        return;
    }
    OpenRequest request;
    request.version_id = body.value("versionId", std::int64_t{0});
    request.project_id = state.selected_project_ ? state.selected_project_->id : std::int64_t{0};
    request.token = body.value("token", std::uint64_t{0});
    request.select = body.value("select", false);
    request.name = body.value("name", std::string());
    request.playlist_uuid = body.value("playlistUuid", std::string());
    request.frames = body.value("frames", std::string());
    request.frame_range = body.value("frameRange", std::string());
    request.movie = body.value("movie", std::string());
    request.uploaded_movie = body.value("uploadedMovie", std::string());
    if (request.playlist_uuid.empty()) {
        log::info("open versionId=" + std::to_string(request.version_id) + " playlist=none");
        return;
    }
    resolveOpen(caf::actor_cast<caf::weak_actor_ptr>(state.panel), std::move(request));
}

void placeOpenedMedia(PanelState &state, const OpenReady &ready) {
    if (ready.playlist_uuid.empty() || (ready.frames_spec.empty() && ready.movie_path.empty())) {
        return;
    }
    const utility::Uuid playlistId{ready.playlist_uuid};
    PanelState *held = &state; ///< The reply runs later, on this same actor.
    auto global = state.panel->system().registry().template get<caf::actor>(global_registry);
    if (!global) {
        log::error("open versionId=" + std::to_string(ready.version_id) + " session missing");
        return;
    }
    state.panel->mail(session::session_atom_v)
        .request(global, caf::infinite)
        .then(
            [=](caf::actor session) {
                state.panel->mail(session::get_playlist_atom_v, playlistId)
                    .request(session, caf::infinite)
                    .then(
                        [=](caf::actor playlist) {
                            state.panel->mail(session::media_rate_atom_v)
                                .request(playlist, caf::infinite)
                                .then(
                                    [=](const utility::FrameRate &rate) {
                                        addOpenedSources(*held, ready, playlist, rate);
                                    },
                                    [=](const caf::error &err) {
                                        log::error(
                                            "open versionId=" + std::to_string(ready.version_id) +
                                            " rate " + caf::to_string(err));
                                    });
                        },
                        [=](const caf::error &err) {
                            log::error(
                                "open versionId=" + std::to_string(ready.version_id) +
                                " playlist " + caf::to_string(err));
                        });
            },
            [=](const caf::error &err) {
                log::error(
                    "open versionId=" + std::to_string(ready.version_id) + " session " +
                    caf::to_string(err));
            });
}

void addOpenedSources(PanelState &state, 
    const OpenReady &ready, caf::actor playlist, const utility::FrameRate &rate) {
    utility::UuidActorVector sources;
    utility::Uuid movieSource;
    utility::Uuid framesSource;
    if (!ready.movie_path.empty()) {
        const utility::Uuid uuid = utility::Uuid::generate();
        auto source = state.panel->spawn<media::MediaSourceActor>(
            "Movie", utility::posix_path_to_uri(ready.movie_path), rate, uuid);
        sources.emplace_back(uuid, source); ///< Movie first, so it is the active source.
        movieSource = uuid;
    }
    if (!ready.frames_spec.empty()) {
        try {
            utility::FrameList frames;
            const bool scan = ready.frames_spec.find('=') == std::string::npos;
            const caf::uri uri = utility::parse_cli_posix_path(ready.frames_spec, frames, scan);
            if (!frames.empty()) {
                const utility::Uuid uuid = utility::Uuid::generate();
                auto source =
                    state.panel->spawn<media::MediaSourceActor>("Frames", uri, frames, rate, uuid);
                sources.emplace_back(uuid, source);
                framesSource = uuid;
            }
        } catch (const std::exception &caught) {
            log::failure(caught); ///< The sequence parsed during the check and failed again.
        }
    }
    if (sources.empty()) {
        return;
    }
    const utility::Uuid mediaId = utility::Uuid::generate();
    const std::string name = ready.name.empty() ? "Version" : ready.name;
    auto media = state.panel->spawn<media::MediaActor>(name, mediaId, sources);
    const utility::Uuid active = movieSource.is_null() ? framesSource : movieSource;
    anon_mail(media::current_media_source_atom_v, active).send(media);
    state.panel->mail(playlist::add_media_atom_v, utility::UuidActor(mediaId, media), utility::Uuid())
        .request(playlist, caf::infinite)
        .then(
            [=](const utility::UuidActor &added) {
                if (!ready.select || !state.opened_media_) {
                    return;
                }
                const nlohmann::json done = {
                    {"token", ready.token},
                    {"select", true},
                    {"playlistUuid", ready.playlist_uuid},
                    {"mediaUuid", utility::to_string(added.uuid())}};
                state.opened_media_->set_value(done.dump());
            },
            [=](const caf::error &err) {
                log::error(
                    "open versionId=" + std::to_string(ready.version_id) + " add " +
                    caf::to_string(err));
            });
}


} // namespace xstudio::mikros_shotbrowser
