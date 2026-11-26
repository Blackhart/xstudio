// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/shotgrid/field_schema.hpp"

#include <doctest/doctest.h>

using namespace pms;

TEST_CASE("schema entity name is snake_case for the schema URL") {
    CHECK(shotGridSchemaEntityName("Version") == "version");
    CHECK(shotGridSchemaEntityName("Playlist") == "playlist");
    CHECK(shotGridSchemaEntityName("PublishedFile") == "published_file");
    CHECK(shotGridSchemaEntityName("HumanUser") == "human_user");
    CHECK(shotGridSchemaEntityName("CustomNonProjectEntity01") == "custom_non_project_entity_01");
}

TEST_CASE("field path leaf is the last type and the last field") {
    const ShotGridFieldPathLeaf direct = splitShotGridFieldPath("sg_departments", "Playlist");
    CHECK(direct.owner == "Playlist");
    CHECK(direct.field == "sg_departments");

    const ShotGridFieldPathLeaf deep =
        splitShotGridFieldPath("sg_published_file.PublishedFile.sg_sites", "Version");
    CHECK(deep.owner == "PublishedFile");
    CHECK(deep.field == "sg_sites");

    const ShotGridFieldPathLeaf longer = splitShotGridFieldPath(
        "entity.Shot.sg_sequence.Sequence.code", "Version");
    CHECK(longer.owner == "Sequence");
    CHECK(longer.field == "code");

    CHECK_THROWS_AS(splitShotGridFieldPath("entity.Shot", "Version"), Error);
}

TEST_CASE("schema field json reads wrapped data_type and valid_types") {
    const nlohmann::json body = {
        {"data",
         {{"data_type", {{"value", "multi_entity"}, {"editable", false}}},
          {"properties",
           {{"valid_types",
             {{"value", nlohmann::json::array({"Site", "CustomEntity02"})},
              {"editable", false}}}}}}},
        {"links", {{"self", "/api/v1.1/schema/published_file/fields/sg_sites"}}}};
    const ShotGridFieldInfo info = shotGridFieldInfoFromJson(body);
    CHECK(info.data_type == "multi_entity");
    REQUIRE(info.valid_types.size() == 2);
    CHECK(info.valid_types[0] == "Site");
    CHECK(info.valid_types[1] == "CustomEntity02");
}

TEST_CASE("schema field json allows a missing valid_types list") {
    const nlohmann::json body = {
        {"data", {{"data_type", {{"value", "entity"}, {"editable", false}}}}}};
    const ShotGridFieldInfo info = shotGridFieldInfoFromJson(body);
    CHECK(info.data_type == "entity");
    CHECK(info.valid_types.empty());
}

TEST_CASE("field schema cache fetches each pair once") {
    ShotGridFieldSchemaCache cache;
    int fetches = 0;
    const auto fetch = [&fetches](const std::string &owner, const std::string &field) {
        ++fetches;
        CHECK(owner == "PublishedFile");
        CHECK(field == "sg_sites");
        return nlohmann::json{
            {"data",
             {{"data_type", {{"value", "multi_entity"}}},
              {"properties",
               {{"valid_types", {{"value", nlohmann::json::array({"CustomNonProjectEntity01"})}}}}}}}};
    };
    const ShotGridFieldInfo first = cachedFieldInfo(cache, "PublishedFile", "sg_sites", fetch);
    const ShotGridFieldInfo second = cachedFieldInfo(cache, "PublishedFile", "sg_sites", fetch);
    CHECK(fetches == 1);
    CHECK(first.data_type == "multi_entity");
    REQUIRE(second.valid_types.size() == 1);
    CHECK(second.valid_types[0] == "CustomNonProjectEntity01");

    const auto other = [&fetches](const std::string &, const std::string &) {
        ++fetches;
        return nlohmann::json{{"data", {{"data_type", {{"value", "entity"}}}}}};
    };
    const ShotGridFieldInfo author = cachedFieldInfo(cache, "Version", "created_by", other);
    CHECK(fetches == 2);
    CHECK(author.data_type == "entity");
}
