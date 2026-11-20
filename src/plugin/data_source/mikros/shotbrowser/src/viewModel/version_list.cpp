// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.cpp
 * @brief Version list: page load and JSON cards.
 */

#include "viewModel/version_list.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "utility/media.hpp"
#include "utility/thumbnails.hpp"

#include <caf/actor_cast.hpp>
#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <unordered_set>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

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
 * @brief Routes a `version_transcoded` batch to `VersionList::applyCardTranscoded`.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class VersionTranscodedHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the list that owns the cards.
     * @param list Version list on the panel actor.
     */
    explicit VersionTranscodedHandler(VersionList *list) : list_(list) {}

    /**
     * @brief Applies a signed-URL batch, or declines the message.
     * @param visitor Receives an empty result when the mail was ours.
     * @param message Transcoded batch mail.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        CatalogView view{message};
        if (!view) {
            return false;
        }
        if (caf::get<2>(view) != "version_transcoded") {
            return false; ///< Another catalog: the next handler continues.
        }
        const std::uint64_t requestId = caf::get<1>(view);
        if (requestId != list_->version_request_id_->load()) {
            visitor(caf::unit);
            return true; ///< clearVersions bumped the id; drop the stale batch.
        }
        const std::int64_t projectId = caf::get<3>(view);
        if (dal::selectedProject() == projectId) {
            list_->applyCardTranscoded(caf::get<13>(view));
        }
        visitor(caf::unit);
        return true;
    }

  private:
    VersionList *list_ = nullptr; ///< Receives ready signed-URL batches.
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
    preview_version_ = panel.add_string_attribute("PreviewVersion", "PreviewVersion", "");
    preview_version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
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
    // Timing test: cards only (no thumbs / media).
    // scheduleVersionThumbs(projectId, std::move(versionIds));
    // scheduleVersionTranscoded(projectId, newRows);
    (void)projectId;
    (void)versionIds;
}

VersionCard *VersionList::findCard(std::int64_t versionId) const {
    for (const std::unique_ptr<VersionCard> &card : versions_) {
        if (card->id() == versionId) {
            return card.get();
        }
    }
    return nullptr;
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

void VersionList::scheduleVersionTranscoded(
    std::int64_t projectId, const std::vector<poco::VersionListElement> &rows) {
    if (projectId == -1 || rows.empty()) {
        return;
    }
    const caf::weak_actor_ptr panel = caf::actor_cast<caf::weak_actor_ptr>(&panel_);
    const std::uint64_t requestId = version_request_id_->load();
    scheduleVersionTranscodedResolve(
        projectId,
        rows,
        [panel, projectId, requestId](std::vector<poco::VersionTranscodedMedia> ready) {
            if (ready.empty()) {
                return;
            }
            nlohmann::json body = nlohmann::json::array();
            for (poco::VersionTranscodedMedia &row : ready) {
                body.push_back(
                    {{"versionId", row.id},
                     {"uploadedMovie", std::move(row.uploaded_movie)}});
            }
            sendCatalog(
                panel,
                requestId,
                "version_transcoded",
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

void VersionList::applyCardTranscoded(const utility::JsonStore &payload) {
    if (!payload.is_array() || payload.empty()) {
        return;
    }
    bool changed = false;
    for (const auto &entry : payload) {
        if (!entry.contains("versionId") || !entry.contains("uploadedMovie")) {
            continue;
        }
        const std::int64_t id = entry["versionId"].get<std::int64_t>();
        const std::string uploaded = entry["uploadedMovie"].get<std::string>();
        if (uploaded.empty()) {
            continue;
        }
        VersionCard *card = findCard(id);
        if (!card) {
            continue;
        }
        poco::VersionTranscodedMedia media;
        media.id = id;
        media.uploaded_movie = uploaded;
        card->setTranscoded(media);
        changed = true;
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

caf::message_handler VersionList::message_handler_extensions() {
    caf::detail::behavior_impl::pointer pages = caf::make_counted<VersionPageHandler>(this);
    caf::detail::behavior_impl::pointer thumbs = caf::make_counted<VersionThumbHandler>(this);
    caf::detail::behavior_impl::pointer transcoded =
        caf::make_counted<VersionTranscodedHandler>(this);
    return caf::message_handler{std::move(pages)}
        .or_else(caf::message_handler{std::move(thumbs)})
        .or_else(caf::message_handler{std::move(transcoded)});
}

void VersionList::open_version(const std::string &text) {
    if (text.empty()) {
        return;
    }
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(text);
    } catch (const std::exception &) {
        log::error("open_version request is not json");
        return;
    }
    if (!body.is_object()) {
        return;
    }
    const std::int64_t versionId = body.value("versionId", std::int64_t{0});
    const std::int64_t projectId =
        dal::selectedProject() == -1 ? std::int64_t{0} : dal::selectedProject();
    std::string frameRange = body.value("frameRange", std::string());
    std::string frames;
    std::string movie;
    std::string uploadedMovie;
    bool pathsKnown = false;
    if (const VersionCard *card = findCard(versionId)) {
        if (frameRange.empty()) {
            frameRange = card->frameRange();
        }
        frames = card->frames();
        movie = card->movie();
        uploadedMovie = card->uploadedMovie();
        pathsKnown = true; ///< Card already carries list paths (possibly empty).
    }
    mikros_shotbrowser::open_version(
        panel_,
        utility::JsonStore(
            {{"versionId", versionId},
             {"projectId", projectId},
             {"name", body.value("name", std::string())},
             {"playlistName", body.value("playlistName", std::string())},
             {"frameRange", frameRange},
             {"frames", frames},
             {"movie", movie},
             {"uploadedMovie", uploadedMovie},
             {"pathsKnown", pathsKnown}}));
}

void VersionList::preview_version(const std::string &text) {
    if (text.empty()) {
        return;
    }
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(text);
    } catch (const std::exception &) {
        log::error("preview_version request is not json");
        return;
    }
    if (!body.is_object()) {
        return;
    }
    const std::int64_t versionId = body.value("versionId", std::int64_t{0});
    const std::int64_t projectId =
        dal::selectedProject() == -1 ? std::int64_t{0} : dal::selectedProject();
    std::string frameRange = body.value("frameRange", std::string());
    std::string frames;
    std::string movie;
    std::string uploadedMovie;
    bool pathsKnown = false;
    if (const VersionCard *card = findCard(versionId)) {
        if (frameRange.empty()) {
            frameRange = card->frameRange();
        }
        frames = card->frames();
        movie = card->movie();
        uploadedMovie = card->uploadedMovie();
        pathsKnown = true; ///< Card already carries list paths (possibly empty).
    }
    mikros_shotbrowser::preview_version(
        panel_,
        utility::JsonStore(
            {{"versionId", versionId},
             {"projectId", projectId},
             {"name", body.value("name", std::string())},
             {"frameRange", frameRange},
             {"frames", frames},
             {"movie", movie},
             {"uploadedMovie", uploadedMovie},
             {"pathsKnown", pathsKnown}}));
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
    if (open_version_ && attr_uuid == open_version_->uuid()) {
        open_version(open_version_->value());
        return;
    }
    if (preview_version_ && attr_uuid == preview_version_->uuid()) {
        preview_version(preview_version_->value());
    }
}

} // namespace xstudio::mikros_shotbrowser
