find_package(VTK 9.4 REQUIRED COMPONENTS
  CommonCore CommonDataModel CommonColor FiltersCore FiltersSources
  RenderingCore RenderingOpenGL2)

add_library(vv_scene STATIC
  Scene.cpp SoftwareRasterizer.cpp
  ../ScalarVizUtils.cpp)
target_include_directories(vv_scene PUBLIC . ../include)
target_compile_features(vv_scene PUBLIC cxx_std_17)
target_link_libraries(vv_scene PUBLIC ${VTK_LIBRARIES})
if(MSVC)
  target_compile_options(vv_scene PRIVATE /W4)
else()
  target_compile_options(vv_scene PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
vtk_module_autoinit(TARGETS vv_scene MODULES ${VTK_LIBRARIES})

if(EMSCRIPTEN)
  target_compile_options(vv_scene PRIVATE -fwasm-exceptions)
  add_executable(vv_wasm WasmApi.cpp)
  target_link_libraries(vv_wasm PRIVATE vv_scene)
  target_compile_options(vv_wasm PRIVATE -fwasm-exceptions)
  set_target_properties(vv_wasm PROPERTIES OUTPUT_NAME vv SUFFIX .js)
  target_link_options(vv_wasm PRIVATE
    --no-entry -O3 -fwasm-exceptions
    "-sMODULARIZE=1" "-sEXPORT_ES6=1" "-sENVIRONMENT=web"
    "-sALLOW_MEMORY_GROWTH=1" "-sMAXIMUM_MEMORY=1073741824" "-sABORTING_MALLOC=0"
    "-sINITIAL_MEMORY=33554432" "-sFILESYSTEM=0" "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2" "-sASSERTIONS=0"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_vv_create','_vv_destroy','_vv_surface','_vv_scalar','_vv_scalar_buffer','_vv_scalar_commit','_vv_camera_state','_vv_set_camera_state','_vv_lut','_vv_style','_vv_planes','_vv_resize','_vv_render','_vv_pixels','_vv_camera','_vv_reveal','_vv_project','_vv_pick','_vv_annotations','_vv_stats']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPF32','HEAPU32','HEAPF64']")
  vtk_module_autoinit(TARGETS vv_wasm MODULES ${VTK_LIBRARIES})
  install(FILES "$<TARGET_FILE:vv_wasm>" "${CMAKE_CURRENT_BINARY_DIR}/vv.wasm" DESTINATION .)
else()
  add_executable(vv_scene_tests ../../tests/SceneTests.cpp)
  target_link_libraries(vv_scene_tests PRIVATE vv_scene)
  vtk_module_autoinit(TARGETS vv_scene_tests MODULES ${VTK_LIBRARIES})
  add_test(NAME vv_scene_tests COMMAND vv_scene_tests)
endif()
