project(mikros_shotbrowser VERSION ${XSTUDIO_GLOBAL_VERSION} LANGUAGES CXX)

find_package(Qt6 COMPONENTS Core Widgets Qml Quick REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
	mikros_shotbrowser_plugin.cpp
	qml/MikrosShotBrowser.1/resources/mikros_shotbrowser.qrc
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_link_libraries(${PROJECT_NAME}
	PUBLIC
		xstudio::ui::qml::helper
		xstudio::utility
		xstudio::plugin_manager
		Qt6::Core
		Qt6::Qml
		Qt6::Quick
		Qt6::Widgets
)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS_NO_SHARED true)

qt_add_qml_module(${PROJECT_NAME}
	URI MikrosShotBrowser
	OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MikrosShotBrowser
	VERSION 1.0
	NO_GENERATE_PLUGIN_SOURCE
	NO_PLUGIN_OPTIONAL
	RESOURCE_PREFIX /
	PLUGIN_TARGET ${PROJECT_NAME}
	QML_FILES
		qml/MikrosShotBrowser.1/MikrosShotBrowserPanel.qml
)

default_plugin_options(${PROJECT_NAME})
