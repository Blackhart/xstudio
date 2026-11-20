// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/shotgrid/mapping.hpp"

#include <doctest/doctest.h>

using namespace pms;

TEST_CASE("filled fixture parses and names are present") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_FIXTURE_PATH);
    CHECK_NOTHROW(mapping.throwIfNamesMissing());
    CHECK(mapping.findSchema("project")->shotgridName == "Project");
    CHECK(mapping.findSchema("version")->findField("name")->shotgrid == "code");
    CHECK(mapping.findSchema("version")->findField("image")->provider == "http");
    CHECK(mapping.findSchema("version")->findField("image")->shotgrid == "image");
    CHECK(mapping.findSchema("shot")->findLink("sequence")->shotgrid == "sg_sequence");
    CHECK(mapping.findSchema("sequence")->findField("shots")->shotgrid == "shots");
    CHECK(mapping.findSchema("playlist")->findField("versions")->shotgrid == "versions");
    CHECK(mapping.findSchema("playlist")->findField("departments")->shotgrid == "sg_departments");
    CHECK(mapping.findSchema("playlist")->findField("category")->shotgrid == "sg_category");
    CHECK(mapping.findSchema("playlist")->findField("author")->shotgrid == "created_by");
    CHECK(
        mapping.findSchema("playlist")->findField("open_notes_count")->shotgrid ==
        "open_notes_count");
}

TEST_CASE("example mapping stores version images over http") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_EXAMPLE_PATH);
    const ShotGridSchemaField *image = mapping.findSchema("version")->findField("image");
    REQUIRE(image != nullptr);
    CHECK(image->shotgrid == "image");
    CHECK(image->provider == "http");
}
