cmake_minimum_required(VERSION 3.16...3.21)

project(obs-ndi VERSION 4.11.1)

set(PLUGIN_VERSION "${PROJECT_VERSION}")
add_definitions(-DPLUGIN_VERSION="${PLUGIN_VERSION}")

add_library(${CMAKE_PROJECT_NAME} MODULE)

set(PLUGIN_AUTHOR "Stephane Lepin")
set(MACOS_BUNDLEID "fr.palakis.${CMAKE_PROJECT_NAME}")
set(LINUX_MAINTAINER_EMAIL "stephane.lepin@gmail.com")

# Add your custom source files here - header files are optional and only required for visibility
# e.g. in Xcode or Visual Studio
if (TARGET libobs) 
	set(NDI_EXTERNAL FALSE)
	set(NDI_PLUGIN_DIRECTORY "${OBS_PLUGIN_DESTINATION}")
	set(NDI_DATA_DIRECTORY   "${OBS_DATA_DESTINATION}/obs-plugins/obs-ndi")
else ()
	set(NDI_EXTERNAL TRUE)
	include(external/FindLibObs.cmake)
	find_package(LibObs REQUIRED)
endif ()

include(cmake/ObsPluginHelpers.cmake)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/obs-ndi.cpp
          src/obs-ndi.cpp
          src/obs-ndi-source.cpp
          src/obs-ndi-output.cpp
          src/obs-ndi-filter.cpp
          src/premultiplied-alpha-filter.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/ndi)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)


# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(OS_WINDOWS)
  configure_file(cmake/bundle/windows/installer-Windows.iss.in
                 ${CMAKE_BINARY_DIR}/installer-Windows.generated.iss)

  configure_file(cmake/bundle/windows/resource.rc.in ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc)

  if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4)
  endif()
  # --- End of section ---

  # -- macOS specific build settings and tasks --
elseif(OS_MACOS)
  configure_file(cmake/bundle/macos/installer-macos.pkgproj.in
                 ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)

  set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
  set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
  set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")

  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall)
  # --- End of section ---

  # --- Linux-specific build settings and tasks ---
else()
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall)
endif()
# --- End of section ---

setup_plugin_target(${CMAKE_PROJECT_NAME})
