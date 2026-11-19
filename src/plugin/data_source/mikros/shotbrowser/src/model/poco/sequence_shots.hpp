// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/sequence_shots.hpp
 * @brief One sequence and the shots already attached to it.
 */
#pragma once

#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"

#include <vector>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One sequence row and the shots ShotGrid attached to it.
 *
 * `shots` is empty when the sequence has none. The tree of a project is a
 * vector of these nodes.
 */
struct SequenceShots {
    Sequence sequence;       ///< Id and name of the sequence.
    std::vector<Shot> shots; ///< Shots attached to `sequence`.
};

} // namespace xstudio::mikros_shotbrowser::poco
