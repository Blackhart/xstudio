// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_tree.cpp
 * @brief Playlist tree: name pages, unfolded versions, and the thumbnail stamp.
 */

#include "viewModel/playlist_tree.hpp"
#include "utility/thumbnails.hpp"

#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <QCollator>
#include <QString>

#include <algorithm>
#include <functional>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

/**
 * @brief Builds the JSON one playlist version row reads.
 * @param row Version whose display fields are copied.
 * @param label Unique name shown in the title.
 * @param image Thumbnail path already resolved, or empty until the thumb arrives.
 * @return Object with the card keys.
 */
nlohmann::json versionFacts(
    const poco::Version &row, const std::string &label, const std::string &image) {
    return {
        {"name", label},
        {"id", row.id},
        {"image", image},
        {"author", row.author},
        {"createdAt", row.created_at},
        {"frameRange", row.frame_range},
        {"description", row.description},
        {"step", row.step},
        {"status", row.status},
        {"artisticStatus", row.artistic_status},
        {"sites", row.sites},
        {"hasNotes", row.has_notes},
        {"syncedToClient", row.synced_to_client},
        {"frames", row.frames},
        {"movie", row.movie},
        {"uploadedMovie", row.uploaded_movie}};
}


using PlaylistPage = std::function<void(
    std::uint64_t,
    const std::string &,
    std::int64_t,
    bool,
    std::int64_t,
    const utility::JsonStore &)>;

/**
 * @brief Applies a playlist page and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class PlaylistPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one playlist page.
     * @param apply Called on the panel actor for a current playlist page.
     */
    explicit PlaylistPageHandler(PlaylistPage apply) : apply_(std::move(apply)) {}

    /**
     * @brief Runs the playlist lambda, or declines the message.
     * @param visitor Receives an empty result when the page was ours.
     * @param message Worker page.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        using view_type = caf::const_typed_message_view<
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
        view_type view{message};
        if (!view) {
            return false;
        }
        const std::string catalog = caf::get<2>(view);
        if (catalog != "playlist_tree" && catalog != "playlist_versions") {
            return false; ///< Another catalog: the next handler continues.
        }
        apply_(
            caf::get<1>(view),
            catalog,
            caf::get<3>(view),
            caf::get<12>(view),
            caf::get<11>(view),
            caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    PlaylistPage apply_; ///< Lambda stored by `message_handler_extensions`.
};

/**
 * @brief Orders one playlist node before another by name.
 * @param left First playlist node.
 * @param right Second playlist node.
 * @return True when `left` belongs before `right`.
 */
bool playlistNameAlphabeticOrderAlgorithm(const nlohmann::json &left, const nlohmann::json &right) {
    static const QCollator order = [] {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive); ///< `a` comes before `B`.
        return collator;
    }();
    const int nameOrder = order.compare(
        QString::fromStdString(left.value("name", std::string())),
        QString::fromStdString(right.value("name", std::string())));
    if (nameOrder != 0) {
        return nameOrder < 0; ///< Different names: the earlier name comes first.
    }
    return left.value("id", std::int64_t{0}) < right.value("id", std::int64_t{0});
}

/**
 * @brief Orders one version row before another by name.
 * @param left First version row.
 * @param right Second version row.
 * @return True when `left` belongs before `right`.
 */
bool versionNameAlphabeticOrderAlgorithm(const nlohmann::json &left, const nlohmann::json &right) {
    static const QCollator order = [] {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive); ///< `a` comes before `B`.
        return collator;
    }();
    const int nameOrder = order.compare(
        QString::fromStdString(left.value("name", std::string())),
        QString::fromStdString(right.value("name", std::string())));
    if (nameOrder != 0) {
        return nameOrder < 0; ///< Different names: the earlier name comes first.
    }
    return left.value("id", std::int64_t{0}) < right.value("id", std::int64_t{0});
}

/**
 * @brief Playlist node already shown, so a later page can keep its versions.
 * @param tree Tree currently displayed.
 * @param playlistId Playlist the new page names.
 * @return The node, or empty when this playlist is new.
 */
std::optional<nlohmann::json> shownPlaylist(const nlohmann::json &tree, std::int64_t playlistId) {
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        return std::optional<nlohmann::json>{nlohmann::json(node)};
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PlaylistTree::PlaylistTree(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    initQmlAttributes(panel);
}

void PlaylistTree::initQmlAttributes(plugin::StandardPlugin &panel) {
    tree_ = panel.add_json_attribute("PlaylistTree", "PlaylistTree", nlohmann::json::array());
    tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    expanded_ = panel.add_string_attribute("ExpandedPlaylist", "ExpandedPlaylist", "");
    expanded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

// ---------------------------------------------------------------------------
// Unfold. A name loads that playlist's versions. Selecting nothing else does.
// ---------------------------------------------------------------------------

void PlaylistTree::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value || !expanded_ || attr_uuid != expanded_->uuid()) {
        return; ///< Only an unfold loads versions.
    }
    expandPlaylist();
}

void PlaylistTree::expandPlaylist() {
    const std::string name = expanded_->value();
    if (name.empty() || dal::selectedProject() == -1) {
        return; ///< Closing the branch loads nothing.
    }
    const nlohmann::json tree = treeValue();
    if (!tree.is_array()) {
        return;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        if (!versionsReady(playlistId)) {
            loadVersionsForPlaylistAsync(playlistId); ///< Unfold loads versions.
        }
        return;
    }
}

void PlaylistTree::clearExpanded() {
    if (!expanded_ || expanded_->value().empty()) {
        return;
    }
    expanded_->set_value("", false); ///< Do not fetch versions for the previous project.
}

bool PlaylistTree::versionsReady(std::int64_t playlistId) const {
    const nlohmann::json tree = treeValue();
    if (!tree.is_array()) {
        return false;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        return node.value("versionsLoaded", false);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Loads. Names stop. Versions of one playlist do not.
// ---------------------------------------------------------------------------

bool PlaylistTree::checkResultViewIsPlaylist() const {
    return dal::resultView() == ResultView::Playlist;
}

void PlaylistTree::loadPlaylistListAsync() {
    const std::uint64_t requestId = ++(*playlist_request_id_); ///< Stop the name fetch already running.
    clearPlaylistUi();
    if (!checkResultViewIsPlaylist() || dal::selectedProject() == -1) {
        return; ///< Versions is showing, or no project is selected.
    }
    CatalogLoadRequest request = makeCatalogRequest(
        &panel_, CatalogKind::PlaylistTree, requestId, dal::selectedProject());
    request.generation = playlist_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void PlaylistTree::clearPlaylists() {
    ++(*playlist_request_id_); ///< Drop pages that still belong to the previous project.
    clearPlaylistUi();
}

void PlaylistTree::clearPlaylistUi() {
    versions_by_playlist_.clear();
    sendPlaylistTreeToQml(nlohmann::json::array());
    clearExpanded();
}

void PlaylistTree::loadVersionsForPlaylistAsync(std::int64_t playlistId) {
    const VersionParents parents = readVersionParents();
    CatalogLoadRequest request = makeCatalogRequest(
        &panel_,
        CatalogKind::PlaylistVersions,
        playlist_request_id_->load(),
        dal::selectedProject());
    request.playlist_id = playlistId;
    request.sequence = parents.sequenceId;
    request.shot = parents.shotId;
    request.asset = parents.assetId;
    launchCatalogLoad(panel_.system(), std::move(request));
}

void PlaylistTree::reloadExpanded() {
    if (!checkResultViewIsPlaylist() || dal::selectedProject() == -1 || !expanded_) {
        return; ///< Versions does not reload a playlist.
    }
    const std::string name = expanded_->value();
    if (name.empty()) {
        return;
    }
    nlohmann::json tree = treeValue();
    if (!tree.is_array()) {
        return;
    }
    for (auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        node["versionsLoaded"] = false; ///< The next page replaces these versions.
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        sendPlaylistTreeToQml(tree);
        loadVersionsForPlaylistAsync(playlistId);
        return;
    }
}

// ---------------------------------------------------------------------------
// Tree. The first name page replaces nodes. A later page adds them.
// ---------------------------------------------------------------------------

nlohmann::json PlaylistTree::treeValue() const {
    if (!tree_) {
        return nlohmann::json::array();
    }
    const nlohmann::json tree = tree_->value();
    return tree.is_array() ? tree : nlohmann::json::array();
}

void PlaylistTree::sendPlaylistTreeToQml(const nlohmann::json &tree) {
    if (!tree_) {
        return;
    }
    tree_->set_value(tree); ///< The QML reads this attribute.
}

void PlaylistTree::updatePlaylistList(bool append, const utility::JsonStore &payload) {
    const std::vector<poco::Playlist> rows = rowsFromJson<poco::Playlist>(payload);
    const nlohmann::json previous = treeValue();
    nlohmann::json tree = append ? previous : nlohmann::json::array();
    for (const poco::Playlist &row : rows) {
        if (shownPlaylist(tree, row.id)) {
            continue; ///< This page repeats a playlist already in the tree.
        }
        const std::optional<nlohmann::json> kept = shownPlaylist(previous, row.id);
        if (kept) {
            tree.push_back(*kept); ///< Keep versions already unfolded on this playlist.
            continue;
        }
        tree.push_back(
            {{"name", row.name}, {"id", row.id}, {"versions", nlohmann::json::array()}});
    }
    sortPlaylistsByName(tree);
    sendPlaylistTreeToQml(tree);
}

void PlaylistTree::updateVersionsForPlaylist(
    std::int64_t playlistId, bool append, const utility::JsonStore &payload) {
    const std::vector<poco::Version> rows = rowsFromJson<poco::Version>(payload);
    std::vector<poco::Version> &stored = versions_by_playlist_[playlistId];
    if (!append) {
        stored.clear(); ///< The first page replaces this playlist's versions.
    }
    stored.insert(stored.end(), rows.begin(), rows.end());
    const std::vector<std::string> labels = buildUniqueLabels(stored);
    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t index = 0; index < stored.size(); ++index) {
        entries.push_back(versionFacts(stored[index], labels[index], std::string()));
    }
    nlohmann::json tree = treeValue();
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        node["versions"] = std::move(entries);
        if (rows.size() < dal::readPageLimit()) {
            node["versionsLoaded"] = true; ///< A short page is the last page.
        }
        sortVersionsByName(node); ///< Only the versions of this playlist move.
        sendPlaylistTreeToQml(tree);
        stampPlaylistThumbs(); ///< A file stored earlier can show with this page.
        return;
    }
}

void PlaylistTree::sortPlaylistsByName(nlohmann::json &tree) const {
    if (!tree.is_array()) {
        return;
    }
    std::sort(tree.begin(), tree.end(), playlistNameAlphabeticOrderAlgorithm);
}

void PlaylistTree::sortVersionsByName(nlohmann::json &node) const {
    nlohmann::json versions = node.value("versions", nlohmann::json::array());
    if (!versions.is_array()) {
        return;
    }
    std::sort(versions.begin(), versions.end(), versionNameAlphabeticOrderAlgorithm);
    node["versions"] = std::move(versions);
}

void PlaylistTree::stampPlaylistThumbs() {
    if (!tree_ || dal::selectedProject() == -1) {
        return;
    }
    nlohmann::json tree = treeValue();
    bool changed = false;
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("versions") || !node["versions"].is_array()) {
            continue;
        }
        for (auto &entry : node["versions"]) {
            if (!entry.is_object() || !entry.contains("id")) {
                continue;
            }
            const std::string image =
                thumbFor(dal::selectedProject(), entry["id"].get<std::int64_t>());
            if (entry.value("image", std::string()) == image) {
                continue;
            }
            entry["image"] = image;
            changed = true;
        }
    }
    if (changed) {
        sendPlaylistTreeToQml(tree);
    }
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the tree.
// ---------------------------------------------------------------------------

caf::message_handler PlaylistTree::message_handler_extensions() {
    auto apply = [this](
                     std::uint64_t requestId,
                     const std::string &catalog,
                     std::int64_t projectId,
                     bool append,
                     std::int64_t playlistId,
                     const utility::JsonStore &payload) {
        if (dal::selectedProject() != projectId) {
            trace::drop(catalog.c_str(), requestId); ///< The user picked another project.
            return;
        }
        if (catalog == "playlist_tree") {
            if (requestId != playlist_request_id_->load()) {
                trace::drop("playlist_tree", requestId);
                return;
            }
            updatePlaylistList(append, payload);
            return;
        }
        updateVersionsForPlaylist(
            playlistId, append, payload); ///< Version pages are not dropped when the name id moves.
    };
    caf::detail::behavior_impl::pointer handler =
        caf::make_counted<PlaylistPageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
