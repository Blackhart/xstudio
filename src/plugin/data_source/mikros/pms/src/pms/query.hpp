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

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pms {

/**
 * @brief Builds In and Where trees for one entity, then runs find() or count().
 *
 * Start from the generated function, for example Version(). In() and Where()
 * may be repeated; each extra clause is combined with the previous one by
 * `and`. Calling find() with neither clause lists every row. Limit() and
 * After() turn the same find() into a keyset page. count() uses the same
 * filters and rejects Limit() and After().
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
     * @brief Adds a parent field filter, or combines it with the one stored.
     *
     * `Parent` must appear in `Entity::Scope` (including inherited ancestors).
     * Leaves are stamped with the parent domain name so validate and translate
     * resolve fields on that parent via mapping links. The first call stores
     * `expr`. A later call builds an `and` whose children are the previous tree
     * and `expr`.
     *
     * @tparam Parent Parent entity tag whose fields the expression uses.
     * @param expr Filter whose leaves belong to `Parent`.
     * @return A new query. This query is unchanged.
     */
    template <typename Parent> Query In(FieldExpr<Parent> expr) const {
        static_assert(scopeAllows<Entity, Parent>(), "parent is not in this entity scope");
        Query copy = *this;
        auto stamped = stampParent(expr.node, Parent::entityName);
        if (!copy.in_) {
            copy.in_ = std::move(stamped);
            return copy;
        }
        auto combined = std::make_shared<ExprNode>();
        combined->op = ExprOp::And;
        combined->kind = ExprKind::Group;
        combined->args = {copy.in_, std::move(stamped)};
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
     * @brief Restricts which domain fields find() loads.
     *
     * Without this call, every mapped field is requested. `id` and `name` are
     * always kept so a returned row still passes checkRecord. Count ignores
     * the list.
     *
     * @tparam Ts Value types of the field proxies.
     * @param fields Proxies such as `version::author`.
     * @return A new query. This query is unchanged.
     */
    template <typename... Ts> Query Fields(const Field<Entity, Ts> &... fields) const {
        Query copy = *this;
        std::vector<std::string> keys;
        keys.reserve(sizeof...(Ts) + 2);
        (keys.push_back(fields.key), ...);
        auto ensure = [&keys](const char *key) {
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        };
        ensure("id");
        ensure("name");
        copy.fields_ = std::move(keys);
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
        out.fields = fields_;
        return out;
    }

    /**
     * @brief Validates the query and asks the session backend for matching rows.
     *
     * Without Limit(), returns every match and leaves next_cursor empty. With
     * Limit(), returns one page; pass After(next_cursor) to continue.
     * CachingBackend caches limited finds and every count.
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

    /**
     * @brief Validates the query and asks how many rows match.
     *
     * Uses the same In() and Where() trees as find(). Limit() and After() are
     * rejected by validation.
     *
     * @param session Schema and backend. The backend is not owned.
     * @return Match count.
     * @throw Error When validation fails or the backend rejects the query.
     */
    std::size_t count(Session &session) const {
        Ir out = ir();
        out.op = Op::Count;
        validate(out, session.schema());
        return session.backend().count(out);
    }

  private:
    std::string entity_; ///< YAML entity name copied from the tag.
    std::shared_ptr<const ExprNode> in_;    ///< Parent field tree, null until In().
    std::shared_ptr<const ExprNode> where_; ///< Filter tree, null until Where().
    std::optional<std::size_t> limit_;      ///< Page size when set.
    std::optional<std::string> cursor_;     ///< Continuation cursor when set.
    std::optional<std::vector<std::string>> fields_; ///< Find projection. Empty means all fields.
};

} // namespace pms

#include "pms/gen/query_factories.hpp"
