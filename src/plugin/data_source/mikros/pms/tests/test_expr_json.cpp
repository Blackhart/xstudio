// SPDX-License-Identifier: Apache-2.0

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
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const nlohmann::json json = {
        {"kind", "field"}, {"op", "eq"}, {"key", "status"}, {"value", "apr"}};
    FieldExpr<struct Version> where{exprFromJson(json)};
    const Records rows = Version().Where(where).find(session).rows;
    REQUIRE(rows.size() == 1);
    REQUIRE(backend.calls.size() == 1);
    REQUIRE(backend.calls[0].where);
    CHECK(backend.calls[0].where->key == "status");
}
