// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/query.hpp
 * @brief Fluent query builder. The verb is the last call.
 */
#pragma once

#include "pms/backend.hpp"
#include "pms/gen/entities.hpp"
#include "pms/session.hpp"
#include "pms/validate.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace pms {

/**
 * @brief Builds In and Where trees for one entity, then runs find().
 *
 * Start from the generated function, for example Version(). In() and Where()
 * may be repeated; each extra clause is combined with the previous one by
 * `and`. Calling find() with neither clause lists every row. Limit() and
 * After() turn the same find() into a keyset page.
 *
 * The generated function hides the entity struct in ordinary lookup. Name the
 * query type with `decltype(Project())` rather than `Query<Project>`.
 *
 * @tparam Entity Generated entity tag, such as the Project struct.
 */
template <typename Entity> class Query {
  public:
    /**
     * @brief Builds an empty query for `Entity::entityName`.
     */
    Query() : entity_(Entity::entityName) {}

    /**
     * @brief Adds a parent scope filter, or combines it with the one stored.
     *
     * Each parent in `Parents` must appear in `Entity::Scope`. The first call
     * stores `expr`. A later call builds an `and` whose children are the
     * previous tree and `expr`.
     *
     * @tparam Parents Parent tags named by the expression.
     * @param expr Scope filter whose leaves name parents of `Entity`.
     * @return A new query. This query is unchanged.
     */
    template <typename... Parents> Query In(ScopeExpr<Parents...> expr) const {
        static_assert(
            (true && ... && scopeAllows<Entity, Parents>()),
            "parent is not in this entity scope");
        Query copy = *this;
        if (!copy.in_) {
            copy.in_ = std::move(expr.node);
            return copy;
        }
        auto combined = std::make_shared<ExprNode>();
        combined->op = ExprOp::And;
        combined->kind = ExprKind::Group;
        combined->args = {copy.in_, expr.node};
        copy.in_ = std::move(combined);
        return copy;
    }

    /**
     * @brief Adds a field filter, or combines it with the filter already stored.
     *
     * The first call stores `expr`. A later call builds an `and` whose children
     * are the previous tree and `expr`. The trees are not flattened.
     *
     * @param expr Filter whose leaves belong to `Entity`.
     * @return A new query. This query is unchanged.
     */
    Query Where(FieldExpr<Entity> expr) const {
        Query copy = *this;
        if (!copy.where_) {
            copy.where_ = std::move(expr.node);
            return copy;
        }
        auto combined = std::make_shared<ExprNode>();
        combined->op = ExprOp::And;
        combined->kind = ExprKind::Group;
        combined->args = {copy.where_, expr.node};
        copy.where_ = std::move(combined);
        return copy;
    }

    /**
     * @brief Caps how many rows find() returns in one page.
     * @param limit Maximum rows. Must be greater than zero.
     * @return A new query. This query is unchanged.
     */
    Query Limit(std::size_t limit) const {
        Query copy = *this;
        copy.limit_ = limit;
        return copy;
    }

    /**
     * @brief Continues a limited find after a previous Page::next_cursor.
     * @param cursor Opaque token from the prior page. Empty clears the cursor.
     * @return A new query. This query is unchanged.
     */
    Query After(std::string cursor) const {
        Query copy = *this;
        if (cursor.empty()) {
            copy.cursor_.reset();
        } else {
            copy.cursor_ = std::move(cursor);
        }
        return copy;
    }

    /**
     * @brief Returns the trees without a verb and without validation.
     * @return Intermediate representation with an empty `op`.
     */
    Ir ir() const {
        Ir out;
        out.entity = entity_;
        out.in = in_;
        out.where = where_;
        out.limit = limit_;
        out.cursor = cursor_;
        return out;
    }

    /**
     * @brief Validates the query and asks the session backend for matching rows.
     *
     * Without Limit(), returns every match and leaves next_cursor empty. With
     * Limit(), returns one page; pass After(next_cursor) to continue.
     * CachingBackend caches only limited finds.
     *
     * @param session Schema and backend. The backend is not owned.
     * @return Page of rows and an optional next cursor.
     * @throw Error When validation fails or a row has no usable identity.
     */
    Page find(Session &session) const {
        Ir out = ir();
        out.op = Op::Find;
        validate(out, session.schema());
        Page page = session.backend().find(out);
        for (const Record &row : page.rows) {
            checkRecord(row);
        }
        return page;
    }

  private:
    std::string entity_; ///< YAML entity name copied from the tag.
    std::shared_ptr<const ExprNode> in_;    ///< Scope tree, null until In().
    std::shared_ptr<const ExprNode> where_; ///< Filter tree, null until Where().
    std::optional<std::size_t> limit_;      ///< Page size when set.
    std::optional<std::string> cursor_;     ///< Continuation cursor when set.
};

} // namespace pms

#include "pms/gen/query_factories.hpp"
