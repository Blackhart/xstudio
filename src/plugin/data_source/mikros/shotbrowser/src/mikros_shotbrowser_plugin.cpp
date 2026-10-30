// SPDX-License-Identifier: Apache-2.0

#include "mikros_shotbrowser_plugin.hpp"

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

MikrosShotBrowserPlugin::MikrosShotBrowserPlugin(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings) {

    make_behavior();
    connect_to_ui();

    register_ui_panel_qml(
        "Mikros Shot Browser",
        R"(
            import MikrosShotBrowser 1.0
            MikrosShotBrowserPanel {
                anchors.fill: parent
            }
        )",
        100.0f,
        "qrc:/mikros_shotbrowser_icons/mikros_shotbrowser.svg",
        5.0f);
}

XSTUDIO_PLUGIN_DECLARE_BEGIN()

XSTUDIO_REGISTER_PLUGIN(
    MikrosShotBrowserPlugin,
    MikrosShotBrowserPlugin::PLUGIN_UUID,
    MikrosShotBrowser,
    plugin_manager::PluginFlags::PF_DATA_SOURCE,
    true,
    Mikros,
    Mikros Shot Browser skeleton panel.,
    0.1.0)

XSTUDIO_PLUGIN_DECLARE_END()
