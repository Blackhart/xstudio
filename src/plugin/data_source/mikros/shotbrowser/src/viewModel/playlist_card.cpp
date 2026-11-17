// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_card.cpp
 * @brief Fields one playlist version row shows, and the cached image stamped onto that row.
 */

#include "viewModel/playlist_card.hpp"
#include "viewModel/thumbnails.hpp"
#include "viewModel/version_card.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Writes version labels into one playlist node.
 * @param node Playlist object already stored in the tree.
 * @param versions Versions received for that playlist so far.
 */
void writeVersions(nlohmann::json &node, const std::vector<poco::Version> &versions) {
    const std::vector<std::string> labels = buildUniqueLabels(versions);
    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t index = 0; index < versions.size(); ++index) {
        entries.push_back(versionFacts(versions[index], labels[index], std::string()));
    }
    node["versions"] = std::move(entries);
}

} // namespace

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Mails one playlist's versions. The actor writes them into that node.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param playlistId Playlist the user unfolded.
 * @param versions Versions received for that playlist so far.
 * @param complete True on the last page.
 */
void sendPlaylistVersions(
    const TreeMail &mail,
    std::int64_t playlistId,
    const std::vector<poco::Version> &versions,
    bool complete) {
    nlohmann::json patch = {{"playlistId", playlistId}, {"complete", complete}};
    writeVersions(patch, versions);
    sendCatalog(
        mail.weak,
        mail.requestId,
        mail.catalog,
        mail.projectId,
        mail.scope,
        false,
        utility::JsonStore(std::move(patch)));
}

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

void
stampPlaylistThumbs(PanelState &state) {
    if (!state.playlist_tree_ || !state.selected_project_) {
        return;
    }
    nlohmann::json tree = state.playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
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
                thumbFor(state, state.selected_project_->id, entry["id"].get<std::int64_t>());
            if (entry.value("image", std::string()) == image) {
                continue;
            }
            entry["image"] = image;
            changed = true;
        }
    }
    if (changed) {
        state.playlist_tree_->set_value(std::move(tree));
    }
}


} // namespace xstudio::mikros_shotbrowser
