// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"

namespace xstudio::mikros_shotbrowser {

class MikrosShotBrowserPlugin : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("a022285c-2451-4a29-8535-74c2b63ee0d7");

    MikrosShotBrowserPlugin(caf::actor_config &cfg, const utility::JsonStore &init_settings);
    ~MikrosShotBrowserPlugin() override = default;
};

} // namespace xstudio::mikros_shotbrowser
