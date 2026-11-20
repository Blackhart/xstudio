// SPDX-License-Identifier: Apache-2.0

#include "pms/error.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/mapping.hpp"
#include "pms/shotgrid/translate.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace pms;
using namespace pms::project;

namespace {

ShotGridMapping filledMapping() {
    return ShotGridMapping::load(PMS_SHOTGRID_MAPPING_FIXTURE_PATH);
}

} // namespace

TEST_CASE("filters for an unfiltered project find") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Project().ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    CHECK(filters.is_null());
}

TEST_CASE("filters for project where code") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Project().Where(code == "PAW01").ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "code");
    CHECK(filters[1] == "is");
    CHECK(filters[2] == "PAW01");
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

TEST_CASE("filters for version in project use the direct project field") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().In(scope::project == std::int64_t{4}).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    REQUIRE(filters.size() == 3);
    CHECK(filters[0] == "project");
    CHECK(filters[1] == "is");
    CHECK(filters[2]["type"] == "Project");
    CHECK(filters[2]["id"] == 4);
}

TEST_CASE("filters for version in sequence include versions on its shots") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().In(scope::sequence == std::int64_t{8}).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_object());
    CHECK(filters["logical_operator"] == "or");
    REQUIRE(filters["conditions"].size() == 2);
    CHECK(filters["conditions"][0][0] == "entity.Shot.sg_sequence");
    CHECK(filters["conditions"][0][2]["type"] == "Sequence");
    CHECK(filters["conditions"][0][2]["id"] == 8);
    CHECK(filters["conditions"][1][0] == "entity");
    CHECK(filters["conditions"][1][2]["type"] == "Sequence");
    CHECK(filters["conditions"][1][2]["id"] == 8);
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
    const ShotGridSchema *project = mapping.findSchema("project");
    REQUIRE(project != nullptr);
    const nlohmann::json row = {
        {"type", "Project"},
        {"id", 12},
        {"attributes",
         {{"name", "paw01"}, {"code", "PAW01"}}}};
    const Record record = shotgridRecordFromJson(row, *project);
    CHECK(std::get<std::int64_t>(record.at("id")) == 12);
    CHECK(std::get<std::string>(record.at("name")) == "paw01");
    CHECK(std::get<std::string>(record.at("code")) == "PAW01");
}

TEST_CASE("version image copies the url and the mapping provider") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 4},
        {"attributes", {{"code", "v001"}, {"image", "https://example.test/a.jpg"}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    CHECK(std::get<std::string>(record.at("name")) == "v001");
    CHECK(std::get<std::string>(record.at("image")) == "https://example.test/a.jpg");
    CHECK(std::get<std::string>(record.at("image_provider")) == "http");
}

TEST_CASE("version card fields copy entities, open notes count, the client flag, and sites") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 4},
        {"attributes",
         {{"code", "v001"},
          {"created_by", {{"type", "HumanUser"}, {"id", 7}, {"name", "Ada"}}},
          {"created_at", "2024-03-01T12:00:00Z"},
          {"frame_range", "1001-1100"},
          {"description", "Comp review"},
          {"sg_task.Task.step", {{"type", "Step"}, {"id", 3}, {"name", "Comp"}}},
          {"sg_status_list", "ip"},
          {"sg_status_artistique", "apr"},
          {"open_notes_count", 2},
          {"sg_synced_to_client", true},
          {"sg_published_file.PublishedFile.sg_sites",
           nlohmann::json::array({"Paris", "Montreal"})}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    CHECK(std::get<std::string>(record.at("author")) == "Ada");
    CHECK(std::get<std::string>(record.at("created_at")) == "2024-03-01T12:00:00Z");
    CHECK(std::get<std::string>(record.at("frame_range")) == "1001-1100");
    CHECK(std::get<std::string>(record.at("description")) == "Comp review");
    CHECK(std::get<std::string>(record.at("step")) == "Comp");
    CHECK(std::get<std::string>(record.at("status")) == "ip");
    CHECK(std::get<std::string>(record.at("artistic_status")) == "apr");
    CHECK(std::get<std::int64_t>(record.at("open_notes_count")) == 2);
    CHECK(std::get<std::int64_t>(record.at("synced_to_client")) == 1);
    CHECK(std::get<std::string>(record.at("sites")) == "Paris,Montreal");
}

TEST_CASE("uploaded movie keeps a signed url from a string or an object") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 4},
        {"attributes",
         {{"code", "v001"},
          {"sg_path_to_frames", "/shows/paw/sh010.####.exr"},
          {"sg_path_to_movie", "/shows/paw/sh010.mov"},
          {"sg_uploaded_movie_mp4",
           {{"url", "https://example.test/signed.mp4"}, {"name", "sh010.mp4"}}}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    CHECK(std::get<std::string>(record.at("frames")) == "/shows/paw/sh010.####.exr");
    CHECK(std::get<std::string>(record.at("frames_provider")) == "fileSeq");
    CHECK(std::get<std::string>(record.at("movie")) == "/shows/paw/sh010.mov");
    CHECK(std::get<std::string>(record.at("movie_provider")) == "file");
    CHECK(std::get<std::string>(record.at("uploaded_movie")) == "https://example.test/signed.mp4");
    CHECK(std::get<std::string>(record.at("uploaded_movie_provider")) == "signedurl");
}

TEST_CASE("sequence shots become a list of links") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *sequence = mapping.findSchema("sequence");
    REQUIRE(sequence != nullptr);
    const nlohmann::json row = {
        {"type", "Sequence"},
        {"id", 8},
        {"attributes",
         {{"code", "sq010"},
          {"shots",
           nlohmann::json::array(
               {{{"id", 501}, {"name", "sq010_sh010"}, {"type", "Shot"}},
                {{"id", 502}, {"name", "sq010_sh020"}, {"type", "Shot"}}})}}}};
    const Record record = shotgridRecordFromJson(row, *sequence);
    CHECK(std::get<std::string>(record.at("name")) == "sq010");
    const auto &shots = std::get<std::vector<Link>>(record.at("shots"));
    REQUIRE(shots.size() == 2);
    CHECK(shots[0].id == 501);
    CHECK(shots[0].name == "sq010_sh010");
    CHECK(shots[1].id == 502);
    CHECK(shots[1].name == "sq010_sh020");
}

TEST_CASE("sequence shots are read from relationships data") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *sequence = mapping.findSchema("sequence");
    REQUIRE(sequence != nullptr);
    // REST keeps multi-entity fields under relationships.<field>.data.
    const nlohmann::json row = {
        {"type", "Sequence"},
        {"id", 8},
        {"attributes", {{"code", "sq010"}}},
        {"relationships",
         {{"shots",
           {{"data",
             nlohmann::json::array(
                 {{{"id", 501}, {"name", "sq010_sh010"}, {"type", "Shot"}},
                  {{"id", 502}, {"name", "sq010_sh020"}, {"type", "Shot"}}})}}}}}};
    const Record record = shotgridRecordFromJson(row, *sequence);
    CHECK(std::get<std::string>(record.at("name")) == "sq010");
    const auto &shots = std::get<std::vector<Link>>(record.at("shots"));
    REQUIRE(shots.size() == 2);
    CHECK(shots[0].id == 501);
    CHECK(shots[0].name == "sq010_sh010");
    CHECK(shots[1].id == 502);
    CHECK(shots[1].name == "sq010_sh020");
}

TEST_CASE("playlist versions become a list of links") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *playlist = mapping.findSchema("playlist");
    REQUIRE(playlist != nullptr);
    const nlohmann::json row = {
        {"type", "Playlist"},
        {"id", 3},
        {"attributes",
         {{"code", "pl01"},
          {"versions",
           nlohmann::json::array(
               {{{"id", 701}, {"name", "v001"}, {"type", "Version"}},
                {{"id", 702}, {"name", "v002"}, {"type", "Version"}}})}}}};
    const Record record = shotgridRecordFromJson(row, *playlist);
    CHECK(std::get<std::string>(record.at("name")) == "pl01");
    const auto &versions = std::get<std::vector<Link>>(record.at("versions"));
    REQUIRE(versions.size() == 2);
    CHECK(versions[0].id == 701);
    CHECK(versions[1].id == 702);
}

TEST_CASE("playlist header fields map from ShotGrid") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *playlist = mapping.findSchema("playlist");
    REQUIRE(playlist != nullptr);
    const nlohmann::json row = {
        {"type", "Playlist"},
        {"id", 3},
        {"attributes",
         {{"code", "pl01"},
          {"created_at", "2024-03-01T12:30:00Z"},
          {"sg_category", "Dailies"},
          {"open_notes_count", 2}}},
        {"relationships",
         {{"created_by",
           {{"data", {{"type", "HumanUser"}, {"id", 7}, {"name", "Ada"}}}}},
          {"sg_departments",
           {{"data",
             nlohmann::json::array(
                 {{{"id", 1}, {"name", "Animation"}, {"type", "Department"}},
                  {{"id", 2}, {"name", "Lighting"}, {"type", "Department"}}})}}}}}};
    const Record record = shotgridRecordFromJson(row, *playlist);
    CHECK(std::get<std::string>(record.at("name")) == "pl01");
    CHECK(std::get<std::string>(record.at("author")) == "Ada");
    CHECK(std::get<std::string>(record.at("created_at")) == "2024-03-01T12:30:00Z");
    CHECK(std::get<std::string>(record.at("category")) == "Dailies");
    CHECK(std::get<std::int64_t>(record.at("open_notes_count")) == 2);
    CHECK(std::get<std::string>(record.at("departments")) == "Animation,Lighting");
}

TEST_CASE("version author is read from relationships data") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 4},
        {"attributes", {{"code", "v001"}}},
        {"relationships",
         {{"created_by",
           {{"data", {{"type", "HumanUser"}, {"id", 7}, {"name", "Ada"}}}}}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    CHECK(std::get<std::string>(record.at("author")) == "Ada");
}

TEST_CASE("an empty shot list leaves the field absent") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *sequence = mapping.findSchema("sequence");
    REQUIRE(sequence != nullptr);
    const nlohmann::json row = {
        {"type", "Sequence"},
        {"id", 8},
        {"attributes", {{"code", "sq010"}}},
        {"relationships", {{"shots", {{"data", nlohmann::json::array()}}}}}};
    const Record record = shotgridRecordFromJson(row, *sequence);
    CHECK(record.fields.count("shots") == 0);
}

TEST_CASE("shotgrid field names keep every mapped column without a projection") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const std::vector<std::string> all = shotgridFieldNames(*version);
    CHECK(std::find(all.begin(), all.end(), "image") != all.end());
    CHECK(std::find(all.begin(), all.end(), "sg_path_to_frames") != all.end());
    CHECK(std::find(all.begin(), all.end(), "sg_uploaded_movie_mp4") != all.end());
}

TEST_CASE("shotgrid field names honour a list-element projection") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const std::vector<std::string> domain = {
        "id",
        "name",
        "author",
        "created_at",
        "frame_range",
        "description",
        "step",
        "status",
        "artistic_status",
        "sites",
        "open_notes_count",
        "synced_to_client"};
    const std::vector<std::string> names = shotgridFieldNames(*version, &domain);
    CHECK(std::find(names.begin(), names.end(), "image") == names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_path_to_frames") == names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_path_to_movie") == names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_uploaded_movie_mp4") == names.end());
    CHECK(std::find(names.begin(), names.end(), "code") != names.end());
    CHECK(std::find(names.begin(), names.end(), "created_by") != names.end());
}

TEST_CASE("shotgrid field names honour a media projection") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const std::vector<std::string> domain = {
        "id", "name", "frames", "movie", "uploaded_movie"};
    const std::vector<std::string> names = shotgridFieldNames(*version, &domain);
    CHECK(std::find(names.begin(), names.end(), "image") == names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_path_to_frames") != names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_path_to_movie") != names.end());
    CHECK(std::find(names.begin(), names.end(), "sg_uploaded_movie_mp4") != names.end());
}
