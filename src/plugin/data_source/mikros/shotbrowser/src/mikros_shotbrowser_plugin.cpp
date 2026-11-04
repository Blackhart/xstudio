// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser_plugin.cpp
 * @brief xSTUDIO registration entry for the Mikros Shot Browser data source.
 */

#include "model/context/mikros_shotbrowser_context.hpp"

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

XSTUDIO_PLUGIN_DECLARE_BEGIN()

XSTUDIO_REGISTER_PLUGIN(
    MikrosShotBrowserContext,
    MikrosShotBrowserContext::PLUGIN_UUID,
    MikrosShotBrowser,
    plugin_manager::PluginFlags::PF_DATA_SOURCE,
    true,
    Mikros,
    Mikros Shot Browser panel.,
    0.1.0)

XSTUDIO_PLUGIN_DECLARE_END()
