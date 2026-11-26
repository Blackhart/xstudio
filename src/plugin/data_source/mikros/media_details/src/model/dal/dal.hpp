// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.hpp
 * @brief Data-access layer: sole Media Details gateway to the shared pms library.
 */
#pragma once

#include "model/poco/asset_detail.hpp"
#include "model/poco/project_detail.hpp"
#include "model/poco/sequence_detail.hpp"
#include "model/poco/shot_detail.hpp"
#include "model/poco/version_detail.hpp"

#include <cstdint>
#include <optional>

namespace xstudio::mikros_media_details::dal {

/**
 * @brief Ensures the shared pms session exists.
 *
 * The plugin calls this once at launch. Safe when Shot Browser already initialized pms.
 */
void initialize();

/**
 * @brief Loads one version for the detail panel.
 * @param versionId ShotGrid version id stamped on the media.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::VersionDetail> findVersionDetailById(std::int64_t versionId);

/**
 * @brief Loads one shot, including its sequence parent labels.
 * @param shotId Domain shot id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::ShotDetail> findShotDetailById(std::int64_t shotId);

/**
 * @brief Loads one sequence, including its project parent labels.
 * @param sequenceId Domain sequence id.
 * @return The row when found, or nullopt when missing or on failure.
 */
std::optional<poco::SequenceDetail> findSequenceDetailById(std::int64_t sequenceId);

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

} // namespace xstudio::mikros_media_details::dal
