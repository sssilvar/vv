add_executable(vv_renderer_tests MeshRendererTests.cpp
  ../src/MeshRenderer.cpp ../src/ScalarVizUtils.cpp ../src/Colormaps.cpp)
target_link_libraries(vv_renderer_tests PRIVATE vv_options vv_mesh
  VTK::CommonColor VTK::FiltersSources VTK::RenderingOpenGL2 VTK::InteractionStyle VTK::RenderingAnnotation)
vtk_module_autoinit(TARGETS vv_renderer_tests MODULES ${VTK_LIBRARIES})
add_test(NAME panel-layout COMMAND vv_renderer_tests)
set_tests_properties(panel-layout PROPERTIES TIMEOUT 30)

# Qt 5's offscreen backend leaks screen objects at shutdown; allow xcb under Xvfb.
set(VV_TEST_QPA_PLATFORM "offscreen" CACHE STRING "Qt platform plugin for widget and viewer tests")
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
add_executable(vv_ui_tests UiTests.cpp)
set_target_properties(vv_ui_tests PROPERTIES AUTOMOC ON)
target_link_libraries(vv_ui_tests PRIVATE vv_options vv_ui Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME ui COMMAND vv_ui_tests)
set_tests_properties(ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=${VV_TEST_QPA_PLATFORM}")

add_executable(vv_mesh_tests MeshLoadingTests.cpp)
target_link_libraries(vv_mesh_tests PRIVATE vv_options vv_mesh)
vtk_module_autoinit(TARGETS vv_mesh_tests MODULES ${VTK_LIBRARIES})

add_test(NAME mesh-metadata COMMAND vv_mesh_tests "${CMAKE_CURRENT_BINARY_DIR}/mesh-colors.vtk")
add_test(NAME cli
  COMMAND "${CMAKE_COMMAND}" "-DVV=$<TARGET_FILE:vv>" -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake"
)
set_tests_properties(mesh-metadata cli PROPERTIES TIMEOUT 30)

if(UNIX)
  add_executable(vv_stdin_tests StdinTests.cpp)
  target_link_libraries(vv_stdin_tests PRIVATE vv_options vv_mesh)

  add_test(NAME stdin-errors COMMAND vv_stdin_tests "${CMAKE_CURRENT_BINARY_DIR}/stdin-test")
  add_test(NAME build-helper
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/build-helper.sh" "${PROJECT_SOURCE_DIR}"
  )
  set_tests_properties(stdin-errors build-helper PROPERTIES TIMEOUT 30)
endif()

add_executable(vv_viewer_tests ViewerTests.cpp)
set_target_properties(vv_viewer_tests PROPERTIES AUTOMOC ON)
target_link_libraries(vv_viewer_tests PRIVATE vv_options vv_viewer Qt${QT_VERSION_MAJOR}::Test)
vtk_module_autoinit(TARGETS vv_viewer_tests MODULES ${VTK_LIBRARIES})
add_test(NAME viewer COMMAND vv_viewer_tests)
set_tests_properties(viewer PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=${VV_TEST_QPA_PLATFORM}")
if(VV_ENABLE_SANITIZERS)
  # Full allocation backtraces make this integration run substantially slower.
  set_tests_properties(viewer PROPERTIES TIMEOUT 120)
endif()

add_executable(vv_render_benchmark RenderBenchmark.cpp)
target_link_libraries(vv_render_benchmark PRIVATE vv_options vv_viewer VTK::FiltersSources)
vtk_module_autoinit(TARGETS vv_render_benchmark MODULES ${VTK_LIBRARIES})
