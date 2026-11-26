project(mikros_media_details VERSION ${XSTUDIO_GLOBAL_VERSION} LANGUAGES CXX)

find_package(Qt6 COMPONENTS Concurrent Core Gui Widgets Qml Quick Network REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
	model/dal/dal.cpp
	model/log.cpp
	mikros_media_details.cpp
	utility/detail_load.cpp
	utility/home.cpp
	utility/media_meta.cpp
	utility/notes_load.cpp
	utility/thumbnails.cpp
	viewModel/media_details.cpp
	viewModel/version_notes.cpp
	view/resources/mikros_media_details.qrc
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

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
)

set_target_properties(${PROJECT_NAME} PROPERTIES
	LINK_DEPENDS_NO_SHARED true
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "$ORIGIN"
)

add_preference(
	plugin_data_source_mikros_media_details.json
	${CMAKE_CURRENT_SOURCE_DIR}/../share/preference
	${PROJECT_NAME})

qt_add_qml_module(${PROJECT_NAME}
	URI MikrosMediaDetails
	OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MikrosMediaDetails
	VERSION 1.0
	NO_PLUGIN
	RESOURCE_PREFIX /
	QML_FILES
		view/MediaDetailsPanel.qml
		view/DetailSections.qml
		view/DetailLinkText.qml
		view/DetailFieldRow.qml
		view/StatusBadge.qml
		view/NotesList.qml
)

default_plugin_options(${PROJECT_NAME})
