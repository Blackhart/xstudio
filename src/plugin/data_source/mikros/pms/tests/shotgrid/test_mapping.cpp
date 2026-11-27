// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/shotgrid/mapping.hpp"

#include <doctest/doctest.h>

#include <algorithm>

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
    CHECK(
        mapping.findSchema("version")->findField("open_notes_count")->shotgrid ==
        "open_notes_count");
}

TEST_CASE("example mapping types choose the operators") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_EXAMPLE_PATH);
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    CHECK(version->findField("name")->type == "text");
    CHECK(version->findField("status")->type == "status_list");
    CHECK(version->findField("frame_count")->type == "number");
    CHECK(version->findField("cut_duration_in_sec") == nullptr);
    CHECK(version->findField("client_reviewed")->type == "checkbox");
    CHECK(version->findField("open_notes_count")->type == "summary");
    CHECK(version->findField("image")->type == "image");
    CHECK(version->findField("uploaded_movie")->type == "url");
    CHECK(version->findField("author")->type == "entity");
    CHECK(version->findField("sites")->type == "multi_entity");
    CHECK(mapping.findSchema("shot")->findField("cut_duration_in_sec")->type == "float");
    CHECK(mapping.findSchema("project")->findField("status")->type == "list");

    const std::vector<std::string> *text = mapping.opsFor("text");
    REQUIRE(text != nullptr);
    CHECK(std::find(text->begin(), text->end(), "contains") != text->end());
    const std::vector<std::string> *status = mapping.opsFor("status_list");
    REQUIRE(status != nullptr);
    CHECK(std::find(status->begin(), status->end(), "contains") == status->end());
    CHECK(std::find(status->begin(), status->end(), "in") != status->end());
    const std::vector<std::string> *number = mapping.opsFor("number");
    REQUIRE(number != nullptr);
    CHECK(std::find(number->begin(), number->end(), "not_between") != number->end());
    REQUIRE(mapping.opsFor("summary") != nullptr);
    CHECK(mapping.opsFor("summary")->empty());
    REQUIRE(mapping.opsFor("multi_entity") != nullptr);
    CHECK(std::find(mapping.opsFor("multi_entity")->begin(), mapping.opsFor("multi_entity")->end(),
                    "name_is") == mapping.opsFor("multi_entity")->end());
    CHECK(std::find(mapping.opsFor("entity")->begin(), mapping.opsFor("entity")->end(),
                    "name_is") != mapping.opsFor("entity")->end());
}

TEST_CASE("example mapping stores version images over http") {
    const ShotGridMapping mapping = ShotGridMapping::load(PMS_SHOTGRID_MAPPING_EXAMPLE_PATH);
    const ShotGridSchemaField *image = mapping.findSchema("version")->findField("image");
    REQUIRE(image != nullptr);
    CHECK(image->shotgrid == "image");
    CHECK(image->provider == "http");
}
