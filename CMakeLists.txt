cmake_minimum_required(VERSION 3.14)

project(VtkViewer VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Assets and Icons
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(GENERATED_ASSETS_DIR "${ASSETS_DIR}/generated")

option(VV_FETCH_DEPS "Fetch cxxopts and nlohmann_json with CMake when missing" ON)
option(VV_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(VV_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(VV_QT_WINDOWS_DEPLOY "Run Qt windeployqt after vv links (Windows)" ON)
option(VV_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BUILD_TESTING "Build regression tests" OFF)

if(VV_FETCH_DEPS)
  include(FetchContent)
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
      GIT_TAG v3.3.1
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
      GIT_TAG v3.12.0
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

if(TARGET nlohmann_json AND NOT TARGET nlohmann_json::nlohmann_json)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# MSYS2's vtk package links against system fast_float but its exported
# VTK::fast_float target only requires FastFloat::fast_float without finding
# it, so find it ourselves first.
find_package(fast_float CONFIG QUIET)

find_package(VTK QUIET COMPONENTS
  CommonCore
  CommonDataModel
  CommonColor
  IOGeometry
  IOLegacy
  IOXML
  IOImage
  IOHDF
  FiltersCore
  FiltersExtraction
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

# Compile flags shared by every target built from this tree.
add_library(vv_options INTERFACE)

if(MSVC)
  target_compile_options(vv_options INTERFACE /EHsc)
  target_compile_definitions(vv_options INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

if(VV_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(vv_options INTERFACE /W4)
    if(VV_WARNINGS_AS_ERRORS)
      target_compile_options(vv_options INTERFACE /WX)
    endif()
  else()
    target_compile_options(vv_options INTERFACE
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
      target_compile_options(vv_options INTERFACE -Werror)
    endif()
  endif()
endif()

if(VV_ENABLE_SANITIZERS)
  if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR "VV_ENABLE_SANITIZERS requires GCC or Clang on macOS/Linux")
  endif()
  target_compile_options(vv_options INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(vv_options INTERFACE -fsanitize=address,undefined)
endif()

# Mesh parsing and loading: no Qt, so tests link it without a display or moc.
add_library(vv_mesh STATIC
  src/CartoMeshParser.cpp
  src/FSurfMeshParser.cpp
  src/JsonMeshParser.cpp
  src/LSDynaMeshParser.cpp
  src/MeshLoading.cpp
  src/MeshParser.cpp
  src/STLMeshParser.cpp
  src/TemporalSource.cpp
  src/VTKHDFMeshParser.cpp
  src/VTKMeshParser.cpp
  src/XMLMeshParser.cpp
  src/XdmfMeshParser.cpp
  src/mesh_utils.cpp
  src/include/CartoMeshParser.h
  src/include/FSurfMeshParser.h
  src/include/JsonMeshParser.h
  src/include/LSDynaMeshParser.h
  src/include/MeshLoading.h
  src/include/MeshParser.h
  src/include/STLMeshParser.h
  src/include/TemporalSource.h
  src/include/VTKHDFMeshParser.h
  src/include/VTKMeshParser.h
  src/include/XMLMeshParser.h
  src/include/XdmfMeshParser.h
  src/include/mesh_utils.h
)
target_include_directories(vv_mesh PUBLIC src/include)
target_link_libraries(vv_mesh PRIVATE vv_options)

# Overlay widgets: Qt only, so the widget tests run them without VTK or OpenGL.
add_library(vv_ui STATIC
  src/AnnotationBar.cpp
  src/ColorBarWidget.cpp
  src/ColorPanel.cpp
  src/Colormaps.cpp
  src/CommandPalette.cpp
  src/FieldList.cpp
  src/KeyHints.cpp
  src/LayersPanel.cpp
  src/LucideIcon.cpp
  src/Notice.cpp
  src/OutsidePress.cpp
  src/OverlayStyle.cpp
  src/PanelLabel.cpp
  src/PlaybackBar.cpp
  src/ProbeCard.cpp
  src/ScalarPager.cpp
  src/SearchList.cpp
  src/StylePanel.cpp
  src/ToolRail.cpp
  src/include/AnnotationBar.h
  src/include/ColorBarWidget.h
  src/include/ColorPanel.h
  src/include/Colormaps.h
  src/include/CommandPalette.h
  src/include/FieldList.h
  src/include/KeyHints.h
  src/include/LayersPanel.h
  src/include/LucideIcon.h
  src/include/Notice.h
  src/include/OutsidePress.h
  src/include/OverlayStyle.h
  src/include/PanelLabel.h
  src/include/PlaybackBar.h
  src/include/ProbeCard.h
  src/include/ScalarPager.h
  src/include/SearchList.h
  src/include/StylePanel.h
  src/include/SurfaceStyle.h
  src/include/ToolRail.h
)
set_target_properties(vv_ui PROPERTIES AUTOMOC ON)
target_include_directories(vv_ui PUBLIC src/include)
target_link_libraries(vv_ui PUBLIC Qt${QT_VERSION_MAJOR}::Widgets PRIVATE vv_options)

add_library(vv_viewer STATIC
  src/MeshRenderer.cpp
  src/ScalarVizUtils.cpp
  src/ViewerWindow.cpp
  src/include/FacetPages.h
  src/include/MeshRenderer.h
  src/include/ProbeTrace.h
  src/include/ScalarVizUtils.h
  src/include/ViewerWindow.h
)
set_target_properties(vv_viewer PROPERTIES AUTOMOC ON)
target_link_libraries(vv_viewer PRIVATE vv_options PUBLIC vv_mesh vv_ui)

add_executable(vv src/main_qt.cpp)
set_target_properties(vv PROPERTIES AUTOMOC ON)
target_link_libraries(vv PRIVATE vv_options vv_viewer cxxopts::cxxopts)

if(WIN32)
  set(VV_ICON_RC "${GENERATED_ASSETS_DIR}/vv_icon.rc")
  file(WRITE "${VV_ICON_RC}" "IDI_ICON1 ICON \"${GENERATED_ASSETS_DIR}/vv_icon.ico\"\n")
  target_sources(vv PRIVATE "${VV_ICON_RC}")
endif()

# XDMF heavy data is read with the HDF5 C API. Prefer the copy VTK already links
# (VTK::hdf5), so no new dependency is introduced.
if(TARGET VTK::hdf5)
  set(VV_HDF5_TARGET VTK::hdf5)
else()
  find_package(HDF5 REQUIRED COMPONENTS C)
  set(VV_HDF5_TARGET hdf5::hdf5)
endif()

target_link_libraries(vv_mesh PUBLIC
  ${VV_HDF5_TARGET}
  nlohmann_json::nlohmann_json
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::IOGeometry
  VTK::IOLegacy
  VTK::IOXML
  VTK::IOImage
  VTK::IOHDF
  VTK::IOPLY
  VTK::FiltersCore
  VTK::FiltersExtraction
)
target_link_libraries(vv_viewer PUBLIC
  VTK::CommonColor
  VTK::RenderingCore
  VTK::RenderingOpenGL2
  VTK::InteractionStyle
  VTK::GUISupportQt
  VTK::RenderingAnnotation
  Qt${QT_VERSION_MAJOR}::Widgets
)

# Object factories (OpenGL rendering, HDF readers) register per translation
# unit, so every target that compiles VTK code needs the autoinit definitions.
vtk_module_autoinit(
  TARGETS vv vv_mesh vv_viewer
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
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(NOT CLANG_FORMAT_EXE AND APPLE)
  find_program(XCRUN_EXE NAMES xcrun)
  if(XCRUN_EXE)
    execute_process(
      COMMAND "${XCRUN_EXE}" --find clang-format
      RESULT_VARIABLE _vv_clang_format_result
      OUTPUT_VARIABLE _vv_clang_format_path
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      TIMEOUT 10
    )
    if(_vv_clang_format_result STREQUAL "0" AND EXISTS "${_vv_clang_format_path}")
      set(CLANG_FORMAT_EXE "${_vv_clang_format_path}" CACHE FILEPATH "Path to clang-format" FORCE)
    endif()
  endif()
endif()

if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i ${VV_FORMAT_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting C++ sources with clang-format"
    VERBATIM
  )
  add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror ${VV_FORMAT_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking C++ formatting without changing files"
    VERBATIM
  )
else()
  message(STATUS "clang-format not found; 'format' and 'format-check' targets will not be available.")
endif()

string(TIMESTAMP VV_BUILD_DATE "%Y-%m-%d")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/include/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.h
  @ONLY
)
target_include_directories(vv PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install path prefix." FORCE)
endif()

install(TARGETS vv RUNTIME DESTINATION bin)
