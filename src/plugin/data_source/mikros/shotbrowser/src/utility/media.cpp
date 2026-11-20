// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/media.cpp
 * @brief Resolve version media paths, open/preview playlists, and the retrieve actors.
 */

#include "utility/media.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "utility/home.hpp"
#include "utility/sequence_path.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/caf_error.hpp"
#include "xstudio/media/media_actor.hpp"
#include "xstudio/playlist/playlist.hpp"
#include "xstudio/session/session.hpp"
#include "xstudio/utility/frame_list.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/json_store.hpp"
#include "xstudio/utility/uuid.hpp"

#include <caf/actor_registry.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

constexpr const char *kMikrosVersionMetaPath = "/metadata/mikros/version";

/**
 * @brief Live ephemeral preview playlist (single-click, session-hidden).
 */
struct PreviewState {
    std::int64_t version_id = 0; ///< ShotGrid version currently in preview. 0 = none.
    utility::UuidActor playlist; ///< Hidden preview playlist actor.
    std::uint64_t generation = 0; ///< Bumped on each preview / clear to drop stale work.
};

PreviewState g_preview; ///< One preview playlist per process for this plugin.

/**
 * @brief Writes the ShotGrid version id on the media actor JSON store.
 * @param mediaActor Media that was just spawned.
 * @param versionId ShotGrid version id.
 */
void stampVersionId(caf::actor mediaActor, std::int64_t versionId) {
    anon_mail(
        json_store::set_json_atom_v,
        utility::Uuid(),
        utility::JsonStore(nlohmann::json{{"id", versionId}}),
        std::string(kMikrosVersionMetaPath))
        .send(mediaActor);
}

/**
 * @brief Finds media in `playlist` whose stamped ShotGrid version id matches.
 * @param panel Panel actor that owns the request.
 * @param playlist Playlist whose media are inspected.
 * @param versionId ShotGrid version id to match.
 * @param then_ok Called with the matching media, or nullopt when none.
 * @param then_err Called when listing media fails.
 */
void findPlaylistMediaByVersionId(
    plugin::StandardPlugin &panel,
    caf::actor playlist,
    std::int64_t versionId,
    std::function<void(std::optional<utility::UuidActor>)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    panel.mail(playlist::get_media_atom_v)
        .request(playlist, caf::infinite)
        .then(
            [&panel, versionId, then_ok = std::move(then_ok)](
                const std::vector<utility::UuidActor> &media) {
                if (media.empty()) {
                    if (then_ok) {
                        then_ok(std::nullopt);
                    }
                    return;
                }
                auto remaining = std::make_shared<std::size_t>(media.size());
                auto found = std::make_shared<std::optional<utility::UuidActor>>();
                for (const utility::UuidActor &ua : media) {
                    panel.mail(
                             json_store::get_json_atom_v,
                             utility::Uuid(),
                             std::string(kMikrosVersionMetaPath))
                        .request(ua.actor(), caf::infinite)
                        .then(
                            [remaining, found, ua, versionId, then_ok](
                                const utility::JsonStore &meta) {
                                if (meta.value("id", std::int64_t{0}) == versionId) {
                                    *found = ua;
                                }
                                if (--(*remaining) == 0 && then_ok) {
                                    then_ok(*found);
                                }
                            },
                            [remaining, found, then_ok](const caf::error &) {
                                if (--(*remaining) == 0 && then_ok) {
                                    then_ok(*found);
                                }
                            });
                }
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                }
            });
}

} // namespace

bool regularFile(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

SourceChoice chooseSources(bool framesOnDisk, bool movieOnDisk, bool hasUploadedUrl) {
    SourceChoice choice;
    choice.frames = framesOnDisk;
    choice.movieFile = movieOnDisk;
    choice.movieUrl = !movieOnDisk && hasUploadedUrl;
    return choice;
}

std::string sequenceSpec(const std::string &path, const std::string &frameRange) {
    const ResolvedFrames resolved = resolveShotGridFrames(path, frameRange);
    if (!resolved.exists || resolved.xstudio_path.empty()) {
        return {};
    }
    return resolved.xstudio_path;
}

std::filesystem::path movieCachePath(std::int64_t projectId, std::int64_t versionId) {
    const std::filesystem::path home = thumb::homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "xStudio" / "mikros_shotbrowser" / "movies" / std::to_string(projectId) /
           std::to_string(versionId) / "movie.mp4";
}

bool writeMovie(const std::filesystem::path &path, const std::string &bytes) {
    if (path.empty() || bytes.empty()) {
        return false;
    }
    if (regularFile(path.string())) {
        return true;
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

std::string fetchMovie(const std::string &url) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setTransferTimeout(120000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    std::string body;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        body.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    delete reply;
    return body;
}

ResolvedVersionPaths resolveVersionPaths(
    const poco::VersionMedia &media,
    std::int64_t projectId,
    const std::string &frameRange) {
    const std::string range = frameRange.empty() ? media.frame_range : frameRange;
    const std::string frames = sequenceSpec(media.frames, range);
    const bool movieOnDisk = regularFile(media.movie);
    const bool hasUrl = !media.uploaded_movie.empty();
    const SourceChoice choice = chooseSources(!frames.empty(), movieOnDisk, hasUrl);

    ResolvedVersionPaths ready;
    if (choice.frames) {
        ready.frames_spec = frames;
    }
    if (choice.movieFile) {
        ready.movie_path = media.movie;
    } else if (choice.movieUrl) {
        const std::filesystem::path cache = movieCachePath(projectId, media.id);
        if (!cache.empty() && !regularFile(cache.string())) {
            writeMovie(cache, fetchMovie(media.uploaded_movie));
        }
        if (regularFile(cache.string())) {
            ready.movie_path = cache.string();
        }
    }
    return ready;
}

/**
 * @brief Makes `playlist` the inspected and viewed media container.
 * @param session Session actor.
 * @param playlist Playlist uuid and actor.
 */
void activatePlaylist(caf::actor session, const utility::UuidActor &playlist) {
    anon_mail(session::active_media_container_atom_v, playlist).send(session);
    anon_mail(session::viewport_active_media_container_atom_v, playlist).send(session);
}

/**
 * @brief Creates `name` or reuses it, activates it, then runs `then_ok`.
 * @param panel Panel actor.
 * @param session Session actor.
 * @param name Non-empty playlist name.
 * @param then_ok Success callback.
 * @param then_err Error callback.
 */
void ensureNamedPlaylist(
    plugin::StandardPlugin &panel,
    caf::actor session,
    const std::string &name,
    std::function<void(utility::UuidActor, std::string)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    panel.mail(session::get_playlist_atom_v, name)
        .request(session, caf::infinite)
        .then(
            [&panel, session, name, then_ok, then_err](caf::actor playlist) {
                if (playlist) {
                    panel.mail(utility::uuid_atom_v)
                        .request(playlist, caf::infinite)
                        .then(
                            [session, name, then_ok, playlist](const utility::Uuid &uuid) {
                                const utility::UuidActor ua(uuid, playlist);
                                activatePlaylist(session, ua);
                                if (then_ok) {
                                    then_ok(ua, name);
                                }
                            },
                            [session, name, then_ok, playlist](const caf::error &) {
                                const utility::UuidActor ua(utility::Uuid(), playlist);
                                activatePlaylist(session, ua);
                                if (then_ok) {
                                    then_ok(ua, name);
                                }
                            });
                    return;
                }
                panel.mail(session::add_playlist_atom_v, name)
                    .request(session, caf::infinite)
                    .then(
                        [session, name, then_ok](
                            const std::pair<utility::Uuid, utility::UuidActor> &created) {
                            activatePlaylist(session, created.second);
                            if (then_ok) {
                                then_ok(created.second, name);
                            }
                        },
                        [then_err](const caf::error &err) {
                            if (then_err) {
                                then_err(err);
                            } else {
                                log::error("create_playlist add " + caf::to_string(err));
                            }
                        });
            },
            [then_err](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("create_playlist get " + caf::to_string(err));
                }
            });
}

void create_playlist(
    plugin::StandardPlugin &panel,
    const std::string &name,
    std::function<void(utility::UuidActor, std::string)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    auto global = panel.system().registry().template get<caf::actor>(global_registry);
    if (!global) {
        log::error("create_playlist session missing");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "session missing"));
        }
        return;
    }
    panel.mail(session::session_atom_v)
        .request(global, caf::infinite)
        .then(
            [&panel, name, then_ok = std::move(then_ok), then_err = std::move(then_err)](
                caf::actor session) {
                if (!name.empty()) {
                    ensureNamedPlaylist(panel, session, name, then_ok, then_err);
                    return;
                }
                panel.mail(utility::name_atom_v, std::string("Playlist {}"), true)
                    .request(session, caf::infinite)
                    .then(
                        [&panel, session, then_ok, then_err](const std::string &nextName) {
                            ensureNamedPlaylist(panel, session, nextName, then_ok, then_err);
                        },
                        [then_err](const caf::error &err) {
                            if (then_err) {
                                then_err(err);
                            } else {
                                log::error(
                                    "create_playlist next name " + caf::to_string(err));
                            }
                        });
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("create_playlist session " + caf::to_string(err));
                }
            });
}

/**
 * @brief Selects a known media uuid as current in `playlist`.
 */
void selectMediaByUuid(
    plugin::StandardPlugin &panel,
    caf::actor playlist,
    const utility::UuidActor &media,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    panel.mail(playlist::selection_actor_atom_v)
        .request(playlist, caf::infinite)
        .then(
            [media, then_ok](caf::actor selection) {
                anon_mail(
                    playlist::select_media_atom_v, utility::UuidVector{media.uuid()})
                    .send(selection);
                if (then_ok) {
                    then_ok(media);
                }
            },
            [then_err](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("selectAddedMedia select " + caf::to_string(err));
                }
            });
}

/**
 * @brief Spawns sources, stamps the version id, and adds media to the playlist (no select).
 */
void placeVersionMedia(
    plugin::StandardPlugin &panel,
    caf::actor playlist,
    const ResolvedVersionPaths &paths,
    const std::string &name,
    std::int64_t versionId,
    const utility::FrameRate &rate,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    utility::UuidActorVector sources;
    utility::Uuid movieSource;
    utility::Uuid framesSource;
    if (!paths.movie_path.empty()) {
        const utility::Uuid uuid = utility::Uuid::generate();
        auto source = panel.spawn<media::MediaSourceActor>(
            "Movie", utility::posix_path_to_uri(paths.movie_path), rate, uuid);
        sources.emplace_back(uuid, source);
        movieSource = uuid;
    }
    if (!paths.frames_spec.empty()) {
        try {
            utility::FrameList frames;
            const bool scan = paths.frames_spec.find('=') == std::string::npos;
            const caf::uri uri =
                utility::parse_cli_posix_path(paths.frames_spec, frames, scan);
            if (!frames.empty()) {
                const utility::Uuid uuid = utility::Uuid::generate();
                auto source = panel.spawn<media::MediaSourceActor>(
                    "Frames", uri, frames, rate, uuid);
                sources.emplace_back(uuid, source);
                framesSource = uuid;
            }
        } catch (const std::exception &caught) {
            log::failure(caught);
        }
    }
    if (sources.empty()) {
        log::error("add_version_to_playlist no sources");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "no media sources"));
        }
        return;
    }
    const utility::Uuid mediaId = utility::Uuid::generate();
    const std::string mediaName = name.empty() ? "Version" : name;
    auto mediaActor = panel.spawn<media::MediaActor>(mediaName, mediaId, sources);
    stampVersionId(mediaActor, versionId); ///< Persist ShotGrid id for later dedupe.
    const utility::Uuid active = movieSource.is_null() ? framesSource : movieSource;
    anon_mail(media::current_media_source_atom_v, active).send(mediaActor);
    panel.mail(playlist::add_media_atom_v, utility::UuidActor(mediaId, mediaActor), utility::Uuid())
        .request(playlist, caf::infinite)
        .then(
            [then_ok = std::move(then_ok)](const utility::UuidActor &added) {
                if (then_ok) {
                    then_ok(added);
                }
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("add_version_to_playlist add " + caf::to_string(err));
                }
            });
}

void add_version_to_playlist(
    plugin::StandardPlugin &panel,
    const utility::UuidActor &playlist,
    const ResolvedVersionPaths &paths,
    const std::string &name,
    std::int64_t versionId,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    if (paths.frames_spec.empty() && paths.movie_path.empty()) {
        log::error("add_version_to_playlist empty paths");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "empty media paths"));
        }
        return;
    }
    if (!playlist.actor()) {
        log::error("add_version_to_playlist playlist missing");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "playlist missing"));
        }
        return;
    }
    findPlaylistMediaByVersionId(
        panel,
        playlist.actor(),
        versionId,
        [&panel,
         playlist,
         paths,
         name,
         versionId,
         then_ok = std::move(then_ok),
         then_err = std::move(then_err)](std::optional<utility::UuidActor> existing) {
            if (existing) {
                if (then_ok) {
                    then_ok(*existing); ///< Already in the playlist; do not add again.
                }
                return;
            }
            panel.mail(session::media_rate_atom_v)
                .request(playlist.actor(), caf::infinite)
                .then(
                    [&panel,
                     playlistActor = playlist.actor(),
                     paths,
                     name,
                     versionId,
                     then_ok = std::move(then_ok),
                     then_err = std::move(then_err)](const utility::FrameRate &rate) {
                        placeVersionMedia(
                            panel,
                            playlistActor,
                            paths,
                            name,
                            versionId,
                            rate,
                            then_ok,
                            then_err);
                    },
                    [then_err = std::move(then_err)](const caf::error &err) {
                        if (then_err) {
                            then_err(err);
                        } else {
                            log::error(
                                "add_version_to_playlist rate " + caf::to_string(err));
                        }
                    });
        },
        then_err);
}

void selectAddedMedia(
    plugin::StandardPlugin &panel,
    const utility::UuidActor &playlist,
    std::int64_t versionId,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    if (!playlist.actor()) {
        log::error("selectAddedMedia playlist missing");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "playlist missing"));
        }
        return;
    }
    findPlaylistMediaByVersionId(
        panel,
        playlist.actor(),
        versionId,
        [&panel, playlist, versionId, then_ok = std::move(then_ok), then_err = std::move(then_err)](
            std::optional<utility::UuidActor> media) {
            if (!media) {
                log::error(
                    "selectAddedMedia missing versionId=" + std::to_string(versionId));
                if (then_err) {
                    then_err(caf::make_error(xstudio_error::error, "media missing"));
                }
                return;
            }
            selectMediaByUuid(panel, playlist.actor(), *media, then_ok, then_err);
        },
        then_err);
}

/**
 * @brief Removes every media item from `playlist`, then runs `then_ok`.
 */
void clearPlaylistMedia(
    plugin::StandardPlugin &panel,
    caf::actor playlist,
    std::function<void()> then_ok,
    std::function<void(const caf::error &)> then_err) {
    panel.mail(playlist::get_media_atom_v)
        .request(playlist, caf::infinite)
        .then(
            [&panel, playlist, then_ok = std::move(then_ok), then_err = std::move(then_err)](
                const std::vector<utility::UuidActor> &media) {
                if (media.empty()) {
                    if (then_ok) {
                        then_ok();
                    }
                    return;
                }
                utility::UuidVector ids;
                ids.reserve(media.size());
                for (const utility::UuidActor &ua : media) {
                    ids.push_back(ua.uuid());
                }
                panel.mail(playlist::remove_media_atom_v, ids)
                    .request(playlist, caf::infinite)
                    .then(
                        [then_ok = std::move(then_ok)](bool) {
                            if (then_ok) {
                                then_ok();
                            }
                        },
                        [then_err = std::move(then_err)](const caf::error &err) {
                            if (then_err) {
                                then_err(err);
                            } else {
                                log::error("clearPlaylistMedia " + caf::to_string(err));
                            }
                        });
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("clearPlaylistMedia list " + caf::to_string(err));
                }
            });
}

/**
 * @brief Resolves session, then runs `then_ok` with it.
 */
void withSession(
    plugin::StandardPlugin &panel,
    std::function<void(caf::actor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    auto global = panel.system().registry().template get<caf::actor>(global_registry);
    if (!global) {
        log::error("withSession session missing");
        if (then_err) {
            then_err(caf::make_error(xstudio_error::error, "session missing"));
        }
        return;
    }
    panel.mail(session::session_atom_v)
        .request(global, caf::infinite)
        .then(
            [then_ok = std::move(then_ok), then_err = std::move(then_err)](caf::actor session) {
                if (!session) {
                    if (then_err) {
                        then_err(caf::make_error(xstudio_error::error, "session missing"));
                    }
                    return;
                }
                if (then_ok) {
                    then_ok(session);
                }
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                } else {
                    log::error("withSession " + caf::to_string(err));
                }
            });
}

/**
 * @brief Shows `playlist` in the viewport only (does not change the inspected list).
 */
void activatePreviewViewport(caf::actor session, const utility::UuidActor &playlist) {
    anon_mail(session::viewport_active_media_container_atom_v, playlist).send(session);
}

/**
 * @brief Gets or creates the hidden `preview` playlist, then runs `then_ok`.
 */
void ensurePreviewPlaylist(
    plugin::StandardPlugin &panel,
    std::function<void(utility::UuidActor)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    if (g_preview.playlist) {
        withSession(
            panel,
            [then_ok = std::move(then_ok)](caf::actor session) {
                activatePreviewViewport(session, g_preview.playlist);
                if (then_ok) {
                    then_ok(g_preview.playlist);
                }
            },
            then_err);
        return;
    }
    withSession(
        panel,
        [&panel, then_ok = std::move(then_ok), then_err = std::move(then_err)](
            caf::actor session) {
            // Hidden: not inserted in the session playlist tree.
            panel.mail(session::add_playlist_atom_v, std::string(kPreviewPlaylistName), true)
                .request(session, caf::infinite)
                .then(
                    [session, then_ok](const utility::UuidActor &created) {
                        g_preview.playlist = created;
                        activatePreviewViewport(session, created);
                        if (then_ok) {
                            then_ok(created);
                        }
                    },
                    [then_err](const caf::error &err) {
                        if (then_err) {
                            then_err(err);
                        } else {
                            log::error(
                                "ensurePreviewPlaylist add " + caf::to_string(err));
                        }
                    });
        },
        then_err);
}

/**
 * @brief Starts playback on the current viewport playhead after a successful select.
 */
void playPreview(plugin::StandardPlugin &panel) {
    auto events =
        panel.system().registry().template get<caf::actor>(global_playhead_events_actor);
    if (!events) {
        return;
    }
    panel.mail(ui::viewport::viewport_playhead_atom_v)
        .request(events, caf::infinite)
        .then(
            [](caf::actor playhead) {
                if (playhead) {
                    anon_mail(playhead::play_atom_v, true).send(playhead);
                }
            },
            [](const caf::error &) {});
}

void clear_preview(plugin::StandardPlugin &panel) {
    ++g_preview.generation; ///< Drop any in-flight preview_version callbacks.
    g_preview.version_id = 0;
    if (!g_preview.playlist) {
        return;
    }
    const utility::UuidActor preview = g_preview.playlist;
    // Keep the hidden playlist: it is linked to the session, so send_exit would
    // propagate user_shutdown and break open_version / the viewport.
    withSession(
        panel,
        [&panel, preview](caf::actor session) {
            panel.mail(session::viewport_active_media_container_atom_v)
                .request(session, caf::infinite)
                .then(
                    [&panel, session, preview](const utility::UuidActor &viewed) {
                        const bool viewing_preview = viewed.uuid() == preview.uuid();
                        auto clear_media = [&panel, preview]() {
                            clearPlaylistMedia(
                                panel, preview.actor(), [] {}, [](const caf::error &) {});
                        };
                        if (!viewing_preview) {
                            clear_media(); ///< Durable open already moved the viewport.
                            return;
                        }
                        panel.mail(session::active_media_container_atom_v)
                            .request(session, caf::infinite)
                            .then(
                                [session, preview, clear_media](
                                    const utility::UuidActor &inspected) {
                                    if (inspected && inspected.uuid() != preview.uuid()) {
                                        anon_mail(
                                            session::viewport_active_media_container_atom_v,
                                            inspected)
                                            .send(session);
                                    }
                                    clear_media();
                                },
                                [clear_media](const caf::error &) { clear_media(); });
                    },
                    [&panel, preview](const caf::error &) {
                        clearPlaylistMedia(
                            panel, preview.actor(), [] {}, [](const caf::error &) {});
                    });
        },
        [](const caf::error &err) {
            log::error("clear_preview session " + caf::to_string(err));
        });
}

void on_preview_viewport_container_changed(
    plugin::StandardPlugin &panel, const utility::UuidActor &viewed) {
    if (!g_preview.playlist) {
        return;
    }
    if (viewed.uuid() == g_preview.playlist.uuid()) {
        return; ///< Still viewing the preview playlist.
    }
    clear_preview(panel);
}

void preview_version(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &name,
    const std::string &frameRange) {
    if (versionId == 0) {
        return;
    }
    if (versionId == g_preview.version_id && g_preview.playlist) {
        return; ///< Same version already previewed.
    }
    const std::uint64_t gen = ++g_preview.generation;
    retrieveVersionMedia(
        panel,
        versionId,
        projectId,
        frameRange,
        [&panel, versionId, name, gen](ResolvedVersionPaths paths) {
            if (gen != g_preview.generation) {
                return;
            }
            ensurePreviewPlaylist(
                panel,
                [&panel, versionId, name, paths = std::move(paths), gen](
                    utility::UuidActor playlist) {
                    if (gen != g_preview.generation) {
                        return;
                    }
                    clearPlaylistMedia(
                        panel,
                        playlist.actor(),
                        [&panel, playlist, versionId, name, paths, gen]() {
                            if (gen != g_preview.generation) {
                                return;
                            }
                            add_version_to_playlist(
                                panel,
                                playlist,
                                paths,
                                name,
                                versionId,
                                [&panel, playlist, versionId, name, paths, gen](
                                    utility::UuidActor) {
                                    if (gen != g_preview.generation) {
                                        return;
                                    }
                                    selectAddedMedia(
                                        panel,
                                        playlist,
                                        versionId,
                                        [&panel, versionId, name, paths](
                                            utility::UuidActor) {
                                            g_preview.version_id = versionId;
                                            playPreview(panel);
                                            log::info(
                                                "preview_version version=" + name + "|" +
                                                std::to_string(versionId) + " frames=" +
                                                paths.frames_spec +
                                                " movie=" + paths.movie_path);
                                        },
                                        [versionId](const caf::error &err) {
                                            log::error(
                                                "preview_version select versionId=" +
                                                std::to_string(versionId) + " " +
                                                caf::to_string(err));
                                        });
                                },
                                [versionId](const caf::error &err) {
                                    log::error(
                                        "preview_version add versionId=" +
                                        std::to_string(versionId) + " " +
                                        caf::to_string(err));
                                });
                        },
                        [versionId](const caf::error &err) {
                            log::error(
                                "preview_version clear media versionId=" +
                                std::to_string(versionId) + " " + caf::to_string(err));
                        });
                },
                [versionId](const caf::error &err) {
                    log::error(
                        "preview_version playlist versionId=" +
                        std::to_string(versionId) + " " + caf::to_string(err));
                });
        },
        [versionId](const caf::error &err) {
            log::error(
                "preview_version retrieve versionId=" + std::to_string(versionId) + " " +
                caf::to_string(err));
        });
}

void open_version(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &name,
    const std::string &playlistName,
    const std::string &frameRange) {
    if (versionId == 0) {
        return;
    }
    clear_preview(panel);
    retrieveVersionMedia(
        panel,
        versionId,
        projectId,
        frameRange,
        [&panel, versionId, name, playlistName](ResolvedVersionPaths paths) {
            create_playlist(
                panel,
                playlistName,
                [&panel, versionId, name, paths = std::move(paths)](
                    utility::UuidActor playlist, std::string resolvedPlaylistName) {
                    add_version_to_playlist(
                        panel,
                        playlist,
                        paths,
                        name,
                        versionId,
                        [&panel, playlist, versionId, name, paths, resolvedPlaylistName](
                            utility::UuidActor) {
                            selectAddedMedia(
                                panel,
                                playlist,
                                versionId,
                                [versionId, name, paths, resolvedPlaylistName](
                                    utility::UuidActor) {
                                    log::info(
                                        "open_version version=" + name + "|" +
                                        std::to_string(versionId) + " frames=" +
                                        paths.frames_spec + " movie=" + paths.movie_path +
                                        " playlist=" + resolvedPlaylistName);
                                },
                                [versionId](const caf::error &err) {
                                    log::error(
                                        "open_version select versionId=" +
                                        std::to_string(versionId) + " " +
                                        caf::to_string(err));
                                });
                        },
                        [versionId](const caf::error &err) {
                            log::error(
                                "open_version add_version versionId=" +
                                std::to_string(versionId) + " " + caf::to_string(err));
                        });
                },
                [versionId](const caf::error &err) {
                    log::error(
                        "open_version create_playlist versionId=" +
                        std::to_string(versionId) + " " + caf::to_string(err));
                });
        },
        [versionId](const caf::error &err) {
            log::error(
                "open_version retrieve versionId=" + std::to_string(versionId) + " " +
                caf::to_string(err));
        });
}

void open_playlist(
    plugin::StandardPlugin &panel,
    std::int64_t playlistId,
    std::int64_t projectId,
    const std::string &playlistName) {
    if (playlistId == 0) {
        return;
    }
    retrieveVersionsForPlaylist(
        panel,
        playlistId,
        [&panel, playlistId, projectId, playlistName](
            std::vector<poco::PlaylistVersionLink> versions) {
            create_playlist(
                panel,
                playlistName,
                [&panel, playlistId, projectId, versions = std::move(versions)](
                    utility::UuidActor playlist, std::string resolvedName) {
                    auto versionsPtr = std::make_shared<std::vector<poco::PlaylistVersionLink>>(
                        std::move(versions));
                    auto index = std::make_shared<std::size_t>(0);
                    auto addNext = std::make_shared<std::function<void()>>();
                    *addNext =
                        [&panel,
                         playlist,
                         projectId,
                         playlistId,
                         versionsPtr,
                         resolvedName = std::move(resolvedName),
                         index,
                         addNext]() {
                            if (*index >= versionsPtr->size()) {
                                log::info(
                                    "open_playlist done playlist=" + resolvedName + "|" +
                                    std::to_string(playlistId) + " count=" +
                                    std::to_string(versionsPtr->size()));
                                return;
                            }
                            const poco::PlaylistVersionLink link = (*versionsPtr)[*index];
                            ++(*index);
                            retrieveVersionMedia(
                                panel,
                                link.id,
                                projectId,
                                std::string(),
                                [&panel, playlist, link, addNext](ResolvedVersionPaths paths) {
                                    add_version_to_playlist(
                                        panel,
                                        playlist,
                                        paths,
                                        link.name,
                                        link.id,
                                        [&panel, playlist, link, addNext](utility::UuidActor) {
                                            selectAddedMedia(
                                                panel,
                                                playlist,
                                                link.id,
                                                [addNext](utility::UuidActor) {
                                                    (*addNext)();
                                                },
                                                [addNext, link](const caf::error &err) {
                                                    log::error(
                                                        "open_playlist select versionId=" +
                                                        std::to_string(link.id) + " " +
                                                        caf::to_string(err));
                                                    (*addNext)();
                                                });
                                        },
                                        [addNext, link](const caf::error &err) {
                                            log::error(
                                                "open_playlist add versionId=" +
                                                std::to_string(link.id) + " " +
                                                caf::to_string(err));
                                            (*addNext)();
                                        });
                                },
                                [addNext, link](const caf::error &err) {
                                    log::error(
                                        "open_playlist retrieve versionId=" +
                                        std::to_string(link.id) + " " + caf::to_string(err));
                                    (*addNext)();
                                });
                        };
                    (*addNext)();
                },
                [playlistId](const caf::error &err) {
                    log::error(
                        "open_playlist create_playlist playlistId=" +
                        std::to_string(playlistId) + " " + caf::to_string(err));
                });
        },
        [playlistId](const caf::error &err) {
            log::error(
                "open_playlist retrieve playlistId=" + std::to_string(playlistId) + " " +
                caf::to_string(err));
        });
}

void retrieveVersionMedia(
    plugin::StandardPlugin &panel,
    std::int64_t versionId,
    std::int64_t projectId,
    const std::string &frameRange,
    std::function<void(ResolvedVersionPaths)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    const utility::JsonStore request({
        {"versionId", versionId},
        {"projectId", projectId},
        {"frameRange", frameRange}});
    auto worker = panel.system().spawn<RetrieveVersionMediaActor, caf::detached>();
    panel.mail(request)
        .request(worker, caf::infinite)
        .then(
            [then_ok = std::move(then_ok)](const utility::JsonStore &payload) {
                if (!then_ok) {
                    return;
                }
                ResolvedVersionPaths paths;
                paths.frames_spec = payload.value("framesSpec", std::string());
                paths.movie_path = payload.value("moviePath", std::string());
                then_ok(std::move(paths));
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                }
            });
}

RetrieveVersionMediaActor::RetrieveVersionMediaActor(caf::actor_config &cfg)
    : caf::event_based_actor(cfg) {}

const char *RetrieveVersionMediaActor::name() const { return "RetrieveVersionMediaActor"; }

caf::behavior RetrieveVersionMediaActor::make_behavior() {
    return {
        [](const utility::JsonStore &request) -> utility::JsonStore {
            ResolvedVersionPaths paths;
            try {
                const std::int64_t versionId = request.value("versionId", std::int64_t{0});
                const std::int64_t projectId = request.value("projectId", std::int64_t{0});
                const std::string frameRange = request.value("frameRange", std::string());
                const poco::VersionMedia media = dal::findVersionMedia(versionId);
                paths = resolveVersionPaths(media, projectId, frameRange);
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed retrieve must not take the panel down.
            }
            return utility::JsonStore(
                {{"framesSpec", paths.frames_spec}, {"moviePath", paths.movie_path}});
        },
    };
}

void retrieveVersionsForPlaylist(
    plugin::StandardPlugin &panel,
    std::int64_t playlistId,
    std::function<void(std::vector<poco::PlaylistVersionLink>)> then_ok,
    std::function<void(const caf::error &)> then_err) {
    const utility::JsonStore request(nlohmann::json{{"playlistId", playlistId}});
    auto worker = panel.system().spawn<RetrieveVersionsForPlaylistActor, caf::detached>();
    panel.mail(request)
        .request(worker, caf::infinite)
        .then(
            [then_ok = std::move(then_ok)](const utility::JsonStore &payload) {
                if (!then_ok) {
                    return;
                }
                std::vector<poco::PlaylistVersionLink> versions;
                const auto &rows = payload.value("versions", nlohmann::json::array());
                if (rows.is_array()) {
                    versions.reserve(rows.size());
                    for (const auto &row : rows) {
                        if (!row.is_object()) {
                            continue;
                        }
                        poco::PlaylistVersionLink link;
                        link.id = row.value("id", std::int64_t{0});
                        link.name = row.value("name", std::string());
                        versions.push_back(std::move(link));
                    }
                }
                then_ok(std::move(versions));
            },
            [then_err = std::move(then_err)](const caf::error &err) {
                if (then_err) {
                    then_err(err);
                }
            });
}

RetrieveVersionsForPlaylistActor::RetrieveVersionsForPlaylistActor(caf::actor_config &cfg)
    : caf::event_based_actor(cfg) {}

const char *RetrieveVersionsForPlaylistActor::name() const {
    return "RetrieveVersionsForPlaylistActor";
}

caf::behavior RetrieveVersionsForPlaylistActor::make_behavior() {
    return {
        [](const utility::JsonStore &request) -> utility::JsonStore {
            nlohmann::json versions = nlohmann::json::array();
            try {
                const std::int64_t playlistId = request.value("playlistId", std::int64_t{0});
                const std::vector<poco::PlaylistVersionLink> links =
                    dal::findPlaylistVersionLinks(playlistId);
                for (const poco::PlaylistVersionLink &link : links) {
                    versions.push_back({{"id", link.id}, {"name", link.name}});
                }
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed retrieve must not take the panel down.
            }
            return utility::JsonStore(nlohmann::json{{"versions", std::move(versions)}});
        },
    };
}

} // namespace xstudio::mikros_shotbrowser
