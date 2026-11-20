// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.cpp
 * @brief Version list: page load, JSON cards, and open media.
 */

#include "viewModel/version_list.hpp"

#include "model/dal/dal.hpp"
#include "utility/home.hpp"
#include "utility/sequence_path.hpp"
#include "utility/thumbnails.hpp"

#include "xstudio/media/media_actor.hpp"
#include "xstudio/utility/frame_list.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/uuid.hpp"

#include <caf/actor_registry.hpp>
#include <caf/actor_system.hpp>
#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

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
 * ShotGrid `%04d` / `####` paths are converted via libfileseq into `{:04d}` specs.
 * @param path ShotGrid sequence path (`%04d`, `#`, `@`, or `{:`).
 * @param frameRange Version frame range. Empty keeps the on-disk fileseq range.
 * @return The xSTUDIO spec, or empty when no frame exists on this site.
 */
std::string sequenceSpec(const std::string &path, const std::string &frameRange) {
    const ResolvedFrames resolved = resolveShotGridFrames(path, frameRange);
    if (!resolved.exists || resolved.xstudio_path.empty()) {
        return {};
    }
    if (resolved.xstudio_path != path) {
        trace::info(
            "frames fileseq '" + path + "' -> '" + resolved.xstudio_path + "'");
    }
    return resolved.xstudio_path;
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
        movie = ready.movie_path.find("mikros_shotbrowser/movies") == std::string::npos
                    ? "file"
                    : "signedurl";
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
            const poco::VersionMedia media = dal::findVersionMedia(request.version_id);
            request.frames = media.frames; ///< Media paths stay off the list find.
            request.movie = media.movie;
            request.uploaded_movie = media.uploaded_movie;
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

using CatalogView = caf::const_typed_message_view<
    data_source::put_data_atom,
    std::uint64_t,
    std::string,
    std::int64_t,
    bool,
    std::int64_t,
    bool,
    std::int64_t,
    bool,
    std::int64_t,
    bool,
    std::int64_t,
    bool,
    utility::JsonStore>;

/**
 * @brief Routes a `version` page or `version_count` to `VersionList`.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class VersionPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the list that owns the page and footer update.
     * @param list Version list on the panel actor.
     */
    explicit VersionPageHandler(VersionList *list) : list_(list) {}

    /**
     * @brief Forwards a version page or count, or declines the message.
     * @param visitor Receives an empty result when the page was ours.
     * @param message Worker page.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        CatalogView view{message};
        if (!view) {
            return false;
        }
        const std::string &catalog = caf::get<2>(view);
        if (catalog != "version" && catalog != "version_count") {
            return false; ///< Another catalog: the next handler continues.
        }
        if (catalog == "version_count") {
            list_->updateVersionFooter(caf::get<1>(view), caf::get<13>(view));
        } else {
            list_->updateVersionList(caf::get<1>(view), caf::get<12>(view), caf::get<13>(view));
        }
        visitor(caf::unit);
        return true;
    }

  private:
    VersionList *list_ = nullptr; ///< Receives version pages and footer counts.
};


/**
 * @brief Routes a `version_thumbs` batch to `VersionList::applyCardImages`.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class VersionThumbHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the list that owns the cards.
     * @param list Version list on the panel actor.
     */
    explicit VersionThumbHandler(VersionList *list) : list_(list) {}

    /**
     * @brief Applies a thumb batch, or declines the message.
     * @param visitor Receives an empty result when the mail was ours.
     * @param message Thumb batch mail.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        CatalogView view{message};
        if (!view) {
            return false;
        }
        if (caf::get<2>(view) != "version_thumbs") {
            return false; ///< Another catalog: the next handler continues.
        }
        const std::int64_t projectId = caf::get<3>(view);
        const utility::JsonStore &payload = caf::get<13>(view);
        rememberThumbs(projectId, payload); ///< Playlist can reuse these URLs via thumbFor.
        if (dal::selectedProject() == projectId) {
            list_->applyCardImages(payload);
        }
        visitor(caf::unit);
        return true;
    }

  private:
    VersionList *list_ = nullptr; ///< Receives ready thumb URL batches.
};

/**
 * @brief Routes an `open_media` result to `VersionList::openMedia`.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class OpenPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the list that opens media.
     * @param list Version list on the panel actor.
     */
    explicit OpenPageHandler(VersionList *list) : list_(list) {}

    /**
     * @brief Forwards an open result, or declines the message.
     * @param visitor Receives an empty result when the page was ours.
     * @param message Worker page.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        CatalogView view{message};
        if (!view) {
            return false;
        }
        if (caf::get<2>(view) != "open_media") {
            return false; ///< Another catalog: the next handler continues.
        }
        list_->openMedia(caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    VersionList *list_ = nullptr; ///< Receives open-media results.
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

VersionList::VersionList(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    initQmlAttributes(panel);
}

void VersionList::initQmlAttributes(plugin::StandardPlugin &panel) {
    version_cards_ =
        panel.add_json_attribute("VersionCards", "VersionCards", nlohmann::json::array());
    version_cards_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    footer_ = panel.add_string_attribute("VersionListFooter", "VersionListFooter", "");
    footer_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    versions_loaded_ = panel.add_boolean_attribute("VersionsLoaded", "VersionsLoaded", false);
    versions_loaded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    load_more_versions_ =
        panel.add_string_attribute("LoadMoreVersions", "LoadMoreVersions", "");
    load_more_versions_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    version_search_ = panel.add_string_attribute("VersionSearch", "VersionSearch", "");
    version_search_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    open_version_ = panel.add_string_attribute("OpenVersion", "OpenVersion", "");
    open_version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    opened_media_ = panel.add_string_attribute("OpenedMedia", "OpenedMedia", "");
    opened_media_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

std::optional<std::string> VersionList::activeSearchQuery() const {
    if (search_query_.empty()) {
        return std::nullopt;
    }
    return search_query_;
}

// ---------------------------------------------------------------------------
// Version list. The panel clears first; unknown rows become cards and publish JSON.
// ---------------------------------------------------------------------------

void VersionList::loadVersionsAsync() {
    if (dal::selectedProject() == -1) {
        return; ///< No project: leave whatever clearVersions already emptied.
    }
    next_cursor_.reset(); ///< A new first page replaces any pending continuation.
    more_incoming_ = false;
    const std::uint64_t requestId = ++(*version_request_id_); ///< Stop the version fetch already running.
    const std::int64_t projectId = dal::selectedProject();
    const VersionParents parents = readVersionParents();
    const std::optional<std::string> nameOrId = activeSearchQuery();
    CatalogLoadRequest request = makeCatalogRequest(
        &panel_, CatalogKind::Version, requestId, projectId);
    request.sequence = parents.sequenceId;
    request.shot = parents.shotId;
    request.asset = parents.assetId;
    request.name_or_id = nameOrId;
    request.generation = version_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(request));
    CatalogLoadRequest counts =
        makeCatalogRequest(&panel_, CatalogKind::VersionCount, requestId, projectId);
    counts.sequence = parents.sequenceId;
    counts.shot = parents.shotId;
    counts.asset = parents.assetId;
    counts.name_or_id = nameOrId;
    counts.generation = version_request_id_; ///< Drop a stale footer with the pages.
    launchCatalogLoad(panel_.system(), std::move(counts));
}

void VersionList::loadVersionsFromNameOrIdAsync(const std::string &text) {
    search_query_ = text; ///< Empty clears the filter; the DAL trims before Where.
    loadVersionsAsync();
}

void VersionList::loadMoreVersionsAsync() {
    if (dal::selectedProject() == -1 || !next_cursor_ || more_incoming_) {
        return; ///< Nothing left to fetch, or a more-page is already on the way.
    }
    more_incoming_ = true;
    const std::uint64_t requestId = version_request_id_->load(); ///< Same generation as the first page.
    const std::int64_t projectId = dal::selectedProject();
    const VersionParents parents = readVersionParents();
    CatalogLoadRequest request =
        makeCatalogRequest(&panel_, CatalogKind::Version, requestId, projectId);
    request.sequence = parents.sequenceId;
    request.shot = parents.shotId;
    request.asset = parents.assetId;
    request.cursor = next_cursor_;
    request.name_or_id = activeSearchQuery();
    request.generation = version_request_id_; ///< Drop the page when clearVersions moves the id.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void VersionList::clearVersions() {
    ++(*version_request_id_); ///< Drop pages that still belong to the previous selection.
    next_cursor_.reset();
    more_incoming_ = false;
    versions_.clear();
    clearFooterValue(); ///< Clear until the next count arrives.
    sendVersionCardsToQml();
    clearVersionsLoadedFlag();
}

namespace {

/**
 * @brief Formats the version-list footer as loaded cards over the server total.
 * @param loaded Cards currently held by the list.
 * @param total Match count from the server, or -1 when the count has not arrived.
 * @return Footer text for debug (`N / T versions`), or `No versions` when total is zero.
 */
std::string formatVersionFooter(std::size_t loaded, int total) {
    if (total == 0) {
        return "No versions";
    }
    if (total < 0) {
        return std::to_string(loaded) + " loaded";
    }
    return std::to_string(loaded) + " / " + std::to_string(total) + " versions";
}

} // namespace

void VersionList::updateVersionFooter(
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != version_request_id_->load()) {
        trace::drop("version_count", requestId); ///< clearVersions bumped the id on selection change.
        return;
    }
    version_total_ = payload.value("versions", 0);
    refreshFooterValue();
}

void VersionList::refreshFooterValue() {
    if (!footer_) {
        return;
    }
    footer_->set_value(formatVersionFooter(versions_.size(), version_total_));
}

void VersionList::clearFooterValue() {
    version_total_ = -1;
    if (footer_) {
        footer_->set_value("");
    }
}

void VersionList::clearVersionsLoadedFlag() {
    if (versions_loaded_) {
        versions_loaded_->set_value(false); ///< An empty list is a load in progress, not a result.
    }
}

void VersionList::updateVersionList(
    std::uint64_t requestId, bool /*append*/, const utility::JsonStore &payload) {
    if (requestId != version_request_id_->load()) {
        trace::drop("version", requestId); ///< clearVersions bumped the id on project/parents change.
        return;
    }
    more_incoming_ = false; ///< This page arrived; another more-request may start.
    const utility::JsonStore rowsJson =
        payload.contains("rows") ? utility::JsonStore(payload.at("rows")) : payload;
    if (payload.contains("next_cursor") && payload.at("next_cursor").is_string()) {
        next_cursor_ = payload.at("next_cursor").get<std::string>();
    } else {
        next_cursor_.reset(); ///< Last page, or a payload without a continuation.
    }
    const std::vector<poco::VersionListElement> rows =
        rowsFromJson<poco::VersionListElement>(rowsJson);
    const std::vector<poco::VersionListElement> newRows = findUnknownVersionRows(rows);
    appendVersions(newRows);
    sendVersionCardsToQml();
    refreshFooterValue(); ///< Show how many cards are loaded versus the server total.
    markVersionsLoaded();
}

std::vector<poco::VersionListElement> VersionList::findUnknownVersionRows(
    const std::vector<poco::VersionListElement> &rows) const {
    std::unordered_set<std::int64_t> knownIds;
    knownIds.reserve(versions_.size());
    for (const std::unique_ptr<VersionCard> &card : versions_) {
        knownIds.insert(card->id());
    }
    std::vector<poco::VersionListElement> newRows;
    newRows.reserve(rows.size());
    for (const poco::VersionListElement &row : rows) {
        if (knownIds.insert(row.id).second) {
            newRows.push_back(row); ///< Skip a version already stored as a card.
        }
    }
    return newRows;
}

void VersionList::appendVersions(const std::vector<poco::VersionListElement> &newRows) {
    if (newRows.empty()) {
        return;
    }
    const std::int64_t projectId = dal::selectedProject();
    std::vector<std::int64_t> versionIds;
    versionIds.reserve(newRows.size());
    versions_.reserve(versions_.size() + newRows.size());
    for (const poco::VersionListElement &row : newRows) {
        versions_.push_back(std::make_unique<VersionCard>(row, row.name));
        versionIds.push_back(row.id);
    }
    scheduleVersionThumbs(projectId, std::move(versionIds));
}

void VersionList::sendVersionCardsToQml() {
    if (!version_cards_) {
        return;
    }
    nlohmann::json rows = nlohmann::json::array();
    for (const std::unique_ptr<VersionCard> &card : versions_) {
        rows.push_back(card->toJson());
    }
    version_cards_->set_value(rows); ///< The QML ListView reads this attribute.
}

void VersionList::scheduleVersionThumbs(
    std::int64_t projectId, std::vector<std::int64_t> versionIds) {
    if (projectId == -1 || versionIds.empty()) {
        return;
    }
    const caf::weak_actor_ptr panel = caf::actor_cast<caf::weak_actor_ptr>(&panel_);
    scheduleThumbs(
        projectId,
        std::move(versionIds),
        [panel, projectId](std::vector<std::pair<std::int64_t, std::string>> ready) {
            if (ready.empty()) {
                return;
            }
            nlohmann::json body = nlohmann::json::array();
            for (auto &[id, fileUrl] : ready) {
                body.push_back({{"versionId", id}, {"image", std::move(fileUrl)}});
            }
            sendCatalog(
                panel,
                0,
                "version_thumbs",
                projectId,
                {},
                false,
                utility::JsonStore(std::move(body)));
        });
}

void VersionList::applyCardImages(const utility::JsonStore &payload) {
    if (!payload.is_array() || payload.empty()) {
        return;
    }
    bool changed = false;
    for (const auto &entry : payload) {
        if (!entry.contains("versionId") || !entry.contains("image")) {
            continue;
        }
        const std::int64_t id = entry["versionId"].get<std::int64_t>();
        const std::string image = entry["image"].get<std::string>();
        if (image.empty()) {
            continue;
        }
        for (std::unique_ptr<VersionCard> &card : versions_) {
            if (card->id() != id) {
                continue;
            }
            card->setImage(image);
            changed = true;
            break;
        }
    }
    if (changed) {
        sendVersionCardsToQml(); ///< One republish for the whole batch.
    }
}

void VersionList::markVersionsLoaded() {
    if (versions_loaded_) {
        versions_loaded_->set_value(true); ///< Zero rows is still a finished load.
    }
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the list.
// ---------------------------------------------------------------------------

OpenReady openReady(const utility::JsonStore &payload);

caf::message_handler VersionList::message_handler_extensions() {
    caf::detail::behavior_impl::pointer pages = caf::make_counted<VersionPageHandler>(this);
    caf::detail::behavior_impl::pointer thumbs = caf::make_counted<VersionThumbHandler>(this);
    caf::detail::behavior_impl::pointer opened = caf::make_counted<OpenPageHandler>(this);
    return caf::message_handler{std::move(pages)}
        .or_else(caf::message_handler{std::move(thumbs)})
        .or_else(caf::message_handler{std::move(opened)});
}

void VersionList::openMedia(const utility::JsonStore &payload) {
    placeOpenedMedia(openReady(payload));
}

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

void VersionList::startOpen(const std::string &text) {
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
    const std::int64_t projectId = dal::selectedProject();
    request.project_id = projectId == -1 ? std::int64_t{0} : projectId;
    request.token = body.value("token", std::uint64_t{0});
    request.select = body.value("select", false);
    request.name = body.value("name", std::string());
    request.playlist_uuid = body.value("playlistUuid", std::string());
    request.frame_range = body.value("frameRange", std::string());
    if (request.playlist_uuid.empty()) {
        log::info("open versionId=" + std::to_string(request.version_id) + " playlist=none");
        return;
    }
    resolveOpen(caf::actor_cast<caf::weak_actor_ptr>(&panel_), std::move(request));
}

void VersionList::placeOpenedMedia(const OpenReady &ready) {
    if (ready.playlist_uuid.empty() || (ready.frames_spec.empty() && ready.movie_path.empty())) {
        return;
    }
    const utility::Uuid playlistId{ready.playlist_uuid};
    VersionList *held = this; ///< The reply runs later, on this same actor.
    auto global = panel_.system().registry().template get<caf::actor>(global_registry);
    if (!global) {
        log::error("open versionId=" + std::to_string(ready.version_id) + " session missing");
        return;
    }
    panel_.mail(session::session_atom_v)
        .request(global, caf::infinite)
        .then(
            [=](caf::actor session) {
                held->panel_.mail(session::get_playlist_atom_v, playlistId)
                    .request(session, caf::infinite)
                    .then(
                        [=](caf::actor playlist) {
                            held->panel_.mail(session::media_rate_atom_v)
                                .request(playlist, caf::infinite)
                                .then(
                                    [=](const utility::FrameRate &rate) {
                                        held->addOpenedSources(ready, playlist, rate);
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

void VersionList::addOpenedSources(
    const OpenReady &ready, caf::actor playlist, const utility::FrameRate &rate) {
    utility::UuidActorVector sources;
    utility::Uuid movieSource;
    utility::Uuid framesSource;
    if (!ready.movie_path.empty()) {
        const utility::Uuid uuid = utility::Uuid::generate();
        auto source = panel_.spawn<media::MediaSourceActor>(
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
                auto source = panel_.spawn<media::MediaSourceActor>(
                    "Frames", uri, frames, rate, uuid);
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
    auto media = panel_.spawn<media::MediaActor>(name, mediaId, sources);
    const utility::Uuid active = movieSource.is_null() ? framesSource : movieSource;
    anon_mail(media::current_media_source_atom_v, active).send(media);
    panel_.mail(playlist::add_media_atom_v, utility::UuidActor(mediaId, media), utility::Uuid())
        .request(playlist, caf::infinite)
        .then(
            [this, ready](const utility::UuidActor &added) {
                if (!ready.select || !opened_media_) {
                    return;
                }
                const nlohmann::json done = {
                    {"token", ready.token},
                    {"select", true},
                    {"playlistUuid", ready.playlist_uuid},
                    {"mediaUuid", utility::to_string(added.uuid())}};
                opened_media_->set_value(done.dump());
            },
            [=](const caf::error &err) {
                log::error(
                    "open versionId=" + std::to_string(ready.version_id) + " add " +
                    caf::to_string(err));
            });
}

void VersionList::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return;
    }
    if (load_more_versions_ && attr_uuid == load_more_versions_->uuid()) {
        loadMoreVersionsAsync(); ///< QML bumped the attribute near the bottom of the list.
        return;
    }
    if (version_search_ && attr_uuid == version_search_->uuid()) {
        clearVersions(); ///< Drop the previous filter result before the new first page.
        loadVersionsFromNameOrIdAsync(version_search_->value());
        return;
    }
    if (!open_version_ || attr_uuid != open_version_->uuid()) {
        return; ///< Only a new open request starts a check.
    }
    startOpen(open_version_->value());
}

} // namespace xstudio::mikros_shotbrowser
