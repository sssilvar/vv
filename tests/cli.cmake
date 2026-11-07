foreach(range IN ITEMS "" "garbage" "0,1junk" "0,1,2" "nan,1" "0,inf" "1,1" "2,1" "1e999,2")
  execute_process(COMMAND "${VV}" "--range=${range}" -
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 3)
  if(NOT result STREQUAL "1" OR NOT error MATCHES "--range expects finite min,max")
    message(FATAL_ERROR "Range '${range}' was not rejected before reading stdin: ${result}: ${error}")
  endif()
endforeach()

foreach(range IN ITEMS "-80,40" "0,1" "-1e2,2.5e1" " 0,1 ")
  execute_process(COMMAND "${VV}" "--range=${range}" "${CMAKE_CURRENT_BINARY_DIR}/missing-mesh.vtk"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 3)
  if(NOT result STREQUAL "1" OR NOT error MATCHES "File does not exist")
    message(FATAL_ERROR "Valid range '${range}' did not reach file validation: ${result}: ${error}")
  endif()
endforeach()

foreach(option IN ITEMS --help --version)
  execute_process(COMMAND "${VV}" "${option}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 3)
  if(NOT result STREQUAL "0" OR output STREQUAL "")
    message(FATAL_ERROR "${option} failed: ${result}: ${error}")
  endif()
endforeach()

foreach(option IN ITEMS --unknown-option --range)
  execute_process(COMMAND "${VV}" "${option}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 3)
  if(NOT result STREQUAL "1" OR error STREQUAL "")
    message(FATAL_ERROR "${option} did not report a usage error: ${result}: ${error}")
  endif()
endforeach()
