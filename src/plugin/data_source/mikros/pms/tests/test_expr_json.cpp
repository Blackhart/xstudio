// SPDX-License-Identifier: Apache-2.0

#include "mapped_schema.hpp"
#include "pms/pms.hpp"

#include "recording_backend.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::version;

TEST_CASE("expr_json round-trips a field leaf") {
    const auto leaf = (status == std::string("apr")).node;
    REQUIRE(leaf);
    const nlohmann::json json = exprToJson(*leaf);
    CHECK(json["kind"] == "field");
    CHECK(json["op"] == "eq");
    CHECK(json["key"] == "status");
    CHECK(json["value"] == "apr");

    const auto restored = exprFromJson(json);
    REQUIRE(restored);
    CHECK(restored->kind == ExprKind::Field);
    CHECK(restored->op == ExprOp::Eq);
    CHECK(restored->key == "status");
    REQUIRE(restored->value);
    CHECK(std::get<std::string>(*restored->value) == "apr");
}

TEST_CASE("expr_json round-trips a group") {
    const auto expr = ((status == std::string("apr")) && contains(name, "comp")).node;
    REQUIRE(expr);
    const nlohmann::json json = exprToJson(*expr);
    const auto restored = exprFromJson(json);
    REQUIRE(restored);
    CHECK(restored->kind == ExprKind::Group);
    CHECK(restored->op == ExprOp::And);
    REQUIRE(restored->args.size() == 2);
    CHECK(restored->args[0]->key == "status");
    CHECK(restored->args[1]->op == ExprOp::Contains);
}

TEST_CASE("expr_json round-trips an in list and an in_last period") {
    const auto listed = isIn(status, std::vector<std::string>{"ip", "rev"}).node;
    REQUIRE(listed);
    const nlohmann::json listJson = exprToJson(*listed);
    CHECK(listJson["op"] == "in");
    CHECK(listJson["value"] == nlohmann::json::array({"ip", "rev"}));
    const auto listBack = exprFromJson(listJson);
    REQUIRE(listBack);
    REQUIRE(listBack->value);
    CHECK(std::get<std::vector<std::string>>(*listBack->value).size() == 2);

    const auto recent = inLast(createdAt, 7, "DAY").node;
    REQUIRE(recent);
    const nlohmann::json periodJson = exprToJson(*recent);
    CHECK(periodJson["value"]["count"] == 7);
    CHECK(periodJson["value"]["unit"] == "DAY");
    const auto periodBack = exprFromJson(periodJson);
    REQUIRE(periodBack);
    REQUIRE(periodBack->value);
    CHECK(std::get<Period>(*periodBack->value) == Period{7, "DAY"});
}

TEST_CASE("expr_json round-trips every remaining leaf and group operator") {
    auto roundTrip = [](const std::shared_ptr<const ExprNode> &node) {
        REQUIRE(node);
        return exprFromJson(exprToJson(*node));
    };

    {
        const auto restored = roundTrip(startsWith(name, "paw").node);
        CHECK(restored->op == ExprOp::StartsWith);
        CHECK(std::get<std::string>(*restored->value) == "paw");
    }
    {
        const auto restored = roundTrip(endsWith(name, "_comp").node);
        CHECK(restored->op == ExprOp::EndsWith);
        CHECK(std::get<std::string>(*restored->value) == "_comp");
    }
    {
        const auto restored = roundTrip((status != std::string("omt")).node);
        CHECK(restored->op == ExprOp::Ne);
        CHECK(std::get<std::string>(*restored->value) == "omt");
    }
    {
        const auto restored =
            roundTrip(isNotIn(status, std::vector<std::string>{"omt", "na"}).node);
        CHECK(restored->op == ExprOp::NotIn);
        CHECK(
            std::get<std::vector<std::string>>(*restored->value) ==
            std::vector<std::string>({"omt", "na"}));
    }
    {
        const auto restored =
            roundTrip(isIn(openNotesCount, std::vector<std::int64_t>{0, 1}).node);
        CHECK(restored->op == ExprOp::In);
        CHECK(
            std::get<std::vector<std::int64_t>>(*restored->value) ==
            std::vector<std::int64_t>({0, 1}));
    }
    {
        const auto restored = roundTrip(lessThan(openNotesCount, std::int64_t{5}).node);
        CHECK(restored->op == ExprOp::Lt);
        CHECK(std::get<std::int64_t>(*restored->value) == 5);
    }
    {
        const auto restored = roundTrip(greaterThan(openNotesCount, std::int64_t{0}).node);
        CHECK(restored->op == ExprOp::Gt);
        CHECK(std::get<std::int64_t>(*restored->value) == 0);
    }
    {
        const auto leaf = (createdAt == "2024-03-01").node;
        REQUIRE(std::get<Date>(*leaf->value) == Date{2024, 3, 1}); ///< C++ builder stores a Date.
        const auto restored = roundTrip(leaf);
        CHECK(restored->op == ExprOp::Eq);
        CHECK(std::get<std::string>(*restored->value) == "2024-03-01"); ///< Shelf JSON keeps the day text.
        CHECK(exprToJson(*restored)["value"] == "2024-03-01");
    }
    {
        const auto restored = roundTrip(greaterThan(createdAt, "2024-03-01").node);
        CHECK(restored->op == ExprOp::Gt);
        CHECK(std::get<std::string>(*restored->value) == "2024-03-01");
    }
    {
        const auto restored = roundTrip(lessThan(createdAt, "2024-03-01").node);
        CHECK(restored->op == ExprOp::Lt);
        CHECK(std::get<std::string>(*restored->value) == "2024-03-01");
    }
    {
        const auto restored = roundTrip(inCalendarDay(createdAt, 0).node);
        CHECK(restored->op == ExprOp::InCalendarDay);
        CHECK(std::get<std::int64_t>(*restored->value) == 0);
    }
    {
        const auto restored = roundTrip(inCalendarWeek(createdAt, 0).node);
        CHECK(restored->op == ExprOp::InCalendarWeek);
        CHECK(std::get<std::int64_t>(*restored->value) == 0);
    }
    {
        const auto restored = roundTrip(inCalendarMonth(createdAt, -1).node);
        CHECK(restored->op == ExprOp::InCalendarMonth);
        CHECK(std::get<std::int64_t>(*restored->value) == -1);
    }
    {
        const auto restored = roundTrip(((status == std::string("ip")) || contains(name, "comp")).node);
        CHECK(restored->op == ExprOp::Or);
        REQUIRE(restored->args.size() == 2);
        CHECK(restored->args[0]->op == ExprOp::Eq);
        CHECK(restored->args[1]->op == ExprOp::Contains);
    }
    {
        const auto restored = roundTrip((!(status == std::string("omt"))).node);
        CHECK(restored->op == ExprOp::Not);
        REQUIRE(restored->args.size() == 1);
        CHECK(restored->args[0]->op == ExprOp::Eq);
    }
    {
        const auto restored = roundTrip(notContains(name, "temp").node);
        CHECK(restored->op == ExprOp::NotContains);
        CHECK(std::get<std::string>(*restored->value) == "temp");
    }
    {
        const auto leaf = between(openNotesCount, 0, 5).node;
        REQUIRE(std::get<IntBounds>(*leaf->value) == IntBounds{0, 5});
        const nlohmann::json json = exprToJson(*leaf);
        CHECK(json["op"] == "between");
        CHECK(json["value"] == nlohmann::json::array({0, 5}));
        const auto restored = exprFromJson(json);
        CHECK(restored->op == ExprOp::Between);
        CHECK(
            std::get<std::vector<std::int64_t>>(*restored->value) ==
            std::vector<std::int64_t>({0, 5})); ///< Shelf JSON keeps a two-int list.
    }
    {
        const auto leaf = between(createdAt, "2024-01-01", "2024-03-01").node;
        REQUIRE(std::get<DateBounds>(*leaf->value) == DateBounds{Date{2024, 1, 1}, Date{2024, 3, 1}});
        const nlohmann::json json = exprToJson(*leaf);
        CHECK(json["value"] == nlohmann::json::array({"2024-01-01", "2024-03-01"}));
        const auto restored = exprFromJson(json);
        CHECK(restored->op == ExprOp::Between);
        CHECK(
            std::get<std::vector<std::string>>(*restored->value) ==
            std::vector<std::string>({"2024-01-01", "2024-03-01"}));
    }
    {
        const auto restored = roundTrip(nameContains(author, "Ada").node);
        CHECK(restored->op == ExprOp::NameContains);
        CHECK(std::get<std::string>(*restored->value) == "Ada");
    }
    {
        const auto restored = roundTrip(nameNotContains(author, "Ad").node);
        CHECK(restored->op == ExprOp::NameNotContains);
        CHECK(std::get<std::string>(*restored->value) == "Ad");
    }
    {
        const auto restored = roundTrip(nameIs(author, "Ada").node);
        CHECK(restored->op == ExprOp::NameIs);
        CHECK(std::get<std::string>(*restored->value) == "Ada");
    }
}

TEST_CASE("expr_json rejects scope kind") {
    nlohmann::json json = {{"kind", "scope"}, {"op", "eq"}, {"key", "project"}, {"value", 1}};
    CHECK_THROWS_AS(exprFromJson(json), Error);
}

TEST_CASE("Where accepts FieldExpr built from JSON") {
    RecordingBackend backend;
    Record row;
    row.fields["id"] = std::int64_t{7};
    row.fields["name"] = std::string("v001");
    backend.next = {row};
    Session session(mappedSchema(), backend);

    const nlohmann::json json = {
        {"kind", "field"}, {"op", "eq"}, {"key", "status"}, {"value", "apr"}};
    FieldExpr<struct Version> where{exprFromJson(json)};
    const Records rows = Version().Where(where).find(session).rows;
    REQUIRE(rows.size() == 1);
    REQUIRE(backend.calls.size() == 1);
    REQUIRE(backend.calls[0].where);
    CHECK(backend.calls[0].where->key == "status");
}
