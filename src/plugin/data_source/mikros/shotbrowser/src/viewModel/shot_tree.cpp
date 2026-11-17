// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_tree.cpp
 * @brief Sequence and shot tree: load, unfold, selection, and alignment of the open branch.
 */

#include "viewModel/shot_tree.hpp"
#include "viewModel/asset_tab.hpp"
#include "viewModel/version_list.hpp"
#include <algorithm>


namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Writes shot labels and ids into one sequence node.
 * @param node Sequence object already stored in the tree.
 * @param shots Shots received for that sequence so far.
 */
void writeShots(nlohmann::json &node, const std::vector<poco::Shot> &shots) {
    const std::vector<std::string> labels = buildUniqueLabels(shots);
    nlohmann::json names = nlohmann::json::array();
    nlohmann::json ids = nlohmann::json::array();
    for (std::size_t index = 0; index < shots.size(); ++index) {
        names.push_back(labels[index]);
        ids.push_back(shots[index].id);
    }
    node["shots"] = std::move(names);
    node["shotIds"] = std::move(ids);
}

/**
 * @brief Mails one sequence's shots. The actor writes them into that node.
 * @param mail Destination. `catalog` is `sequence_shots`.
 * @param sequenceId Sequence the user unfolded.
 * @param shots Shots received for that sequence so far.
 * @param complete True on the last page.
 */
void sendSequenceShots(
    const TreeMail &mail,
    std::int64_t sequenceId,
    const std::vector<poco::Shot> &shots,
    bool complete) {
    nlohmann::json patch = {{"sequenceId", sequenceId}, {"complete", complete}};
    writeShots(patch, shots);
    sendCatalog(
        mail.weak,
        mail.requestId,
        mail.catalog,
        mail.projectId,
        mail.scope,
        false,
        utility::JsonStore(std::move(patch)));
}

} // namespace

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Loads one sequence's shots and mails each page.
 * @param mail Destination. `catalog` is `sequence_shots`.
 * @param sequenceId Sequence the user unfolded.
 */
void publishShotsOfSequence(const TreeMail &mail, std::int64_t sequenceId) {
    std::vector<poco::Shot> shots;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Shot> page =
            dal::findShots(sequenceId, dal::readPageLimit(), cursor);
        if (!page.rows.empty()) {
            shots.insert(shots.end(), page.rows.begin(), page.rows.end());
        }
        const bool complete = !page.next_cursor;
        if (!shots.empty() || complete) {
            sendSequenceShots(mail, sequenceId, shots, complete);
        }
        if (complete) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Loads sequences and mails the tree after each page. Shots stay empty.
 * @param mail Destination and request. `catalog` is `shot_tree`.
 */
void publishSequences(const TreeMail &mail) {
    nlohmann::json tree = nlohmann::json::array();
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Sequence> page =
            dal::findSequences(mail.projectId, dal::readPageLimit(), cursor);
        for (const poco::Sequence &sequence : page.rows) {
            tree.push_back(
                {{"name", sequence.name},
                 {"id", sequence.id},
                 {"shots", nlohmann::json::array()},
                 {"shotIds", nlohmann::json::array()}});
        }
        if (!page.rows.empty()) {
            sendTree(mail, tree); ///< Show this page of sequences. Shots wait for an unfold.
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
}

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

std::optional<poco::Sequence>
findSequenceByLabel(const PanelState &state, 
    const std::string &label) {
    const nlohmann::json tree = state.shot_tree_ ? state.shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id")) {
            continue;
        }
        if (!node["name"].is_string() || node["name"].get<std::string>() != label) {
            continue;
        }
        return poco::Sequence{node["id"].get<std::int64_t>(), label};
    }
    return std::nullopt;
}

std::optional<poco::Shot> findShotByLabel(const PanelState &state, 
    const std::string &label) {
    const nlohmann::json tree = state.shot_tree_ ? state.shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("shots") || !node.contains("shotIds")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        const nlohmann::json &ids = node["shotIds"];
        if (!labels.is_array() || !ids.is_array()) {
            continue;
        }
        const std::size_t count = std::min(labels.size(), ids.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (!labels[index].is_string() || labels[index].get<std::string>() != label) {
                continue;
            }
            return poco::Shot{ids[index].get<std::int64_t>(), label};
        }
    }
    return std::nullopt;
}

std::optional<poco::Sequence> findSequenceOwningShot(const PanelState &state, 
    const std::string &shotLabel) {
    const nlohmann::json tree = state.shot_tree_ ? state.shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id") ||
            !node.contains("shots")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        if (!labels.is_array()) {
            continue;
        }
        for (const auto &entry : labels) {
            if (!entry.is_string() || entry.get<std::string>() != shotLabel) {
                continue;
            }
            return poco::Sequence{
                node["id"].get<std::int64_t>(), node["name"].get<std::string>()};
        }
    }
    return std::nullopt;
}

void clearSequenceSelection(PanelState &state) {
    const bool hadSelection = state.selected_sequence_.has_value() || !state.sequence_->value().empty();
    state.selected_sequence_ = std::nullopt;
    if (!state.sequence_->value().empty()) {
        state.sequence_->set_value("", false); ///< Do not re-enter the sequence handler.
    }
    if (hadSelection) {
        trace::select("sequence", false, 0, {});
    }
}

void clearShotSelection(PanelState &state) {
    const bool hadSelection = state.selected_shot_.has_value() || !state.shot_->value().empty();
    state.selected_shot_ = std::nullopt;
    if (!state.shot_->value().empty()) {
        state.shot_->set_value("", false); ///< Do not re-enter the shot handler.
    }
    if (hadSelection) {
        trace::select("shot", false, 0, {});
    }
}

void clearSequenceBranch(PanelState &state) {
    clearSequenceSelection(state); ///< Drop the sequence before its shots.
    clearShotSelection(state);
}

void refreshShotTree(PanelState &state) {
    ++state.sequence_request_id_; ///< Drop a shot tree that is still loading.
    state.sequences_ready_ = state.selected_project_.has_value();
    clearSequenceSelection(state);
    state.shot_tree_->set_value(nlohmann::json::array()); ///< Hide the previous project's tree.
    if (!state.selected_project_) {
        return;
    }
    startShotTreeLoad(state, state.sequence_request_id_, state.selected_project_->id);
}

void startSequenceShotLoad(PanelState &state, 
    std::uint64_t requestId, std::int64_t projectId, std::int64_t sequenceId) {
    CatalogLoadRequest request =
        makeCatalogRequest(state.panel, CatalogKind::SequenceShots, requestId, projectId);
    request.sequence_id = sequenceId;
    launchCatalogLoad(state.panel->system(), std::move(request));
}

bool sequenceShotsReady(const PanelState &state, std::int64_t sequenceId) {
    const nlohmann::json tree = state.shot_tree_ ? state.shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return false;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != sequenceId) {
            continue;
        }
        return node.value("shotsLoaded", false);
    }
    return false;
}

void startShotTreeLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId) {
    launchCatalogLoad(
        state.panel->system(), makeCatalogRequest(state.panel, CatalogKind::ShotTree, requestId, projectId));
}

void clearShotTree(PanelState &state) {
    ++state.sequence_request_id_; ///< Drop sequence and shot pages for the previous project.
    state.sequences_ready_ = false;
    clearSequenceSelection(state);
    clearShotSelection(state);
    state.shot_tree_->set_value(nlohmann::json::array());
}

void onSequenceExpanded(PanelState &state) {
    const std::string name = state.expanded_sequence_->value();
    if (name.empty() || !state.selected_project_) {
        return;
    }
    const std::optional<poco::Sequence> sequence = findSequenceByLabel(state, name);
    if (!sequence) {
        return;
    }
    if (!sequenceShotsReady(state, sequence->id)) {
        startSequenceShotLoad(state, state.sequence_request_id_, state.selected_project_->id, sequence->id);
    }
    if (!state.selected_sequence_ || state.selected_sequence_->id != sequence->id) {
        state.sequence_->set_value(sequence->name); ///< Versions of the sequence the user opened.
    }
}

void onSequenceChosen(PanelState &state) {
    state.selected_sequence_ = findSequenceByLabel(state, state.sequence_->value());
    logSelection("sequence", state.selected_sequence_);
    if (state.selected_sequence_) {
        clearAssetSelection(state); ///< Sequence and asset are mutually exclusive.
    }
    if (!state.selected_asset_) {
        clearShotSelection(state);
    }
    refreshVersions(state);
}

void onShotChosen(PanelState &state) {
    state.selected_shot_ = findShotByLabel(state, state.shot_->value());
    logSelection("shot", state.selected_shot_);
    if (state.selected_shot_) {
        clearAssetSelection(state); ///< A shot belongs to the sequence branch.
        alignSequenceWithShot(state, state.shot_->value());
    }
    refreshVersions(state);
}

void alignSequenceWithShot(PanelState &state, const std::string &shotLabel) {
    const std::optional<poco::Sequence> parent = findSequenceOwningShot(state, shotLabel);
    if (!parent) {
        return;
    }
    const bool realign = state.sequence_->value() != parent->name;
    state.selected_sequence_ = parent;
    if (!realign) {
        return;
    }
    state.aligning_sequence_ = true;
    state.sequence_->set_value(parent->name); ///< Skip the sequence handler so the shot stays.
    state.aligning_sequence_ = false;
    logSelection("sequence", state.selected_sequence_); ///< Log after the guard is cleared.
}

void applyShotTree(PanelState &state, 
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != state.sequence_request_id_) {
        trace::drop("shot_tree", requestId);
        return;
    }
    state.shot_tree_->set_value(nlohmann::json(payload));
}

void applySequenceShots(PanelState &state, 
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != state.sequence_request_id_) {
        trace::drop("sequence_shots", requestId);
        return;
    }
    const nlohmann::json patch = nlohmann::json(payload);
    if (!patch.contains("sequenceId")) {
        return;
    }
    nlohmann::json tree = state.shot_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    const std::int64_t sequenceId = patch["sequenceId"].get<std::int64_t>();
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != sequenceId) {
            continue;
        }
        node["shots"] = patch["shots"];
        node["shotIds"] = patch["shotIds"];
        if (patch.value("complete", false)) {
            node["shotsLoaded"] = true;
        }
        state.shot_tree_->set_value(tree);
        return;
    }
}


} // namespace xstudio::mikros_shotbrowser
