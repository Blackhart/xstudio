// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/mapping.hpp"
#include "pms/shotgrid/translate.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::project;

namespace {

ShotGridMapping filledMapping() {
    return ShotGridMapping::load(PMS_SHOTGRID_MAPPING_FIXTURE_PATH);
}

} // namespace

TEST_CASE("filters for project where status") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Project().Where(status == "ip").ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "sg_status_list");
    CHECK(filters[1] == "is");
    CHECK(filters[2] == "ip");
}

TEST_CASE("filters for sequence in project by id") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Sequence().In(scope::project == std::int64_t{7}).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "project");
    CHECK(filters[1] == "is");
    CHECK(filters[2]["type"] == "Project");
    CHECK(filters[2]["id"] == 7);
}

TEST_CASE("filters for version in shot and playlist") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version()
                .In(scope::shot == std::int64_t{9})
                .In(scope::playlist == std::int64_t{3})
                .ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_object());
    CHECK(filters["logical_operator"] == "and");
    REQUIRE(filters["conditions"].is_array());
    REQUIRE(filters["conditions"].size() == 2);
    CHECK(filters["conditions"][0][0] == "entity");
    CHECK(filters["conditions"][0][2]["type"] == "Shot");
    CHECK(filters["conditions"][0][2]["id"] == 9);
    CHECK(filters["conditions"][1][0] == "playlists");
    CHECK(filters["conditions"][1][2]["type"] == "Playlist");
    CHECK(filters["conditions"][1][2]["id"] == 3);
}

TEST_CASE("filters for shot in project inherit through sequence") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Shot().In(scope::project == std::int64_t{1}).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "sg_sequence.Sequence.project");
    CHECK(filters[1] == "is");
    CHECK(filters[2]["type"] == "Project");
    CHECK(filters[2]["id"] == 1);
}

TEST_CASE("filters include greater_than cursor when limit is set") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Project().Limit(5).After("10").ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_object());
    CHECK(filters["logical_operator"] == "and");
    REQUIRE(filters["conditions"].size() == 1);
    CHECK(filters["conditions"][0][0] == "id");
    CHECK(filters["conditions"][0][1] == "greater_than");
    CHECK(filters["conditions"][0][2] == 10);
}

TEST_CASE("record maps attributes to domain fields") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridEntityMap *project = mapping.entity("project");
    REQUIRE(project != nullptr);
    const nlohmann::json row = {
        {"type", "Project"},
        {"id", 12},
        {"attributes",
         {{"name", "paw01"},
          {"code", "PAW01"},
          {"sg_status_list", "ip"},
          {"start_date", "2026-01-02"}}}};
    const Record record = shotgridRecordFromJson(row, *project);
    CHECK(std::get<std::int64_t>(record.at("id")) == 12);
    CHECK(std::get<std::string>(record.at("name")) == "paw01");
    CHECK(std::get<std::string>(record.at("status")) == "ip");
    CHECK(std::get<Date>(record.at("start_date")) == Date{2026, 1, 2});
}
