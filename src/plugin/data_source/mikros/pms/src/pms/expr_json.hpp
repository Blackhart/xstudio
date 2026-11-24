// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/expr_json.hpp
 * @brief JSON round-trip for FieldExpr trees (ExprNode field|group only).
 */
#pragma once

#include "pms/ir.hpp"

#include <nlohmann/json.hpp>

#include <memory>

namespace pms {

/**
 * @brief Serializes one FieldExpr tree to JSON.
 *
 * Groups and field leaves only. In() leaves may carry optional `parent`.
 *
 * @param node Root of the expression.
 * @return JSON object with `kind`, `op`, and leaf or group fields.
 * @throw Error When a group child is null, a leaf has no value, or the kind is
 *        links.
 */
nlohmann::json exprToJson(const ExprNode &node);

/**
 * @brief Builds a FieldExpr tree from JSON.
 *
 * Accepts `kind` `field` or `group` only. Rejects legacy `scope`.
 *
 * @param json Object produced by exprToJson or an equivalent editor payload.
 * @return Shared root of the reconstructed tree.
 * @throw Error When the JSON shape, op, or kind is illegal.
 */
std::shared_ptr<const ExprNode> exprFromJson(const nlohmann::json &json);

} // namespace pms
