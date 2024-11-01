if(WEST)
   execute_process(COMMAND ${WEST} list -f "{abspath}" expected
      OUTPUT_VARIABLE TL_CPP_EXPECTED_LIB_DIR
      RESULT_VARIABLE TL_CPP_EXPECTED_LIB_DIR_RESULT
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
   if(NOT "${TL_CPP_EXPECTED_LIB_DIR_RESULT}" STREQUAL "0")
      message(FATAL_ERROR "Expected library not found")
   endif()
   string(STRIP ${TL_CPP_EXPECTED_LIB_DIR} TL_CPP_EXPECTED_LIB_DIR)
   print(TL_CPP_EXPECTED_LIB_DIR)
else()
   message(FATAL_ERROR "Cannot locate the expected library")
endif()

zephyr_include_directories(${TL_CPP_EXPECTED_LIB_DIR}/include/)
