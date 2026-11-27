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

TEST_CASE("filters for version where name contains") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().Where(contains(pms::version::name, "comp")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "code");
    CHECK(filters[1] == "contains");
    CHECK(filters[2] == "comp");
}

TEST_CASE("filters for version status in a list") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().Where(isIn(pms::version::status, std::vector<std::string>{"ip", "rev"})).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "sg_status_list");
    CHECK(filters[1] == "in");
    CHECK(filters[2] == nlohmann::json::array({"ip", "rev"}));
}

TEST_CASE("filters for version created in the last week") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().Where(inLast(pms::version::createdAt, 7, "DAY")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "created_at");
    CHECK(filters[1] == "in_last");
    CHECK(filters[2] == nlohmann::json::array({7, "DAY"}));
}

TEST_CASE("filters for version created this calendar week") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().Where(inCalendarWeek(pms::version::createdAt, 0)).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "created_at");
    CHECK(filters[1] == "in_calendar_week");
    CHECK(filters[2] == 0);
}

TEST_CASE("filters for version with open notes") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().Where(greaterThan(pms::version::openNotesCount, std::int64_t{0})).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "open_notes_count");
    CHECK(filters[1] == "greater_than");
    CHECK(filters[2] == 0);
}

TEST_CASE("filters for every leaf operator match the REST relation and value shape") {
    const ShotGridMapping mapping = filledMapping();
    auto filtersOf = [&](Ir ir) {
        ir.op = Op::Find;
        return shotgridFiltersFromIr(ir, mapping);
    };

    // string: is / is_not / starts_with / ends_with
    {
        const nlohmann::json filters = filtersOf(Project().Where(code != "PAW01").ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "is_not", "PAW01"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(startsWith(pms::version::name, "paw")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "starts_with", "paw"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(endsWith(pms::version::name, "_comp")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "ends_with", "_comp"}));
    }

    // string / int: not_in and in on an int field
    {
        const nlohmann::json filters = filtersOf(
            Version()
                .Where(isNotIn(pms::version::status, std::vector<std::string>{"omt", "na"}))
                .ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array(
                             {"sg_status_list", "not_in", nlohmann::json::array({"omt", "na"})}));
    }
    {
        const nlohmann::json filters = filtersOf(
            Version()
                .Where(isIn(pms::version::openNotesCount, std::vector<std::int64_t>{0, 1, 2}))
                .ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array(
                             {"open_notes_count", "in", nlohmann::json::array({0, 1, 2})}));
    }
    {
        const nlohmann::json filters = filtersOf(
            Version()
                .Where(isNotIn(pms::version::openNotesCount, std::vector<std::int64_t>{0}))
                .ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array(
                             {"open_notes_count", "not_in", nlohmann::json::array({0})}));
    }

    // int: less_than
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(lessThan(pms::version::openNotesCount, std::int64_t{5})).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"open_notes_count", "less_than", 5}));
    }

    // datetime: is / is_not / greater_than / less_than / calendar day and month
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(pms::version::createdAt == "2024-03-01").ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "is", "2024-03-01"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(pms::version::createdAt != "2024-03-01").ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "is_not", "2024-03-01"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(greaterThan(pms::version::createdAt, "2024-03-01")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "greater_than", "2024-03-01"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(lessThan(pms::version::createdAt, "2024-03-01")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "less_than", "2024-03-01"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(inCalendarDay(pms::version::createdAt, 0)).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "in_calendar_day", 0}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(inCalendarMonth(pms::version::createdAt, -1)).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_at", "in_calendar_month", -1}));
    }
}

TEST_CASE("filters for not_contains between and name_* match the REST shapes") {
    const ShotGridMapping mapping = filledMapping();
    auto filtersOf = [&](Ir ir) {
        ir.op = Op::Find;
        return shotgridFiltersFromIr(ir, mapping);
    };

    {
        const nlohmann::json filters =
            filtersOf(Version().Where(notContains(pms::version::name, "temp")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "not_contains", "temp"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(between(pms::version::openNotesCount, 0, 5)).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"open_notes_count", "between", nlohmann::json::array({0, 5})}));
    }
    {
        const nlohmann::json filters = filtersOf(
            Version().Where(between(pms::version::createdAt, "2024-01-01", "2024-03-01")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array(
                             {"created_at",
                              "between",
                              nlohmann::json::array({"2024-01-01", "2024-03-01"})}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(nameContains(pms::version::author, "Ada")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_by", "name_contains", "Ada"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(nameNotContains(pms::version::author, "Ad")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_by", "name_not_contains", "Ad"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(nameIs(pms::version::author, "Ada")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"created_by", "name_is", "Ada"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(notBetween(pms::version::frameCount, 1, 4)).ir());
        REQUIRE(filters.is_array());
        CHECK(filters ==
              nlohmann::json::array({"frame_count", "not_between", nlohmann::json::array({1, 4})}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Version().Where(typeIs(pms::version::entity, "Shot")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"entity", "type_is", "Shot"}));
    }
    {
        const Link shot{10, "sh010", "Shot", {}};
        const nlohmann::json filters =
            filtersOf(Version().Where(pms::version::entity == shot).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array(
                             {"entity", "is", nlohmann::json{{"type", "Shot"}, {"id", 10}}}));
    }
}

TEST_CASE("filters for and or and not match the REST hash and is inversion") {
    const ShotGridMapping mapping = filledMapping();
    auto filtersOf = [&](Ir ir) {
        ir.op = Op::Find;
        return shotgridFiltersFromIr(ir, mapping);
    };

    {
        const nlohmann::json filters =
            filtersOf(Project().Where(code == "PAW01" && name != "archive").ir());
        REQUIRE(filters.is_object());
        CHECK(filters["logical_operator"] == "and");
        REQUIRE(filters["conditions"].size() == 2);
        CHECK(filters["conditions"][0] == nlohmann::json::array({"code", "is", "PAW01"}));
        CHECK(filters["conditions"][1] == nlohmann::json::array({"name", "is_not", "archive"}));
    }
    {
        const nlohmann::json filters =
            filtersOf(Project().Where(code == "PAW01" || code == "PAW02").ir());
        REQUIRE(filters.is_object());
        CHECK(filters["logical_operator"] == "or");
        REQUIRE(filters["conditions"].size() == 2);
        CHECK(filters["conditions"][0] == nlohmann::json::array({"code", "is", "PAW01"}));
        CHECK(filters["conditions"][1] == nlohmann::json::array({"code", "is", "PAW02"}));
    }
    {
        const nlohmann::json filters = filtersOf(Project().Where(!(code == "PAW01")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "is_not", "PAW01"}));
    }
    {
        const nlohmann::json filters = filtersOf(Project().Where(!(code != "PAW01")).ir());
        REQUIRE(filters.is_array());
        CHECK(filters == nlohmann::json::array({"code", "is", "PAW01"}));
    }
    {
        Ir ir = Project().Where(!(contains(name, "paw"))).ir();
        ir.op = Op::Find;
        CHECK_THROWS_WITH_AS(
            shotgridFiltersFromIr(ir, mapping),
            "shotgrid: not only inverts is and is_not",
            Error);
    }
}

TEST_CASE("filters for sequence in project by id") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Sequence().In(project::id == std::int64_t{7}).ir();
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
    Ir ir = Version().In(project::id == std::int64_t{4}).ir();
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
    Ir ir = Version().In(sequence::id == std::int64_t{8}).ir();
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
                .In(shot::id == std::int64_t{9})
                .In(playlist::id == std::int64_t{3})
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

TEST_CASE("filters for version in shot name contains use mapping link path") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Version().In(contains(shot::name, "sh0010")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    CHECK(filters[0] == "entity.Shot.code");
    CHECK(filters[1] == "contains");
    CHECK(filters[2] == "sh0010");
}

TEST_CASE("filters for shot in project inherit through sequence") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Shot().In(project::id == std::int64_t{1}).ir();
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
    const Link &author = std::get<Link>(record.at("author"));
    CHECK(author.id == 7);
    CHECK(author.name == "Ada");
    CHECK(author.type == "HumanUser");
    CHECK(std::get<std::string>(record.at("created_at")) == "2024-03-01T12:00:00Z");
    CHECK(std::get<std::string>(record.at("frame_range")) == "1001-1100");
    CHECK(std::get<std::string>(record.at("description")) == "Comp review");
    const Link &step = std::get<Link>(record.at("step"));
    CHECK(step.id == 3);
    CHECK(step.name == "Comp");
    CHECK(step.type == "Step");
    CHECK(std::get<std::string>(record.at("status")) == "ip");
    CHECK(std::get<std::string>(record.at("artistic_status")) == "apr");
    CHECK(std::get<std::int64_t>(record.at("open_notes_count")) == 2);
    CHECK(std::get<bool>(record.at("synced_to_client")));
    const auto &sites = std::get<std::vector<Link>>(record.at("sites"));
    REQUIRE(sites.size() == 2);
    CHECK(sites[0].name == "Paris");
    CHECK(sites[1].name == "Montreal");
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
    CHECK(shots[0].type == "Shot");
    CHECK(shots[1].id == 502);
    CHECK(shots[1].name == "sq010_sh020");
    CHECK(shots[1].type == "Shot");
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
    CHECK(shots[0].type == "Shot");
    CHECK(shots[1].id == 502);
    CHECK(shots[1].name == "sq010_sh020");
    CHECK(shots[1].type == "Shot");
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
    CHECK(versions[0].type == "Version");
    CHECK(versions[1].id == 702);
    CHECK(versions[1].type == "Version");
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
    const Link &author = std::get<Link>(record.at("author"));
    CHECK(author.id == 7);
    CHECK(author.name == "Ada");
    CHECK(author.type == "HumanUser");
    CHECK(std::get<std::string>(record.at("created_at")) == "2024-03-01T12:30:00Z");
    CHECK(std::get<std::string>(record.at("category")) == "Dailies");
    CHECK(std::get<std::int64_t>(record.at("open_notes_count")) == 2);
    const auto &departments = std::get<std::vector<Link>>(record.at("departments"));
    REQUIRE(departments.size() == 2);
    CHECK(departments[0].name == "Animation");
    CHECK(departments[0].type == "Department");
    CHECK(departments[1].name == "Lighting");
    CHECK(departments[1].type == "Department");
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
    const Link &author = std::get<Link>(record.at("author"));
    CHECK(author.id == 7);
    CHECK(author.name == "Ada");
    CHECK(author.type == "HumanUser");
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

TEST_CASE("version project and entity keep id name and type") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json projectData = {{"type", "Project"}, {"id", 1}, {"name", "Show"}};
    const nlohmann::json entityData = {{"type", "Shot"}, {"id", 10}, {"name", "SH010"}};
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 99},
        {"attributes", {{"code", "v003"}}},
        {"relationships",
         {{"project", {{"data", projectData}}}, {"entity", {{"data", entityData}}}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    const Link &project = std::get<Link>(record.at("project"));
    CHECK(project.id == 1);
    CHECK(project.name == "Show");
    CHECK(project.type == "Project");
    CHECK(record.fields.count("project_id") == 0);
    const Link &entity = std::get<Link>(record.at("entity"));
    CHECK(entity.id == 10);
    CHECK(entity.name == "SH010");
    CHECK(entity.type == "Shot");
    CHECK(record.fields.count("entity_id") == 0);
    CHECK(record.fields.count("entity_type") == 0);
}

TEST_CASE("version playlists and notes keep id name and type") {
    const ShotGridMapping mapping = filledMapping();
    const ShotGridSchema *version = mapping.findSchema("version");
    REQUIRE(version != nullptr);
    const nlohmann::json row = {
        {"type", "Version"},
        {"id", 4},
        {"attributes", {{"code", "v001"}}},
        {"relationships",
         {{"playlists",
           {{"data",
             nlohmann::json::array(
                 {{{"id", 3}, {"name", "dailies"}, {"type", "Playlist"}},
                  {{"id", 8}, {"name", "client"}, {"type", "Playlist"}}})}}},
          {"notes",
           {{"data",
             nlohmann::json::array(
                 {{{"id", 40}, {"name", "retake"}, {"type", "Note"}}})}}}}}};
    const Record record = shotgridRecordFromJson(row, *version);
    const auto &playlists = std::get<std::vector<Link>>(record.at("playlists"));
    REQUIRE(playlists.size() == 2);
    CHECK(playlists[0].id == 3);
    CHECK(playlists[0].name == "dailies");
    CHECK(playlists[0].type == "Playlist");
    CHECK(playlists[1].id == 8);
    CHECK(playlists[1].name == "client");
    const auto &notes = std::get<std::vector<Link>>(record.at("notes"));
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].id == 40);
    CHECK(notes[0].name == "retake");
    CHECK(notes[0].type == "Note");
}

namespace {

FieldSchemaResolve schemaOf(std::vector<std::pair<std::string, std::string>> &lookedUp,
                            const std::string &owner,
                            const std::string &field,
                            ShotGridFieldInfo info) {
    return [&lookedUp, owner, field, info](const std::string &gotOwner, const std::string &gotField) {
        lookedUp.emplace_back(gotOwner, gotField);
        if (gotOwner == owner && gotField == field) {
            return info;
        }
        throw Error("unexpected schema lookup \"" + gotOwner + "." + gotField + "\"");
    };
}

} // namespace

TEST_CASE("filters rewrite name_contains on a deep multi_entity field") {
    const ShotGridMapping mapping = filledMapping();
    std::vector<std::pair<std::string, std::string>> lookedUp;
    const FieldSchemaResolve resolve = schemaOf(
        lookedUp,
        "PublishedFile",
        "sg_sites",
        ShotGridFieldInfo{"multi_entity", {"CustomNonProjectEntity01"}});
    Ir ir = Version().Where(nameContains(pms::version::sites, "Paris")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping, resolve);
    REQUIRE(lookedUp.size() == 1);
    CHECK(lookedUp[0].first == "PublishedFile");
    CHECK(lookedUp[0].second == "sg_sites");
    CHECK(filters == nlohmann::json::array(
                         {"sg_published_file.PublishedFile.sg_sites.CustomNonProjectEntity01."
                          "cached_display_name",
                          "contains",
                          "Paris"}));
}

TEST_CASE("filters rewrite name_not_contains on a direct multi_entity field") {
    const ShotGridMapping mapping = filledMapping();
    std::vector<std::pair<std::string, std::string>> lookedUp;
    const FieldSchemaResolve resolve = schemaOf(
        lookedUp, "Playlist", "sg_departments", ShotGridFieldInfo{"multi_entity", {"Department"}});
    Ir ir = Playlist().Where(nameNotContains(pms::playlist::departments, "Comp")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping, resolve);
    REQUIRE(lookedUp.size() == 1);
    CHECK(lookedUp[0].first == "Playlist");
    CHECK(lookedUp[0].second == "sg_departments");
    CHECK(filters == nlohmann::json::array(
                         {"sg_departments.Department.cached_display_name", "not_contains", "Comp"}));
}

TEST_CASE("filters keep name_contains on an entity field") {
    const ShotGridMapping mapping = filledMapping();
    std::vector<std::pair<std::string, std::string>> lookedUp;
    const FieldSchemaResolve resolve =
        schemaOf(lookedUp, "Version", "created_by", ShotGridFieldInfo{"entity", {"HumanUser"}});
    Ir ir = Version().Where(nameContains(pms::version::author, "Ada")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping, resolve);
    REQUIRE(lookedUp.size() == 1);
    CHECK(lookedUp[0].first == "Version");
    CHECK(lookedUp[0].second == "created_by");
    CHECK(filters == nlohmann::json::array({"created_by", "name_contains", "Ada"}));
}

TEST_CASE("filters or every valid type of a multi_entity field") {
    const ShotGridMapping mapping = filledMapping();
    std::vector<std::pair<std::string, std::string>> lookedUp;
    const FieldSchemaResolve resolve = schemaOf(
        lookedUp,
        "PublishedFile",
        "sg_sites",
        ShotGridFieldInfo{"multi_entity", {"Site", "CustomEntity02"}});
    Ir ir = Version().Where(nameNotContains(pms::version::sites, "Par")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping, resolve);
    REQUIRE(filters.is_object());
    CHECK(filters["logical_operator"] == "or");
    REQUIRE(filters["conditions"].size() == 2);
    CHECK(filters["conditions"][0] ==
          nlohmann::json::array({"sg_published_file.PublishedFile.sg_sites.Site.cached_display_name",
                                 "not_contains",
                                 "Par"}));
    CHECK(filters["conditions"][1] ==
          nlohmann::json::array(
              {"sg_published_file.PublishedFile.sg_sites.CustomEntity02.cached_display_name",
               "not_contains",
               "Par"}));
}

TEST_CASE("filters rewrite name_contains on a multi_entity parent field") {
    const ShotGridMapping mapping = filledMapping();
    std::vector<std::pair<std::string, std::string>> lookedUp;
    const FieldSchemaResolve resolve = schemaOf(
        lookedUp, "Playlist", "sg_departments", ShotGridFieldInfo{"multi_entity", {"Department"}});
    Ir ir = Version().In(nameContains(pms::playlist::departments, "Comp")).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping, resolve);
    REQUIRE(lookedUp.size() == 1);
    CHECK(lookedUp[0].first == "Playlist");
    CHECK(lookedUp[0].second == "sg_departments");
    CHECK(filters == nlohmann::json::array(
                         {"playlists.Playlist.sg_departments.Department.cached_display_name",
                          "contains",
                          "Comp"}));
}

TEST_CASE("filters for note in version use note_links") {
    const ShotGridMapping mapping = filledMapping();
    Ir ir = Note().In(version::id == std::int64_t{12}).ir();
    ir.op = Op::Find;
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping);
    REQUIRE(filters.is_array());
    REQUIRE(filters.size() == 3);
    CHECK(filters[0] == "note_links");
    CHECK(filters[1] == "is");
    CHECK(filters[2]["type"] == "Version");
    CHECK(filters[2]["id"] == 12);
}

TEST_CASE("update body for version status") {
    Ir ir;
    ir.op = Op::Update;
    ir.entity = "version";
    ir.id = 12;
    ir.field = "status";
    ir.value = Value{std::string{"ip"}};
    const nlohmann::json body = shotgridUpdateBody(ir, filledMapping());
    CHECK(body["sg_status_list"] == "ip");
}

TEST_CASE("update body for artistic status") {
    Ir ir;
    ir.op = Op::Update;
    ir.entity = "version";
    ir.id = 12;
    ir.field = "artistic_status";
    ir.value = Value{std::string{"cmpt"}};
    const nlohmann::json body = shotgridUpdateBody(ir, filledMapping());
    CHECK(body["sg_status_artistique"] == "cmpt");
}
