project(mikros_shotbrowser VERSION ${XSTUDIO_GLOBAL_VERSION} LANGUAGES CXX)

add_subdirectory(model/pms)

find_package(Qt6 COMPONENTS Concurrent Core Gui Widgets Qml Quick Network REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/fileseq.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
	model/constants.cpp
	model/dal/dal.cpp
	model/log.cpp
	mikros_shotbrowser.cpp
	utility/home.cpp
	utility/catalog.cpp
	utility/sequence_path.cpp
	utility/thumbnails.cpp
	viewModel/project_selector.cpp
	viewModel/shot_browser.cpp
	viewModel/shot_tree.cpp
	viewModel/asset_list.cpp
	viewModel/version_card.cpp
	viewModel/version_list.cpp
	viewModel/playlist_card.cpp
	viewModel/playlist_tree.cpp
	view/resources/mikros_shotbrowser.qrc
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})
add_dependencies(${PROJECT_NAME} gofileseq)

target_include_directories(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
	PUBLIC
		xstudio::ui::qml::helper
		xstudio::utility
		xstudio::session
		xstudio::plugin_manager
		Qt6::Concurrent
		Qt6::Core
		Qt6::Gui
		Qt6::Qml
		Qt6::Quick
		Qt6::Widgets
		Qt6::Network
	PRIVATE
		pms::pms
		fileseq::fileseq
)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS_NO_SHARED true)

add_preference(
	plugin_data_source_mikros_shotbrowser.json
	${CMAKE_CURRENT_SOURCE_DIR}/../share/preference
	${PROJECT_NAME})

# Install schema under share/xstudio/; dal.cpp resolves it via XSTUDIO_ROOT
# (or PMS_SCHEMA_PATH) at runtime — never bake CMAKE_CURRENT_SOURCE_DIR.
add_resource(
	schema.yaml
	${CMAKE_CURRENT_SOURCE_DIR}/model/pms/schema
	${PROJECT_NAME}
	mikros_shotbrowser)

qt_add_qml_module(${PROJECT_NAME}
	URI MikrosShotBrowser
	OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MikrosShotBrowser
	VERSION 1.0
	NO_PLUGIN
	RESOURCE_PREFIX /
	QML_FILES
		view/MikrosShotBrowserPanel.qml
		view/CatalogValue.qml
		view/NameColumn.qml
		view/ProjectSelector.qml
		view/ShotTree.qml
		view/AssetList.qml
		view/VersionList.qml
		view/VersionCard.qml
		view/PlaylistTree.qml
		view/PlaylistCard.qml
		view/OpenMediaMenu.qml
)

default_plugin_options(${PROJECT_NAME})
