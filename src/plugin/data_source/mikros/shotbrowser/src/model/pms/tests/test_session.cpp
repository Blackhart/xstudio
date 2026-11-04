// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include "recording_backend.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::project;

namespace {

Record projectRow(std::int64_t id, const std::string &name) {
    Record row;
    row.fields["id"] = id;
    row.fields["name"] = name;
    row.fields["status"] = std::string("ip");
    return row;
}

} // namespace

TEST_CASE("find sends the ir and returns the backend records") {
    RecordingBackend backend;
    backend.next = {projectRow(12, "paw3"), projectRow(13, "paw4")};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Project().Where(status == "ip").find(session).rows;

    REQUIRE(backend.calls.size() == 1);
    const char *expected = R"(op: find
entity: project
in:
where:
  field status eq "ip"
)";
    CHECK(dump(backend.calls[0]) == expected);
    REQUIRE(rows.size() == 2);
    CHECK(std::get<std::int64_t>(rows[0].at("id")) == 12);
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw4");
}

TEST_CASE("find without a where is a full project list") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw3")};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Project().find(session).rows;

    REQUIRE(backend.calls.size() == 1);
    CHECK(backend.calls[0].op == Op::Find);
    const bool hasWhere = static_cast<bool>(backend.calls[0].where);
    CHECK_FALSE(hasWhere);
    REQUIRE(rows.size() == 1);
}

TEST_CASE("find of nothing is an empty vector") {
    RecordingBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);
    const Records rows = Project().Where(name == "missing").find(session).rows;
    CHECK(rows.empty());
}

TEST_CASE("find rejects a record without identity") {
    RecordingBackend backend;
    Record row;
    row.fields["status"] = std::string("ip");
    backend.next = {row};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);
    CHECK_THROWS_WITH_AS(Project().find(session), "record missing \"id\"", Error);
}

TEST_CASE("where id is a legal filter") {
    RecordingBackend backend;
    backend.next = {projectRow(42, "paw3")};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);
    const Records rows = Project().Where(id == std::int64_t{42}).find(session).rows;
    REQUIRE(rows.size() == 1);
    CHECK(std::get<std::int64_t>(rows[0].at("id")) == 42);
}

TEST_CASE("find versions in a sequence sends a scope leaf by id") {
    RecordingBackend backend;
    Record version;
    version.fields["id"] = std::int64_t{7};
    version.fields["name"] = std::string("paw01_sq010_v001");
    backend.next = {version};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Version().In(scope::sequence == std::int64_t{1}).find(session).rows;

    REQUIRE(backend.calls.size() == 1);
    const char *expected = R"(op: find
entity: version
in:
  scope sequence eq 1
where:
)";
    CHECK(dump(backend.calls[0]) == expected);
    REQUIRE(rows.size() == 1);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw01_sq010_v001");
}
