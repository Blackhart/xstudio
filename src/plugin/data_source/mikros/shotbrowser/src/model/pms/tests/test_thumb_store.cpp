// SPDX-License-Identifier: Apache-2.0

#include "model/thumb_store.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using xstudio::mikros_shotbrowser::thumb::ThumbKind;
using xstudio::mikros_shotbrowser::thumb::ThumbPlan;
using xstudio::mikros_shotbrowser::thumb::cacheFile;
using xstudio::mikros_shotbrowser::thumb::planThumb;
using xstudio::mikros_shotbrowser::thumb::writeBytesIfMissing;

namespace {

std::filesystem::path makeRoot() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "xstudio-mikros-thumb-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("missing image, missing file, and unknown provider write nothing") {
    const std::filesystem::path root = makeRoot();
    CHECK(planThumb(root, "", "http", 1, 2).kind == ThumbKind::None);
    CHECK(planThumb(root, "/no/such/preview.jpg", "file", 1, 2).kind == ThumbKind::None);
    CHECK(planThumb(root, "https://example.test/a.jpg", "ftp", 1, 2).kind == ThumbKind::None);
    CHECK(std::filesystem::is_empty(root));
}

TEST_CASE("an accessible file is published as itself") {
    const std::filesystem::path root = makeRoot();
    const std::filesystem::path source = root / "on-the-share.jpg";
    REQUIRE(writeBytesIfMissing(source, "pixels"));
    const ThumbPlan plan = planThumb(root, source.string(), "file", 7, 8);
    CHECK(plan.kind == ThumbKind::Ready);
    CHECK(plan.path == source);
    CHECK_FALSE(std::filesystem::exists(cacheFile(root, 7, 8)));
}

TEST_CASE("a cache file already present is not rewritten") {
    const std::filesystem::path root = makeRoot();
    const std::filesystem::path cached = cacheFile(root, 3, 9);
    REQUIRE(writeBytesIfMissing(cached, "old"));
    const ThumbPlan plan = planThumb(root, "https://example.test/a.jpg", "http", 3, 9);
    CHECK(plan.kind == ThumbKind::Ready);
    CHECK(plan.path == cached);
    CHECK(writeBytesIfMissing(cached, "new"));
    CHECK(readFile(cached) == "old");
}

TEST_CASE("two projects keep the same version id apart") {
    const std::filesystem::path root = makeRoot();
    const std::filesystem::path first = cacheFile(root, 1, 4);
    const std::filesystem::path second = cacheFile(root, 2, 4);
    CHECK(first != second);
    REQUIRE(writeBytesIfMissing(first, "one"));
    REQUIRE(writeBytesIfMissing(second, "two"));
    CHECK(first.filename() == "thumbnail.jpg");
    CHECK(second.filename() == "thumbnail.jpg");
    CHECK(readFile(first) == "one");
    CHECK(readFile(second) == "two");
}
