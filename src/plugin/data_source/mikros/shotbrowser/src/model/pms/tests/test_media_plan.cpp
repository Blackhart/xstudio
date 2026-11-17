// SPDX-License-Identifier: Apache-2.0

#include "model/media_plan.hpp"

#include <doctest/doctest.h>

using xstudio::mikros_shotbrowser::chooseSources;

TEST_CASE("a sequence and a movie on disk are both kept") {
    const auto choice = chooseSources(true, true, true);
    CHECK(choice.frames);
    CHECK(choice.movieFile);
    CHECK_FALSE(choice.movieUrl);
}

TEST_CASE("a missing movie falls back to the signed url") {
    const auto choice = chooseSources(false, false, true);
    CHECK_FALSE(choice.frames);
    CHECK_FALSE(choice.movieFile);
    CHECK(choice.movieUrl);
}

TEST_CASE("nothing on disk and no url opens nothing") {
    const auto choice = chooseSources(false, false, false);
    CHECK_FALSE(choice.frames);
    CHECK_FALSE(choice.movieFile);
    CHECK_FALSE(choice.movieUrl);
}
