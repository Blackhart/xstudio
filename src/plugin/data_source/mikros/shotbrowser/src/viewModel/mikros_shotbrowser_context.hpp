// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/mikros_shotbrowser_context.hpp
 * @brief Plugin actor registered with xSTUDIO. Screen behavior lives in the other view model headers.
 */
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <memory>

namespace xstudio::mikros_shotbrowser {

struct PanelState;

/**
 * @brief Plugin actor that builds the panel and forwards each event to its screen.
 */
class MikrosShotBrowserContext : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("a022285c-2451-4a29-8535-74c2b63ee0d7");

    /**
     * @brief Creates the panel actor, publishes its attributes, and loads projects.
     * @param cfg CAF actor config.
     * @param init_settings Plugin init JSON from xSTUDIO.
     */
    MikrosShotBrowserContext(caf::actor_config &cfg, const utility::JsonStore &init_settings);

    /**
     * @brief Drops the actor. In-flight loads are ignored because the panel is gone.
     */
    ~MikrosShotBrowserContext() override;

  private:
    /**
     * @brief Delivers each worker page to the screen that owns that catalog.
     * @return Handlers merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Forwards a panel edit to the screen that owns the attribute.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value reloads a list.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

    std::unique_ptr<PanelState> state_; ///< Catalogs and attributes the screens share.
};

} // namespace xstudio::mikros_shotbrowser
