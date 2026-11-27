// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace pms;

TEST_CASE("schema loads the project entity") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    REQUIRE(schema.entities().size() == 7);

    const EntitySpec *project = schema.entity("project");
    REQUIRE(project != nullptr);
    CHECK_FALSE(project->creatable);
    CHECK(project->scope.empty());

    const FieldSpec *id = project->field("id");
    REQUIRE(id != nullptr);
    CHECK(id->filter);
    CHECK_FALSE(id->canSet);
    CHECK(id->type.empty()); ///< The schema file does not name a type.

    const FieldSpec *name = project->field("name");
    REQUIRE(name != nullptr);
    CHECK(name->filter);
    CHECK(name->canSet);

    const FieldSpec *code = project->field("code");
    REQUIRE(code != nullptr);
    CHECK(code->filter);
    CHECK(code->canSet);

    const FieldSpec *status = project->field("status");
    REQUIRE(status != nullptr);
    CHECK(status->filter);
    CHECK_FALSE(status->canSet);
    REQUIRE(status->show.size() == 1);
    CHECK(status->show[0] == "Active");

    const FieldSpec *type = project->field("type");
    REQUIRE(type != nullptr);
    CHECK(type->filter);
    CHECK_FALSE(type->canSet);
    REQUIRE(type->show.size() == 2);
    CHECK(type->show[0] == "Feature Film");
    CHECK(type->show[1] == "Episodic");
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
    const FieldSpec *shots = sequence->field("shots");
    REQUIRE(shots != nullptr);
    CHECK(shots->filter);
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
    const FieldSpec *versions = playlist->field("versions");
    REQUIRE(versions != nullptr);
    CHECK(versions->filter);
    const FieldSpec *openNotes = playlist->field("open_notes_count");
    REQUIRE(openNotes != nullptr);
    CHECK(openNotes->filter);
    const FieldSpec *departments = playlist->field("departments");
    REQUIRE(departments != nullptr);
    CHECK(departments->filter);
    CHECK(playlist->field("created_at") != nullptr);
    CHECK(playlist->field("category") != nullptr);
    const FieldSpec *author = playlist->field("author");
    REQUIRE(author != nullptr);
    CHECK(author->filter);
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
    const FieldSpec *project = version->field("project");
    REQUIRE(project != nullptr);
    CHECK(schema.allowsInParent(*version, "shot"));
    CHECK(schema.allowsInParent(*version, "sequence"));
    CHECK(schema.allowsInParent(*version, "asset"));
    CHECK(schema.allowsInParent(*version, "playlist"));
    CHECK(schema.allowsInParent(*version, "project"));
    CHECK_FALSE(schema.allowsInParent(*version, "episode"));

    const FieldSpec *id = version->field("id");
    REQUIRE(id != nullptr);
    CHECK_FALSE(id->canSet);

    const FieldSpec *name = version->field("name");
    REQUIRE(name != nullptr);
    CHECK(name->filter);

    const FieldSpec *image = version->field("image");
    REQUIRE(image != nullptr);
    CHECK_FALSE(image->filter);
    REQUIRE(image->providers.size() == 2);
    CHECK(image->providers[0] == "http");
    CHECK(image->providers[1] == "file");

    const FieldSpec *author = version->field("author");
    REQUIRE(author != nullptr);
    CHECK(author->filter);

    const FieldSpec *status = version->field("status");
    REQUIRE(status != nullptr);
    CHECK(status->filter);

    const FieldSpec *created = version->field("created_at");
    REQUIRE(created != nullptr);
    CHECK(created->filter);

    const FieldSpec *notes = version->field("notes");
    REQUIRE(notes != nullptr);
    CHECK(notes->filter);

    const FieldSpec *openNotes = version->field("open_notes");
    REQUIRE(openNotes != nullptr);
    CHECK(openNotes->filter);

    const FieldSpec *openNotesCount = version->field("open_notes_count");
    REQUIRE(openNotesCount != nullptr);
    CHECK_FALSE(openNotesCount->filter);

    const FieldSpec *sites = version->field("sites");
    REQUIRE(sites != nullptr);
    CHECK(sites->filter);

    const FieldSpec *playlists = version->field("playlists");
    REQUIRE(playlists != nullptr);
    CHECK(playlists->filter);

    const FieldSpec *synced = version->field("synced_to_client");
    REQUIRE(synced != nullptr);
    CHECK(synced->filter);

    const FieldSpec *frames = version->field("frames");
    REQUIRE(frames != nullptr);
    REQUIRE(frames->providers.size() == 1);
    CHECK(frames->providers[0] == "fileSeq");

    const FieldSpec *movie = version->field("movie");
    REQUIRE(movie != nullptr);
    REQUIRE(movie->providers.size() == 1);
    CHECK(movie->providers[0] == "file");

    const FieldSpec *uploaded = version->field("uploaded_movie");
    REQUIRE(uploaded != nullptr);
    REQUIRE(uploaded->providers.size() == 1);
    CHECK(uploaded->providers[0] == "signedurl");
}

TEST_CASE("generated proxies use the schema keys") {
    CHECK(std::string(project::id.key) == "id");
    CHECK(std::string(project::name.key) == "name");
    CHECK(std::string(project::code.key) == "code");
    CHECK(std::string(project::status.key) == "status");
    CHECK(std::string(project::type.key) == "type");
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
    CHECK(std::string(playlist::author.key) == "author");
    CHECK(std::string(playlist::openNotesCount.key) == "open_notes_count");
    CHECK(Playlist().ir().entity == "playlist");

    CHECK(std::string(version::id.key) == "id");
    CHECK(std::string(version::name.key) == "name");
    CHECK(std::string(version::image.key) == "image");
    CHECK(std::string(version::openNotesCount.key) == "open_notes_count");
    CHECK(Version().ir().entity == "version");
    CHECK(std::string(Project::entityName) == "project");
    CHECK(std::string(Sequence::entityName) == "sequence");
    CHECK(std::string(Shot::entityName) == "shot");
    CHECK(std::string(Asset::entityName) == "asset");
    CHECK(std::string(Playlist::entityName) == "playlist");
    CHECK(std::string(Note::entityName) == "note");
    CHECK(Note().ir().entity == "note");
    CHECK(std::string(note::name.key) == "name");
    CHECK(std::string(note::content.key) == "content");
    CHECK(std::string(note::author.key) == "author");
    CHECK(std::string(note::createdAt.key) == "created_at");
    CHECK(std::string(note::status.key) == "status");
    CHECK(std::string(note::to.key) == "to");
    CHECK(std::string(note::department.key) == "department");
}

TEST_CASE("schema shelf flags and note status colours") {
    const Schema schema = Schema::load(PMS_SCHEMA_PATH);
    CHECK_FALSE(schema.entity("project")->shelf);
    CHECK(schema.entity("sequence")->shelf);
    CHECK(schema.entity("shot")->shelf);
    CHECK(schema.entity("asset")->shelf);
    CHECK(schema.entity("playlist")->shelf);
    CHECK(schema.entity("version")->shelf);

    const EntitySpec *note = schema.entity("note");
    REQUIRE(note != nullptr);
    CHECK_FALSE(note->creatable);
    CHECK_FALSE(note->shelf);
    REQUIRE(note->scope.size() == 1);
    CHECK(note->scope[0] == "version");
    CHECK(schema.allowsInParent(*note, "version"));

    const FieldSpec *status = note->field("status");
    REQUIRE(status != nullptr);
    CHECK(status->filter);
    CHECK_FALSE(status->canSet);
    CHECK(status->colors.at("opn") == "#f9a825");
    CHECK(status->colors.at("rev") == "#00838f");
    CHECK(status->colors.at("rtk") == "#c62828");
    CHECK(status->colors.size() == 10);
    CHECK(status->labels.at("opn") == "Open");
    CHECK(status->labels.at("artrev") == "Pending Art Director");
    CHECK(status->labels.size() == 10);

    const FieldSpec *attachments = note->field("attachments");
    REQUIRE(attachments != nullptr);
    CHECK(attachments->filter);
}
