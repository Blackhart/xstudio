// SPDX-License-Identifier: Apache-2.0

#include "mapped_schema.hpp"
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
    Session session(mappedSchema(), backend);
    CHECK_THROWS_WITH_AS(Project().After("1").find(session), "cursor requires a limit", Error);
    CHECK_THROWS_WITH_AS(
        Project().Limit(0).find(session), "limit must be greater than zero", Error);
}

TEST_CASE("find sends limit and returns the backend page") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01"), projectRow(2, "paw02")};
    Session session(mappedSchema(), backend);

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
    Session session(mappedSchema(), cache);

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
    Session session(mappedSchema(), cache);

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
    Session session(mappedSchema(), cache);

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
    Session session(mappedSchema(), cache);

    CHECK(Project().find(session).rows.size() == 1);
    backend.next = {projectRow(2, "paw02")};
    const Records rows = Project().find(session).rows;

    REQUIRE(backend.calls.size() == 2);
    CHECK(std::get<std::int64_t>(rows[0].at("id")) == 2);
    CHECK(cache.size() == 0);
}

TEST_CASE("caching backend serves a second count from cache") {
    RecordingBackend backend;
    backend.next_count = 42;
    CachingBackend cache(backend);
    Session session(mappedSchema(), cache);

    CHECK(Sequence().In(project::id == std::int64_t{1}).count(session) == 42);
    backend.next_count = 99;
    CHECK(Sequence().In(project::id == std::int64_t{1}).count(session) == 42);
    REQUIRE(backend.calls.size() == 1);
    CHECK(cache.size() == 1);
}

TEST_CASE("caching backend keeps find and count entries apart") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    backend.next_count = 7;
    CachingBackend cache(backend);
    Session session(mappedSchema(), cache);

    CHECK(Project().Limit(10).find(session).rows.size() == 1);
    CHECK(Project().count(session) == 7);
    CHECK(cache.size() == 2);
    REQUIRE(backend.calls.size() == 2);
    CHECK(backend.calls[0].op == Op::Find);
    CHECK(backend.calls[1].op == Op::Count);
}

TEST_CASE("caching backend expires a count after the ttl") {
    RecordingBackend backend;
    backend.next_count = 1;
    auto tick = std::chrono::steady_clock::time_point{};
    CacheOptions options;
    options.ttl = std::chrono::seconds(5);
    CachingBackend cache(backend, options, [&tick]() { return tick; });
    Session session(mappedSchema(), cache);

    CHECK(Asset().In(project::id == std::int64_t{1}).count(session) == 1);
    tick += std::chrono::seconds(6);
    backend.next_count = 2;
    CHECK(Asset().In(project::id == std::int64_t{1}).count(session) == 2);
    REQUIRE(backend.calls.size() == 2);
}

TEST_CASE("caching backend evicts the least recently used entry") {
    RecordingBackend backend;
    CacheOptions options;
    options.max_bytes = 80 * 1024; ///< One heavy page fits. Two do not.
    CachingBackend cache(backend, options);
    Session session(mappedSchema(), cache);

    const std::string heavy(64 * 1024, 'a');
    backend.next = {projectRow(1, heavy)};
    CHECK(Project().Limit(1).find(session).rows.size() == 1);
    backend.next = {projectRow(2, heavy)};
    CHECK(Project().Limit(2).find(session).rows.size() == 1);
    CHECK(cache.size() == 1);

    backend.next = {projectRow(3, "paw03")};
    const Page page = Project().Limit(1).find(session);
    REQUIRE(backend.calls.size() == 3);
    CHECK(std::get<std::int64_t>(page.rows[0].at("id")) == 3);
}

TEST_CASE("caching backend drops entries after an update") {
    RecordingBackend backend;
    backend.next = {projectRow(1, "paw01")};
    CachingBackend cache(backend);
    Session session(mappedSchema(), cache);

    CHECK(Project().Limit(10).find(session).rows.size() == 1);
    CHECK(cache.size() == 1);

    Ir ir;
    ir.op = Op::Update;
    ir.entity = "shot";
    ir.id = 4;
    ir.field = "status";
    ir.value = Value{std::string{"ip"}};
    cache.update(ir);

    CHECK(cache.size() == 0);
    REQUIRE(backend.calls.size() == 2);
    REQUIRE(backend.calls[1].op.has_value());
    CHECK(*backend.calls[1].op == Op::Update);
    CHECK(backend.calls[1].field == "status");
}
