// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/shotgrid/mapping.hpp"

#include <doctest/doctest.h>

using namespace pms;

TEST_CASE("placeholder mapping loads and requireComplete fails") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_PATH);
    REQUIRE(mapping.entity("project") != nullptr);
    REQUIRE(mapping.entity("version") != nullptr);
    CHECK(mapping.entity("project")->shotgrid.empty());
    CHECK(mapping.entity("version")->link("playlist") != nullptr);
    CHECK_THROWS_WITH_AS(
        mapping.requireComplete(),
        "shotgrid mapping: entity \"project\" has empty shotgrid",
        Error);
}

TEST_CASE("filled fixture parses and requireComplete passes") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_FIXTURE_PATH);
    CHECK_NOTHROW(mapping.requireComplete());
    CHECK(mapping.entity("project")->shotgrid == "Project");
    CHECK(mapping.entity("version")->field("name")->shotgrid == "code");
    CHECK(mapping.entity("shot")->link("sequence")->shotgrid == "sg_sequence");
}
