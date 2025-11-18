cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(ENABLE_QT OFF)
set(ENABLE_FRONTEND_API ON)

include(compilerconfig)
include(defaults)
include(helpers)
include(./cmake/windows/helpers.cmake)

add_library(${PROJECT_NAME} MODULE)

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

if(ENABLE_FRONTEND_API)
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network)
  target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${PROJECT_NAME}
  PRIVATE src/plugin-main.cpp
          src/ndi-source.cpp
          src/ndi-output.cpp
          src/ndi-filter.cpp
          src/config.cpp
          src/premultiplied-alpha-filter.cpp)

set(valid_uuid FALSE)
check_uuid(${WINDOWS_APP_UUID} valid_uuid)
if(NOT valid_uuid)
  message(FATAL_ERROR "Specified Windows package UUID is not a valid UUID value: ${WINDOWS_APP_UUID}")
endif()

configure_file(
  cmake/windows/resources/installer-Windows.iss.in
  "${CMAKE_CURRENT_BINARY_DIR}/installer-Windows.generated.iss"
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/ndi)
set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
