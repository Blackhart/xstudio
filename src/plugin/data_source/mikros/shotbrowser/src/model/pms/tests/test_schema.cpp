// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include <doctest/doctest.h>

using namespace pms;

TEST_CASE("schema loads the project entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    REQUIRE(schema.entities().size() == 6);

    const EntitySpec *project = schema.entity("project");
    REQUIRE(project != nullptr);
    CHECK_FALSE(project->creatable);
    CHECK(project->scope.empty());

    const FieldSpec *id = project->field("id");
    REQUIRE(id != nullptr);
    CHECK(id->type == "int");
    CHECK(id->filter);
    CHECK_FALSE(id->canSet);
    CHECK(id->ops.size() == 2);

    const FieldSpec *name = project->field("name");
    REQUIRE(name != nullptr);
    CHECK(name->type == "string");
    CHECK(name->filter);
    CHECK(name->canSet);

    const FieldSpec *start = project->field("start_date");
    REQUIRE(start != nullptr);
    CHECK(start->type == "date");
    CHECK(start->filter);
    CHECK(start->canSet);

    CHECK(project->field("delivery_date") != nullptr);
    CHECK(project->field("code") != nullptr);
    CHECK(project->field("status") != nullptr);
    CHECK(schema.entity("episode") == nullptr);
}

TEST_CASE("schema loads the sequence entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    const EntitySpec *sequence = schema.entity("sequence");
    REQUIRE(sequence != nullptr);
    CHECK_FALSE(sequence->creatable);
    REQUIRE(sequence->scope.size() == 1);
    CHECK(sequence->scope[0] == "project");
    CHECK(sequence->field("id") != nullptr);
    CHECK(sequence->field("name") != nullptr);
}

TEST_CASE("schema loads the shot entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    const EntitySpec *shot = schema.entity("shot");
    REQUIRE(shot != nullptr);
    CHECK_FALSE(shot->creatable);
    REQUIRE(shot->scope.size() == 1);
    CHECK(shot->scope[0] == "sequence");
    CHECK(shot->field("id") != nullptr);
    CHECK(shot->field("name") != nullptr);
    CHECK(schema.allowsInParent(*shot, "sequence"));
    CHECK(schema.allowsInParent(*shot, "project"));
}

TEST_CASE("schema loads the asset entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    const EntitySpec *asset = schema.entity("asset");
    REQUIRE(asset != nullptr);
    CHECK_FALSE(asset->creatable);
    REQUIRE(asset->scope.size() == 1);
    CHECK(asset->scope[0] == "project");
    CHECK(asset->field("id") != nullptr);
    CHECK(asset->field("name") != nullptr);
    CHECK(schema.allowsInParent(*asset, "project"));
}

TEST_CASE("schema loads the playlist entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    const EntitySpec *playlist = schema.entity("playlist");
    REQUIRE(playlist != nullptr);
    CHECK_FALSE(playlist->creatable);
    REQUIRE(playlist->scope.size() == 1);
    CHECK(playlist->scope[0] == "project");
    CHECK(playlist->field("id") != nullptr);
    CHECK(playlist->field("name") != nullptr);
    CHECK(schema.allowsInParent(*playlist, "project"));
}

TEST_CASE("schema loads the version entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    const EntitySpec *version = schema.entity("version");
    REQUIRE(version != nullptr);
    CHECK_FALSE(version->creatable);
    REQUIRE(version->scope.size() == 4);
    CHECK(version->scope[0] == "shot");
    CHECK(version->scope[1] == "sequence");
    CHECK(version->scope[2] == "asset");
    CHECK(version->scope[3] == "playlist");
    CHECK(version->field("project") == nullptr);
    CHECK(schema.allowsInParent(*version, "shot"));
    CHECK(schema.allowsInParent(*version, "sequence"));
    CHECK(schema.allowsInParent(*version, "asset"));
    CHECK(schema.allowsInParent(*version, "playlist"));
    CHECK(schema.allowsInParent(*version, "project"));
    CHECK_FALSE(schema.allowsInParent(*version, "episode"));

    const FieldSpec *id = version->field("id");
    REQUIRE(id != nullptr);
    CHECK(id->type == "int");
    CHECK_FALSE(id->canSet);

    const FieldSpec *name = version->field("name");
    REQUIRE(name != nullptr);
    CHECK(name->type == "string");
}

TEST_CASE("generated proxies use the schema keys") {
    CHECK(std::string(project::id.key) == "id");
    CHECK(std::string(project::name.key) == "name");
    CHECK(std::string(project::code.key) == "code");
    CHECK(std::string(project::startDate.key) == "start_date");
    CHECK(std::string(project::deliveryDate.key) == "delivery_date");
    CHECK(std::string(project::status.key) == "status");
    CHECK(Project().ir().entity == "project");

    CHECK(std::string(sequence::id.key) == "id");
    CHECK(std::string(sequence::name.key) == "name");
    CHECK(Sequence().ir().entity == "sequence");

    CHECK(std::string(shot::id.key) == "id");
    CHECK(std::string(shot::name.key) == "name");
    CHECK(Shot().ir().entity == "shot");

    CHECK(std::string(asset::id.key) == "id");
    CHECK(std::string(asset::name.key) == "name");
    CHECK(Asset().ir().entity == "asset");

    CHECK(std::string(playlist::id.key) == "id");
    CHECK(std::string(playlist::name.key) == "name");
    CHECK(Playlist().ir().entity == "playlist");

    CHECK(std::string(version::id.key) == "id");
    CHECK(std::string(version::name.key) == "name");
    CHECK(Version().ir().entity == "version");
    CHECK(std::string(scope::project.key) == "project");
    CHECK(std::string(scope::sequence.key) == "sequence");
    CHECK(std::string(scope::shot.key) == "shot");
    CHECK(std::string(scope::asset.key) == "asset");
    CHECK(std::string(scope::playlist.key) == "playlist");
}
