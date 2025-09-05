cmake_minimum_required(VERSION 3.10)

project(VtkViewer VERSION 1.0.0)

find_package(fmt CONFIG REQUIRED)
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  CommonColor
  IOLegacy
  IOXML
  FiltersCore
  RenderingCore
  RenderingOpenGL2
  InteractionStyle
  IOPLY
  RenderingAnnotation
)

file(GLOB SOURCES "src/*.cpp")
add_executable(vv ${SOURCES})
target_include_directories(vv PRIVATE src/include)

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
  VTK::RenderingAnnotation
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

add_custom_command(TARGET vv POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "To use 'vv', add $ENV{HOME}/.local/bin to your PATH."
)