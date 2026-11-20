// SPDX-License-Identifier: Apache-2.0

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::project;

namespace {

constexpr std::size_t versionsPerSequenceTotal() {
    return FakeBackend::versionsPerSequence +
           FakeBackend::shotsPerSequence * FakeBackend::versionsPerShot;
}

constexpr std::size_t versionsPerProjectTotal() {
    return FakeBackend::sequencesPerProject * versionsPerSequenceTotal() +
           FakeBackend::assetsPerProject * FakeBackend::versionsPerAsset;
}

} // namespace

TEST_CASE("the fake backend lists twenty projects") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Project().find(session).rows;

    REQUIRE(rows.size() == FakeBackend::projectCount);
    CHECK(std::get<std::int64_t>(rows.front().at("id")) == 1);
    CHECK(std::get<std::string>(rows.front().at("name")) == "paw20");
    CHECK(std::get<std::string>(rows.front().at("code")) == "PAW20");
    CHECK(std::get<std::int64_t>(rows.back().at("id")) == 20);
    CHECK(std::get<std::string>(rows.back().at("name")) == "paw01");
    CHECK(std::get<std::string>(rows.back().at("code")) == "PAW01");
}

TEST_CASE("the fake backend applies a where clause") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records others = Project().Where(name != "paw01").find(session).rows;
    CHECK(others.size() == FakeBackend::projectCount - 1);

    const Records one = Project().Where(name == "paw07").find(session).rows;
    REQUIRE(one.size() == 1);
    CHECK(std::get<std::int64_t>(one[0].at("id")) == 14);

    const Records none = Project().Where(name == "missing").find(session).rows;
    CHECK(none.empty());
}

TEST_CASE("the fake backend lists sequences in a project") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::sequencesPerProject);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_sq020");
    CHECK(rows[0].fields.count("project") == 0);
    const auto &shots = std::get<std::vector<Link>>(rows[0].at("shots"));
    REQUIRE(shots.size() == FakeBackend::shotsPerSequence);
    CHECK(shots[0].name == "paw20_sq010_sh010");
    CHECK(shots[1].name == "paw20_sq010_sh020");
}

TEST_CASE("the fake backend counts sequences shots and assets in a project") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    CHECK(
        Sequence().In(scope::project == std::int64_t{1}).count(session) ==
        FakeBackend::sequencesPerProject);
    CHECK(
        Shot().In(scope::project == std::int64_t{1}).count(session) ==
        FakeBackend::sequencesPerProject * FakeBackend::shotsPerSequence);
    CHECK(
        Asset().In(scope::project == std::int64_t{1}).count(session) ==
        FakeBackend::assetsPerProject);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));
    CHECK(Shot().In(scope::sequence == sequenceId).count(session) == FakeBackend::shotsPerSequence);
}

TEST_CASE("the fake backend lists shots in a sequence") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(sequences.size() == FakeBackend::sequencesPerProject);
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));

    const Records rows = Shot().In(scope::sequence == sequenceId).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::shotsPerSequence);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010_sh010");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_sq010_sh020");
    CHECK(rows[0].fields.count("sequence") == 0);
}

TEST_CASE("the fake backend lists assets in a project") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Asset().In(scope::project == std::int64_t{1}).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::assetsPerProject);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_ast01");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_ast02");
    CHECK(rows[0].fields.count("project") == 0);
}

TEST_CASE("the fake backend accepts a fields projection on versions") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(sequences.empty());
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));

    const Records rows =
        Version()
            .In(scope::sequence == sequenceId)
            .Fields(version::id, version::name, version::author)
            .Limit(1)
            .find(session)
            .rows;
    REQUIRE_FALSE(rows.empty());
    CHECK(rows[0].fields.count("id") == 1);
    CHECK(rows[0].fields.count("name") == 1);
    CHECK(rows[0].fields.count("author") == 1);
}

TEST_CASE("the fake backend finds version thumbs by several ids") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records listed =
        Version().In(scope::project == std::int64_t{1}).Limit(2).find(session).rows;
    REQUIRE(listed.size() == 2);
    const auto firstId = std::get<std::int64_t>(listed[0].at("id"));
    const auto secondId = std::get<std::int64_t>(listed[1].at("id"));

    const Records rows =
        Version()
            .Where((version::id == firstId) || (version::id == secondId))
            .Fields(version::id, version::name, version::image)
            .Limit(2)
            .find(session)
            .rows;
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].fields.count("image") == 1);
    CHECK(rows[1].fields.count("image") == 1);
    CHECK(std::get<std::string>(rows[0].at("image_provider")) == "file");
}

TEST_CASE("the fake backend lists versions in a sequence including shot versions") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(sequences.size() == FakeBackend::sequencesPerProject);
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));

    const Records rows = Version().In(scope::sequence == sequenceId).find(session).rows;

    REQUIRE(rows.size() == versionsPerSequenceTotal());
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010_v001");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_sq010_v002");
    CHECK(std::get<std::string>(rows[2].at("name")) == "paw20_sq010_sh010_v001");
    CHECK(rows[0].fields.count("sequence") == 0);
    CHECK(rows[0].fields.count("author") == 1);
    CHECK(rows[0].fields.count("image") == 1);
    CHECK(std::get<std::string>(rows[0].at("image_provider")) == "file");
    CHECK(rows[0].fields.count("frames") == 1);
    CHECK(rows[0].fields.count("movie") == 1);
    CHECK(rows[0].fields.count("uploaded_movie") == 1);
    CHECK(std::get<std::string>(rows[0].at("frame_range")) == "1001-1005");
    CHECK(
        std::get<std::string>(rows[0].at("frames")).find("fake_media/") != std::string::npos);
    CHECK(std::get<std::string>(rows[0].at("frames")).find("####.jpg") != std::string::npos);
    CHECK(rows[1].fields.count("author") == 1);
    CHECK(rows[1].fields.count("sites") == 1);
    CHECK(rows[1].fields.count("image") == 1);
}

TEST_CASE("the fake backend lists versions in a shot") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records shots = Shot().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(shots.size() == FakeBackend::sequencesPerProject * FakeBackend::shotsPerSequence);
    const auto shotId = std::get<std::int64_t>(shots[0].at("id"));

    const Records rows = Version().In(scope::shot == shotId).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::versionsPerShot);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010_sh010_v001");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_sq010_sh010_v002");
}

TEST_CASE("the fake backend lists versions in an asset") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records assets = Asset().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(assets.size() == FakeBackend::assetsPerProject);
    const auto assetId = std::get<std::int64_t>(assets[0].at("id"));

    const Records rows = Version().In(scope::asset == assetId).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::versionsPerAsset);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_ast01_v001");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_ast01_v002");
}

TEST_CASE("the fake backend lists playlists in a project") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;

    REQUIRE(rows.size() == FakeBackend::playlistsPerProject);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_pl01");
    CHECK(std::get<std::string>(rows[1].at("name")) == "paw20_pl02");
    CHECK(rows[0].fields.count("project") == 0);
    CHECK(rows[0].fields.count("author") == 1);
    CHECK(rows[0].fields.count("departments") == 1);
    CHECK(rows[0].fields.count("category") == 1);
    CHECK(rows[0].fields.count("created_at") == 1);
    CHECK(rows[0].fields.count("open_notes_count") == 1);
}

TEST_CASE("the fake backend attaches version links on each playlist") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records playlists = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(playlists.size() == FakeBackend::playlistsPerProject);
    const auto playlistId = std::get<std::int64_t>(playlists[0].at("id"));
    const auto &links = std::get<std::vector<Link>>(playlists[0].at("versions"));
    const Records members = Version().In(scope::playlist == playlistId).find(session).rows;
    REQUIRE(links.size() == members.size());
    REQUIRE_FALSE(links.empty());
}

TEST_CASE("the fake backend lists versions in a playlist via weak membership") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records playlists = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE(playlists.size() == FakeBackend::playlistsPerProject);
    const auto playlistId = std::get<std::int64_t>(playlists[0].at("id"));

    const Records rows = Version().In(scope::playlist == playlistId).find(session).rows;

    // Every `_v001` under the project: 2 sequence + 4 shot + 2 asset.
    constexpr std::size_t expected =
        FakeBackend::sequencesPerProject +
        FakeBackend::sequencesPerProject * FakeBackend::shotsPerSequence +
        FakeBackend::assetsPerProject;
    REQUIRE(rows.size() == expected);
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010_v001");
}

TEST_CASE("In(sequence) and In(playlist) combine with and") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(sequences.empty());
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));
    const Records playlists = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(playlists.empty());
    const auto playlistId = std::get<std::int64_t>(playlists[0].at("id"));

    const Records rows =
        Version()
            .In(scope::sequence == sequenceId)
            .In(scope::playlist == playlistId)
            .find(session).rows;

    // Sequence `_v001` plus its two shot `_v001` versions are in pl01.
    REQUIRE(rows.size() == 1 + FakeBackend::shotsPerSequence);
    for (const Record &row : rows) {
        const std::string name = std::get<std::string>(row.at("name"));
        CHECK(name.find("paw20_sq010") == 0);
        CHECK(name.size() >= 5);
        CHECK(name.compare(name.size() - 5, 5, "_v001") == 0);
    }
}

TEST_CASE("a playlist member stays findable by its strong parent") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records sequences = Sequence().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(sequences.empty());
    const auto sequenceId = std::get<std::int64_t>(sequences[0].at("id"));

    const Records bySequence = Version().In(scope::sequence == sequenceId).find(session).rows;
    REQUIRE_FALSE(bySequence.empty());
    const std::string name = std::get<std::string>(bySequence[0].at("name"));

    const Records playlists = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(playlists.empty());
    const auto playlistId = std::get<std::int64_t>(playlists[0].at("id"));
    const Records byPlaylist = Version().In(scope::playlist == playlistId).find(session).rows;

    bool found = false;
    for (const Record &row : byPlaylist) {
        if (std::get<std::string>(row.at("name")) == name) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("the fake backend lists all versions in a project with one find") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records rows = Version().In(scope::project == std::int64_t{1}).find(session).rows;

    REQUIRE(rows.size() == versionsPerProjectTotal());
    CHECK(std::get<std::string>(rows[0].at("name")) == "paw20_sq010_v001");
    CHECK(std::get<std::string>(rows.back().at("name")) == "paw20_ast02_v002");
}

TEST_CASE("the fake backend pages projects by id") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Page first = Project().Limit(5).find(session);
    REQUIRE(first.rows.size() == 5);
    REQUIRE(first.next_cursor.has_value());
    CHECK(std::get<std::int64_t>(first.rows[0].at("id")) == 1);
    CHECK(std::get<std::int64_t>(first.rows[4].at("id")) == 5);
    CHECK(*first.next_cursor == "5");

    const Page second = Project().Limit(5).After(*first.next_cursor).find(session);
    REQUIRE(second.rows.size() == 5);
    CHECK(std::get<std::int64_t>(second.rows[0].at("id")) == 6);
    REQUIRE(second.next_cursor.has_value());
    CHECK(*second.next_cursor == "10");

    const Page last = Project().Limit(10).After("15").find(session);
    REQUIRE(last.rows.size() == 5);
    CHECK(std::get<std::int64_t>(last.rows.back().at("id")) == 20);
    CHECK_FALSE(last.next_cursor.has_value());
}

TEST_CASE("the fake backend pages versions with a playlist filter") {
    FakeBackend backend;
    Session session(Schema::load(PMS_SCHEMA_PATH), backend);

    const Records playlists = Playlist().In(scope::project == std::int64_t{1}).find(session).rows;
    REQUIRE_FALSE(playlists.empty());
    const auto playlistId = std::get<std::int64_t>(playlists[0].at("id"));

    const Records all = Version().In(scope::playlist == playlistId).find(session).rows;
    REQUIRE(all.size() > 1);

    const Page first = Version().In(scope::playlist == playlistId).Limit(1).find(session);
    REQUIRE(first.rows.size() == 1);
    REQUIRE(first.next_cursor.has_value());
    CHECK(std::get<std::int64_t>(first.rows[0].at("id")) ==
          std::get<std::int64_t>(all[0].at("id")));

    const Page second =
        Version().In(scope::playlist == playlistId).Limit(1).After(*first.next_cursor).find(session);
    REQUIRE(second.rows.size() == 1);
    CHECK(std::get<std::int64_t>(second.rows[0].at("id")) ==
          std::get<std::int64_t>(all[1].at("id")));
}
