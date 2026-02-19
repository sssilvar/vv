cmake_minimum_required(VERSION 3.14)

project(VtkViewer VERSION 1.1.0)

set(CMAKE_AUTOMOC ON)

option(VV_FETCH_DEPS "Fetch fmt and cxxopts with CMake when missing" ON)
option(VV_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(VV_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

if(VV_FETCH_DEPS)
  include(FetchContent)
endif()

find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
  if(NOT VV_FETCH_DEPS)
    find_package(fmt CONFIG REQUIRED)
  else()
    message(STATUS "fmt not found. Fetching with FetchContent...")
    FetchContent_Declare(
      fmt
      GIT_REPOSITORY https://github.com/fmtlib/fmt.git
      GIT_TAG 11.0.2
    )
    FetchContent_MakeAvailable(fmt)
  endif()
endif()

find_package(cxxopts CONFIG QUIET)
if(NOT cxxopts_FOUND)
  if(NOT VV_FETCH_DEPS)
    find_package(cxxopts CONFIG REQUIRED)
  else()
    message(STATUS "cxxopts not found. Fetching with FetchContent...")
    FetchContent_Declare(
      cxxopts
      GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
      GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(cxxopts)
  endif()
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  if(NOT VV_FETCH_DEPS)
    find_package(nlohmann_json CONFIG REQUIRED)
  else()
    message(STATUS "nlohmann_json not found. Fetching with FetchContent...")
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

if(TARGET nlohmann_json AND NOT TARGET nlohmann_json::nlohmann_json)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

find_package(VTK QUIET COMPONENTS
  CommonCore
  CommonDataModel
  CommonColor
  IOLegacy
  IOXML
  FiltersCore
  RenderingCore
  RenderingOpenGL2
  InteractionStyle
  GUISupportQt
  IOPLY
  RenderingAnnotation
)

if(NOT VTK_FOUND)
  message(FATAL_ERROR
    "VTK was not found. Install VTK development files and reconfigure. "
    "Examples: macOS (brew): brew install vtk; Ubuntu/Debian: sudo apt install libvtk9-dev. "
    "If VTK is installed in a non-standard location, set CMAKE_PREFIX_PATH or VTK_DIR."
  )
endif()

if(TARGET Qt5::Core)
  set(QT_VERSION_MAJOR 5)
  find_package(Qt5 REQUIRED COMPONENTS Widgets)
elseif(TARGET Qt6::Core)
  set(QT_VERSION_MAJOR 6)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
else()
  find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
  find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()

file(GLOB VV_ALL_SOURCES "src/*.cpp")
set(VV_CORE_SOURCES ${VV_ALL_SOURCES})
list(REMOVE_ITEM VV_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main_qt.cpp"
)

add_executable(vv src/main_qt.cpp ${VV_CORE_SOURCES} src/include/ColorBarWidget.h)
target_include_directories(vv PRIVATE src/include)

if(VV_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(vv PRIVATE /W4)
    if(VV_WARNINGS_AS_ERRORS)
      target_compile_options(vv PRIVATE /WX)
    endif()
  else()
    target_compile_options(vv PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wformat=2
      -Wnull-dereference
      -Wdouble-promotion
    )
    if(VV_WARNINGS_AS_ERRORS)
      target_compile_options(vv PRIVATE -Werror)
    endif()
  endif()
endif()

target_link_libraries(vv PRIVATE fmt::fmt)
target_link_libraries(vv PRIVATE
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::CommonColor
  VTK::IOLegacy
  VTK::IOXML
  VTK::FiltersCore
  VTK::RenderingCore
  VTK::RenderingOpenGL2
  VTK::InteractionStyle
  VTK::GUISupportQt
  VTK::RenderingAnnotation
  Qt${QT_VERSION_MAJOR}::Widgets
  cxxopts::cxxopts
)

vtk_module_autoinit(
  TARGETS vv
  MODULES ${VTK_LIBRARIES}
)

add_custom_target(run
    COMMAND $<TARGET_FILE:vv>
    DEPENDS vv
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running the application"
)

file(GLOB_RECURSE VV_FORMAT_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE} -i ${VV_FORMAT_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting C++ sources with clang-format"
  )
else()
  message(STATUS "clang-format not found; 'format' target will not be available.")
endif()

string(TIMESTAMP VV_BUILD_DATE "%Y-%m-%d")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/include/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.h
  @ONLY
)
include_directories(${CMAKE_CURRENT_BINARY_DIR})


if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install path prefix." FORCE)
endif()

install(TARGETS vv RUNTIME DESTINATION bin)
