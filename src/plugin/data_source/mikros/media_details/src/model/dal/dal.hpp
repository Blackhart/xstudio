// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.hpp
 * @brief Data-access layer: sole Media Details gateway to the shared pms library.
 */
#pragma once

#include "model/poco/asset_detail.hpp"
#include "model/poco/note_detail.hpp"
#include "model/poco/project_detail.hpp"
#include "model/poco/sequence_detail.hpp"
#include "model/poco/shot_detail.hpp"
#include "model/poco/version_detail.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_media_details::dal {

/**
 * @brief Ensures the shared pms session exists.
 *
 * The plugin calls this once at launch. Safe when Shot Browser already initialized pms.
 */
void initialize();

/**
 * @brief Loads one version for the first paint of the detail panel.
 *
 * Omits `version_number`, `open_notes_count`, `sites`, and `playlists`.
 * Those arrive from `findVersionDetailExtraById` after the panel is showing.
 * @param versionId ShotGrid version id stamped on the media.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::VersionDetail> findVersionDetailById(std::int64_t versionId);

/**
 * @brief Loads the slow version fields after the panel is already showing.
 * @param versionId ShotGrid version id already shown.
 * @return Summary fields, sites, and playlists, or nullopt when missing or on failure.
 */
std::optional<poco::VersionDetailExtra> findVersionDetailExtraById(std::int64_t versionId);

/**
 * @brief Loads one shot, including its sequence parent labels.
 * @param shotId Domain shot id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::ShotDetail> findShotDetailById(std::int64_t shotId);

/**
 * @brief Loads one sequence, including its project parent labels.
 *
 * Omits `shot_count`. That summary arrives from `findSequenceShotCountById`.
 * @param sequenceId Domain sequence id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::SequenceDetail> findSequenceDetailById(std::int64_t sequenceId);

/**
 * @brief Loads the summary shot count of one sequence.
 * @param sequenceId Domain sequence id.
 * @return The count when the row exists, or nullopt when missing or on failure.
 */
std::optional<std::int64_t> findSequenceShotCountById(std::int64_t sequenceId);

/**
 * @brief Loads one asset, including its project parent labels.
 * @param assetId Domain asset id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::AssetDetail> findAssetDetailById(std::int64_t assetId);

/**
 * @brief Loads one project.
 * @param projectId Domain project id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::ProjectDetail> findProjectDetailById(std::int64_t projectId);

/**
 * @brief Loads the notes linked to one version, newest first.
 *
 * A failed find is logged and returns an empty list. The Notes tab then
 * shows ready with no rows, and DetailState is left alone.
 * @param versionId Domain version id.
 * @return Notes sorted by `created_at` descending. Empty when none or on failure.
 */
std::vector<poco::NoteDetail> findNotesByVersionId(std::int64_t versionId);

/**
 * @brief Colour map, label map, and fallback colour of one schema field.
 */
struct StatusStyle {
    std::map<std::string, std::string> colors; ///< Code → colour. Empty when the field has none.
    std::map<std::string, std::string> labels; ///< Code → label. Empty when the field has none.
    std::string defaultColor; ///< Colour for a code missing from `colors`. Empty when unset.
    bool editable = false; ///< True when the panel may write this field.
};

/**
 * @brief Writes one editable field through the shared pms session.
 * @param entity Domain entity, such as `"version"`.
 * @param id Record id. Must be greater than zero.
 * @param field Domain field, such as `"status"`.
 * @param code Status code, such as `"ip"`.
 * @throw std::exception When validation or ShotGrid rejects the write.
 */
void updateField(
    const std::string &entity,
    std::int64_t id,
    const std::string &field,
    const std::string &code);

/**
 * @brief Reads the colours and labels of one schema field.
 * @param entityName Domain entity, such as `"version"`.
 * @param fieldKey Field name, such as `"status"`.
 * @return The maps from the schema. Empty when the entity or field is missing.
 */
StatusStyle statusStyle(const std::string &entityName, const std::string &fieldKey);

/**
 * @brief Colour of each note status code, from the schema.
 * @return Code → colour. Empty when the note entity has no colours.
 */
std::map<std::string, std::string> noteStatusColors();

/**
 * @brief Accessible name of each note status code, from the schema.
 * @return Code → label. Empty when the note entity has no labels.
 */
std::map<std::string, std::string> noteStatusLabels();

/**
 * @brief Fallback colour of a note status code that is not in the colour map.
 * @return The schema `default_color`, or empty when the note status field is missing.
 */
std::string noteStatusDefaultColor();

} // namespace xstudio::mikros_media_details::dal
