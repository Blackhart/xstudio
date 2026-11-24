// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/shelf.hpp
 * @brief Named multi-entity FieldExpr search stored for the Shot Browser.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One named search the user can apply when loading versions or playlists.
 *
 * `filters` is an object whose keys are domain entity names and whose values
 * are ExprNode JSON trees (`exprToJson`). Empty object means the virtual All
 * shelf (no extra Where / In). Project is never a key: the panel project
 * already scopes the find.
 */
struct Shelf {
    std::string id;         ///< Uuid string, or `"all"` for the virtual shelf.
    std::string name;       ///< Label shown in the shelf list.
    nlohmann::json filters; ///< `{ "shot": ExprNode, "version": ExprNode, ... }`.
};

} // namespace xstudio::mikros_shotbrowser::poco
