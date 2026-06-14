cmake_minimum_required(VERSION 3.14)

project(VtkViewer VERSION 1.2.0)

set(CMAKE_AUTOMOC ON)

# Assets and Icons
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(GENERATED_ASSETS_DIR "${ASSETS_DIR}/generated")

option(VV_FETCH_DEPS "Fetch fmt and cxxopts with CMake when missing" ON)
option(VV_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(VV_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(VV_QT_WINDOWS_DEPLOY "Run Qt windeployqt after vv links (Windows)" ON)

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
  IOImage
  IOHDF
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
    "Examples: macOS (brew): brew install vtk; "
    "Ubuntu/Debian: sudo apt install libvtk9-dev libvtk9-qt-dev "
    "(libvtk9-qt-dev is required for GUISupportQt). "
    "If VTK is installed in a non-standard location, set CMAKE_PREFIX_PATH or VTK_DIR."
  )
endif()
message(STATUS "VTK ${VTK_VERSION} found")

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

set(VV_CORE_SOURCES
  src/CartoMeshParser.cpp
  src/ColorBarWidget.cpp
  src/FSurfMeshParser.cpp
  src/JsonMeshParser.cpp
  src/LSDynaMeshParser.cpp
  src/MeshLoading.cpp
  src/MeshParser.cpp
  src/MeshRenderer.cpp
  src/PlaybackBar.cpp
  src/ScalarVizUtils.cpp
  src/TemporalSource.cpp
  src/VTKHDFMeshParser.cpp
  src/VTKMeshParser.cpp
  src/ViewerWindow.cpp
  src/XMLMeshParser.cpp
  src/mesh_utils.cpp
)

set(VV_HEADERS
  src/include/CartoMeshParser.h
  src/include/ColorBarWidget.h
  src/include/FSurfMeshParser.h
  src/include/JsonMeshParser.h
  src/include/LSDynaMeshParser.h
  src/include/MeshLoading.h
  src/include/MeshParser.h
  src/include/MeshRenderer.h
  src/include/PlaybackBar.h
  src/include/ScalarVizUtils.h
  src/include/TemporalSource.h
  src/include/VTKHDFMeshParser.h
  src/include/VTKMeshParser.h
  src/include/ViewerWindow.h
  src/include/XMLMeshParser.h
  src/include/mesh_utils.h
)

add_executable(vv src/main_qt.cpp ${VV_CORE_SOURCES} ${VV_HEADERS})

if(WIN32)
  set(VV_ICON_RC "${GENERATED_ASSETS_DIR}/vv_icon.rc")
  file(WRITE "${VV_ICON_RC}" "IDI_ICON1 ICON \"${GENERATED_ASSETS_DIR}/vv_icon.ico\"\n")
  target_sources(vv PRIVATE "${VV_ICON_RC}")
endif()

target_include_directories(vv PRIVATE src/include)

if(MSVC)
  target_compile_options(vv PRIVATE /EHsc)
  target_compile_definitions(vv PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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
      -Wconversion
      -Wsign-conversion
      -Wimplicit-fallthrough
    )
    if(VV_WARNINGS_AS_ERRORS)
      target_compile_options(vv PRIVATE -Werror)
    endif()
  endif()
endif()

target_link_libraries(vv PRIVATE fmt::fmt)
target_link_libraries(vv PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(vv PRIVATE
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::CommonColor
  VTK::IOLegacy
  VTK::IOXML
  VTK::IOImage
  VTK::IOHDF
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

# Windows + Qt: put plugins (platforms/qwindows.dll, etc.) next to vv.exe.
# vcpkg: windeployqt does not understand vcpkg's Qt layout ("Unable to find the platform plugin");
# copy the installed plugins tree instead. Non-vcpkg Qt: windeployqt is the usual tool.
if(WIN32 AND VV_QT_WINDOWS_DEPLOY AND QT_VERSION_MAJOR EQUAL 6)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    # vcpkg qtbase installs plugin roots as <triplet>/Qt6/plugins (see tools/Qt6/bin/qt.conf), not <triplet>/plugins.
    set(_vv_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(EXISTS "${_vv_vcpkg_root}/Qt6/plugins")
      set(_vv_qt_plugins "${_vv_vcpkg_root}/Qt6/plugins")
    elseif(EXISTS "${_vv_vcpkg_root}/plugins")
      set(_vv_qt_plugins "${_vv_vcpkg_root}/plugins")
    else()
      set(_vv_qt_plugins "${_vv_vcpkg_root}/Qt6/plugins")
    endif()
    add_custom_command(
      TARGET vv
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_vv_qt_plugins}" "$<TARGET_FILE_DIR:vv>/plugins"
      COMMENT "Deploy Qt plugins next to vv (from vcpkg installed tree)"
    )
  else()
    set(_vv_windeployqt_hints "")
    if(DEFINED Qt6_DIR)
      list(APPEND _vv_windeployqt_hints "${Qt6_DIR}/../../../tools/Qt6/bin")
      list(APPEND _vv_windeployqt_hints "${Qt6_DIR}/../../../bin")
    endif()
    foreach(_p IN LISTS CMAKE_PREFIX_PATH)
      list(APPEND _vv_windeployqt_hints "${_p}/tools/Qt6/bin" "${_p}/bin")
    endforeach()
    find_program(
      VV_WINDEPLOYQT_EXECUTABLE
      NAMES windeployqt windeployqt.exe
      HINTS ${_vv_windeployqt_hints}
      DOC "Qt deployment tool (ships with Qt)"
    )
    if(VV_WINDEPLOYQT_EXECUTABLE)
      add_custom_command(
        TARGET vv
        POST_BUILD
        COMMAND "${VV_WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:vv>" --no-compiler-runtime
        COMMENT "Qt windeployqt: deploy Qt DLLs and plugins beside vv"
      )
    else()
      message(
        WARNING
        "windeployqt not found; Windows runs may fail to load Qt without manual deployment."
      )
    endif()
  endif()
endif()

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

# ── macOS: bundle + Quick Look generator ─────────────────────────────
if(APPLE)
  set_target_properties(vv PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    MACOSX_BUNDLE_ICON_FILE vv_icon.icns
  )

  # QLGenerator: ObjC plugin that calls `vv --thumbnail` and serves the PNG to Quick Look.
  add_library(vv_ql MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/macos/qlgenerator/GeneratePreviewForURL.m"
  )
  set_target_properties(vv_ql PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "qlgenerator"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/qlgenerator/Info.plist"
    OUTPUT_NAME "vv_ql"
    AUTOMOC OFF
  )
  target_compile_options(vv_ql PRIVATE -fobjc-arc -Wno-deprecated-declarations)
  target_link_libraries(vv_ql PRIVATE
    "-framework CoreFoundation"
    "-framework QuickLook"
    "-framework Foundation"
    "-framework CoreGraphics"
  )

  # Copy resources into vv.app after both targets are built.
  add_dependencies(vv vv_ql)
  add_custom_command(TARGET vv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:vv>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "${GENERATED_ASSETS_DIR}/vv_icon.icns" "$<TARGET_BUNDLE_DIR:vv>/Contents/Resources/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:vv>/Contents/Library/QuickLook"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:vv_ql>" "$<TARGET_BUNDLE_DIR:vv>/Contents/Library/QuickLook/vv.qlgenerator"
    COMMENT "Embedding icon and qlgenerator into app bundle"
  )
endif()
