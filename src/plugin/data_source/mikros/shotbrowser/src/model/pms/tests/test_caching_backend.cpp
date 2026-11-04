// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include "recording_backend.hpp"

#include <doctest/doctest.h>

#include <chrono>

using namespace pms;
using namespace pms::project;

namespace {

Record projectRow(std::int64_t id, const std::string &name) {
    Record row;
    row.fields["id"] = id;
    row.fields["name"] = name;
    return row;
}

} // namespace

TEST_CASE("find dumps limit and cursor") {
    const char *expected = R"(op:
entity: project
in:
where:
limit: 5
cursor: "7"
)";
    CHECK(dump(Project().Limit(5).After("7").ir()) == expected);
}

TEST_CASE("find rejects a zero limit and a cursor without limit") {
    RecordingBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);
    CHECK_THROWS_WITH_AS(Project().After("1").find(session), "cursor requires a limit", Error);
    CHECK_THROWS_WITH_AS(
        Project().Limit(0).find(session), "limit must be greater than zero", Error);
}

TEST_CASE("find sends limit and returns the backend page") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01"), projectRow(2, "paw02")};
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Page page = Project().Limit(2).find(session);

    REQUIRE(backend.calls.size() == 1);
    const char *expected = R"(op: find
entity: project
in:
where:
limit: 2
)";
    CHECK(dump(backend.calls[0]) == expected);
    REQUIRE(page.rows.size() == 2);
    CHECK_FALSE(page.next_cursor.has_value());
}

TEST_CASE("caching backend serves a second find from cache") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    CachingBackend cache(backend);
    Session session(Schema::load(PMS_SCHEMA_PATH), cache);

    const Page first = Project().Limit(10).find(session);
    backend.next = {projectRow(99, "changed")};
    const Page second = Project().Limit(10).find(session);

    REQUIRE(backend.calls.size() == 1);
    REQUIRE(first.rows.size() == 1);
    REQUIRE(second.rows.size() == 1);
    CHECK(std::get<std::int64_t>(second.rows[0].at("id")) == 1);
    CHECK(cache.size() == 1);
}

TEST_CASE("caching backend clear forces a miss") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    CachingBackend cache(backend);
    Session session(Schema::load(PMS_SCHEMA_PATH), cache);

    CHECK(Project().Limit(10).find(session).rows.size() == 1);
    cache.clear();
    backend.next = {projectRow(2, "paw02")};
    const Page page = Project().Limit(10).find(session);

    REQUIRE(backend.calls.size() == 2);
    CHECK(std::get<std::int64_t>(page.rows[0].at("id")) == 2);
}

TEST_CASE("caching backend expires entries after the ttl") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    auto tick = std::chrono::steady_clock::time_point{};
    CacheOptions options;
    options.ttl = std::chrono::seconds(5);
    CachingBackend cache(backend, options, [&tick]() { return tick; });
    Session session(Schema::load(PMS_SCHEMA_PATH), cache);

    CHECK(Project().Limit(10).find(session).rows.size() == 1);
    tick += std::chrono::seconds(6);
    backend.next = {projectRow(2, "paw02")};
    const Page page = Project().Limit(10).find(session);

    REQUIRE(backend.calls.size() == 2);
    CHECK(std::get<std::int64_t>(page.rows[0].at("id")) == 2);
}

TEST_CASE("caching backend does not cache unlimited find") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    CachingBackend cache(backend);
    Session session(Schema::load(PMS_SCHEMA_PATH), cache);

    CHECK(Project().find(session).rows.size() == 1);
    backend.next = {projectRow(2, "paw02")};
    const Records rows = Project().find(session).rows;

    REQUIRE(backend.calls.size() == 2);
    CHECK(std::get<std::int64_t>(rows[0].at("id")) == 2);
    CHECK(cache.size() == 0);
}

TEST_CASE("caching backend evicts the least recently used entry") {
    RecordingBackend backend;
    CacheOptions options;
    options.max_entries = 1;
    options.max_bytes = 1024 * 1024;
    CachingBackend cache(backend, options);
    Session session(Schema::load(PMS_SCHEMA_PATH), cache);

    backend.next = {projectRow(1, "paw01")};
    CHECK(Project().Limit(1).find(session).rows.size() == 1);
    backend.next = {projectRow(2, "paw02")};
    CHECK(Project().Limit(2).find(session).rows.size() == 1);
    CHECK(cache.size() == 1);

    backend.next = {projectRow(3, "paw03")};
    const Page page = Project().Limit(1).find(session);
    REQUIRE(backend.calls.size() == 3);
    CHECK(std::get<std::int64_t>(page.rows[0].at("id")) == 3);
}
