project(mikros_shotbrowser VERSION ${XSTUDIO_GLOBAL_VERSION} LANGUAGES CXX)

add_subdirectory(model/pms)

find_package(Qt6 COMPONENTS Core Widgets Qml Quick REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
	mikros_shotbrowser_plugin.cpp
	model/dal/dal.cpp
	model/context/mikros_shotbrowser_context.cpp
	view/qml/MikrosShotBrowser.1/resources/mikros_shotbrowser.qrc
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
		xstudio::plugin_manager
		Qt6::Core
		Qt6::Qml
		Qt6::Quick
		Qt6::Widgets
	PRIVATE
		pms::pms
)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS_NO_SHARED true)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	PMS_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/model/pms/schema/schema.yaml"
	PMS_SHOTGRID_MAPPING_PATH="${CMAKE_CURRENT_SOURCE_DIR}/model/pms/shotgrid/mapping.yaml"
)

qt_add_qml_module(${PROJECT_NAME}
	URI MikrosShotBrowser
	OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MikrosShotBrowser
	VERSION 1.0
	NO_PLUGIN
	RESOURCE_PREFIX /
	QML_FILES
		view/qml/MikrosShotBrowser.1/MikrosShotBrowserPanel.qml
)

default_plugin_options(${PROJECT_NAME})
