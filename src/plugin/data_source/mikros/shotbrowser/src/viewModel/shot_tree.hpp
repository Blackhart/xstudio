// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_tree.hpp
 * @brief Sequence and shot tree: load, unfold, selection, and alignment of the open branch.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Loads sequences and mails the tree after each page. Shots stay empty.
 * @param mail Destination and request. `catalog` is `shot_tree`.
 */
void publishSequences(const TreeMail &mail);

/**
 * @brief Loads one sequence's shots and mails each page.
 * @param mail Destination. `catalog` is `sequence_shots`.
 * @param sequenceId Sequence the user unfolded.
 */
void publishShotsOfSequence(const TreeMail &mail, std::int64_t sequenceId);

/**
 * @brief Sequence row for a name in the shot tree.
 * @param state Panel catalogs and attributes.
 * @param label Sequence name the panel sent.
 * @return The row, or empty when that sequence is not in the tree.
 */
std::optional<poco::Sequence> findSequenceByLabel(const PanelState &state, const std::string &label);

/**
 * @brief Shot row for a label in the shot tree.
 * @param state Panel catalogs and attributes.
 * @param label Shot label the panel sent.
 * @return The row, or empty when that shot is not under any loaded sequence.
 */
std::optional<poco::Shot> findShotByLabel(const PanelState &state, const std::string &label);

/**
 * @brief Sequence that contains a shot label.
 * @param state Panel catalogs and attributes.
 * @param shotLabel Shot the user selected.
 * @return The parent sequence, or empty when the shot is not in the tree.
 */
std::optional<poco::Sequence>
findSequenceOwningShot(const PanelState &state, const std::string &shotLabel);

/** @brief Clears the sequence and logs the clear when one was selected. */
void clearSequenceSelection(PanelState &state);

/** @brief Clears the shot and logs the clear when one was selected. */
void clearShotSelection(PanelState &state);

/** @brief Clears the sequence and the shot under it. */
void clearSequenceBranch(PanelState &state);

/** @brief Clears the shot tree and loads sequence names, without their shots. */
void refreshShotTree(PanelState &state);

/**
 * @brief Loads the shots of one sequence into its tree node.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the shot tree. A newer tree drops these pages.
 * @param projectId Project that owns the sequence.
 * @param sequenceId Sequence the user unfolded.
 */
void startSequenceShotLoad(
    PanelState &state, std::uint64_t requestId, std::int64_t projectId, std::int64_t sequenceId);

/**
 * @brief True when that sequence's shots were already loaded to the last page.
 * @param state Panel catalogs and attributes.
 * @param sequenceId Sequence in the current tree.
 */
bool sequenceShotsReady(const PanelState &state, std::int64_t sequenceId);

/**
 * @brief Loads sequence names for one project.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the shot tree. A newer tree drops these pages.
 * @param projectId Project whose sequences are loaded.
 */
void startShotTreeLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId);

/** @brief Drops the shot tree without asking the backend. */
void clearShotTree(PanelState &state);

/** @brief Loads one sequence's shots and narrows the version list to that sequence. */
void onSequenceExpanded(PanelState &state);

/** @brief Keeps the chosen sequence, clears the shot and the asset, and reloads versions. */
void onSequenceChosen(PanelState &state);

/** @brief Keeps the chosen shot, selects its sequence, and reloads versions. */
void onShotChosen(PanelState &state);

/**
 * @brief Selects the sequence that owns the shot, without clearing that shot.
 * @param state Panel catalogs and attributes.
 * @param shotLabel Shot that was just selected.
 */
void alignSequenceWithShot(PanelState &state, const std::string &shotLabel);

/**
 * @brief Replaces the shot tree with the sequences received so far.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the tree. An older tree is ignored.
 * @param payload JSON tree from the worker.
 */
void applyShotTree(PanelState &state, std::uint64_t requestId, const utility::JsonStore &payload);

/**
 * @brief Writes one page of shots into the matching sequence node.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the tree. An older page is ignored.
 * @param payload JSON patch with the sequence id and its shots.
 */
void applySequenceShots(
    PanelState &state, std::uint64_t requestId, const utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
