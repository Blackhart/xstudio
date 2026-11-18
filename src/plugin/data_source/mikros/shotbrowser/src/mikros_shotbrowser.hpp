// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser.hpp
 * @brief Plugin actor registered with xSTUDIO. The screens live in ShotBrowser.
 */
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <memory>

namespace xstudio::mikros_shotbrowser {

class ShotBrowser;

/**
 * @brief Plugin actor that builds the panel and forwards each event to ShotBrowser.
 */
class MikrosShotBrowser : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("a022285c-2451-4a29-8535-74c2b63ee0d7");

    /**
     * @brief Creates the panel actor, publishes its attributes, and loads projects.
     * @param cfg CAF actor config.
     * @param init_settings Plugin init JSON from xSTUDIO.
     */
    MikrosShotBrowser(caf::actor_config &cfg, const utility::JsonStore &init_settings);

    /**
     * @brief Drops the actor. In-flight loads are ignored because the panel is gone.
     */
    ~MikrosShotBrowser() override;

  private:
    /**
     * @brief Delivers each worker page to ShotBrowser.
     * @return Handlers merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Forwards a panel edit to ShotBrowser.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value reloads a list.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

    std::unique_ptr<ShotBrowser> shot_browser_; ///< Tabs, screens, and their signals.
};

} // namespace xstudio::mikros_shotbrowser
