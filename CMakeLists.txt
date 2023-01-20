cmake_minimum_required(VERSION 3.16...3.21)

project(obs-pokemon-sv-helper VERSION 0.1.6)
add_library(${CMAKE_PROJECT_NAME} MODULE)

set(PLUGIN_AUTHOR "Kaito Udagawa")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS releases (used both in
# the installer and when submitting the installer for notarization)
set(MACOS_BUNDLEID "tokyo.kaito.${CMAKE_PROJECT_NAME}")

# Replace `me@contoso.com` with the maintainer email address you want to put in Linux packages
set(LINUX_MAINTAINER_EMAIL "umireon@gmail.com")

# Add your custom source files here - header files are optional and only required for visibility
# e.g. in Xcode or Visual Studio
include(ExternalProject)

string(REPLACE ";" "$<SEMICOLON>" CMAKE_OSX_ARCHITECTURES_ "${CMAKE_OSX_ARCHITECTURES}")

ExternalProject_Add(
  p
  URL https://github.com/umireon/pokemon-detector-sv/archive/refs/tags/v0.1.1.tar.gz
  BUILD_BYPRODUCTS
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pokemon-detector-sv${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_core${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_features2d${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgcodecs${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgproc${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}libpng${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zlib${CMAKE_STATIC_LIBRARY_SUFFIX}
    <INSTALL_DIR>/include/
    <INSTALL_DIR>/include/pokemon-detector-sv.h
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
             -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
             -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_}
             -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
             -DCMAKE_POSITION_INDEPENDENT_CODECMAKE_POSITION_INDEPENDENT_CODE=ON)

ExternalProject_Get_Property(p INSTALL_DIR)
add_library(pokemon-detector-sv STATIC IMPORTED)
add_dependencies(pokemon-detector-sv p)
set_target_properties(
  pokemon-detector-sv
  PROPERTIES
    IMPORTED_LOCATION
    ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pokemon-detector-sv${CMAKE_STATIC_LIBRARY_SUFFIX}
)
include_directories(${INSTALL_DIR}/include)
include_directories()
if(MSVC)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      pokemon-detector-sv
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}libpng${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_core470${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_features2d470${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgcodecs470${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgproc470${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zlib${CMAKE_STATIC_LIBRARY_SUFFIX})
else(MSVC)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      stdc++
      pokemon-detector-sv
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}libpng${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_core${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_features2d${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgcodecs${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}opencv_imgproc${CMAKE_STATIC_LIBRARY_SUFFIX}
      ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zlib${CMAKE_STATIC_LIBRARY_SUFFIX})
endif(MSVC)
if(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework Accelerate")
endif(APPLE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)

# Import libobs as main plugin dependency
find_package(libobs REQUIRED)
include(cmake/ObsPluginHelpers.cmake)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

# Uncomment those lines if you want to use Qt in your plugin
#[[
find_qt(COMPONENTS Widgets Core)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)
#]]

configure_file(src/plugin-macros.h.in ${CMAKE_SOURCE_DIR}/src/plugin-macros.generated.h)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-macros.generated.h)

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
