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
    CHECK(mapping.findSchema("shot")->findLink("sequence")->shotgrid == "sg_sequence");
}
