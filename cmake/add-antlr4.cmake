include(CMakePrintHelpers)
include(FetchContent)

# 0. find java runtime (java executable)
find_package(Java REQUIRED Runtime)

# 1. download antlr4 command line tools
# URL: https://www.antlr.org/download/antlr-4.12.0-complete.jar
function(add_antlr4_jar ANTLR4_VERSION)
  set(ANTLR4_JAR_URL "https://www.antlr.org/download/antlr-${ANTLR4_VERSION}-complete.jar")
  set(ANTLR4_JAR_PATH "${CMAKE_CURRENT_BINARY_DIR}/antlr-${ANTLR4_VERSION}-complete.jar")
  if(EXISTS ${ANTLR4_JAR_PATH})
    message(STATUS "${ANTLR4_JAR_PATH} already exists, skip downloading")
  else()
    message(STATUS "Download Antlr4 Command Line Tools")
    message(STATUS "Downloading from ${ANTLR4_JAR_URL}")
    file(DOWNLOAD 
      ${ANTLR4_JAR_URL} ${ANTLR4_JAR_PATH}
      TIMEOUT 30
    )
  endif()

  # we have to perform sanity check here
  execute_process(
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR4_JAR_PATH}
    ERROR_VARIABLE download_error
    OUTPUT_QUIET
  )

  if(NOT download_error STREQUAL "")
    file(REMOVE ${ANTLR4_JAR_PATH})
    message(FATAL_ERROR "${ANTLR4_JAR_PATH} is corrupted, please re-run configure to download or manually put jar file to the path")
  endif()

  # return value
  # cmake_print_variables(ANTLR4_JAR_PATH)
  set(ANTLR4_JAR_PATH ${ANTLR4_JAR_PATH} PARENT_SCOPE)
endfunction(add_antlr4_jar)

# 2. download antlr4 cpp runtime
macro(add_antlr4_cpp ANTLR4_TAG)
  set(OPTIONS SHARED WITH_STATIC_CRT WITH_LIBCXX DISABLE_WARNINGS)
  set(ONE_VALUE_ARGS   "")
  set(MULTI_VALUE_ARGS "")
  cmake_parse_arguments(
    ANTLR4CPP
    "${OPTIONS}"
    "${ONE_VALUE_ARGS}"
    "${MULTI_VALUE_ARGS}"
    ${ARGN}
  )

  FetchContent_Declare( 
    antlr4_cpp
    GIT_REPOSITORY https://github.com/antlr/antlr4
    GIT_TAG        ${TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
  )
  FetchContent_Populate(antlr4_cpp)

  set(ANTLR4CPP_SOURCE_DIR ${antlr4_cpp_SOURCE_DIR}/runtime/Cpp CACHE INTERNAL "")

  # read version from VERSION file
  file(STRINGS ${ANTLR4CPP_SOURCE_DIR}/VERSION ANTLR_VERSION)

  set(WITH_DEMO             FALSE                         CACHE INTERNAL "")
  set(WITH_STATIC_CRT       ${ANTLR4CPP_WITH_STATIC_CRT}  CACHE INTERNAL "")
  set(WITH_LIBCXX           ${ANTLR4CPP_WITH_LIBCXX}      CACHE INTERNAL "")
  set(DISABLE_WARNINGS      ${ANTLR4CPP_DISABLE_WARNINGS} CACHE INTERNAL "")
  set(ANTLR4_INSTALL        FALSE                         CACHE INTERNAL "")
  set(ANTLR_BUILD_CPP_TESTS FALSE                         CACHE INTERNAL "")
  set(ANTLR4CPP_INCLUDE_DIR 
    ${ANTLR4CPP_SOURCE_DIR}/runtime/src CACHE INTERNAL "")
  set(ANTLR4CPP_OUTPUT_DIR 
    ${antlr4_cpp_BINARY_DIR}/runtime    CACHE INTERNAL "")

  if(ANTLR4CPP_SHARED)
    set(ANTLR_BUILD_SHARED TRUE  CACHE  INTERNAL "")
    set(ANTLR_BUILD_STATIC FALSE CACHE  INTERNAL "")
  else()
    set(ANTLR_BUILD_SHARED FALSE CACHE  INTERNAL "")
    set(ANTLR_BUILD_STATIC TRUE  CACHE  INTERNAL "")
  endif()

  # from https://github.com/antlr/antlr4/blob/dev/runtime/Cpp/cmake/ExternalAntlr4Cpp.cmake
  if(MSVC)
    set(ANTLR4CPP_STATIC_LIBRARIES
      ${ANTLR4CPP_OUTPUT_DIR}/antlr4-runtime-static.lib CACHE INTERNAL "")
    set(ANTLR4CPP_SHARED_LIBRARIES
      ${ANTLR4CPP_OUTPUT_DIR}/antlr4-runtime.lib        CACHE INTERNAL "")
    set(ANTLR4CPP_RUNTIME_LIBRARIES
      ${ANTLR4CPP_OUTPUT_DIR}/antlr4-runtime.dll        CACHE INTERNAL "")
  else()
    set(ANTLR4CPP_STATIC_LIBRARIES
      ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.a       CACHE INTERNAL "")
    if(MINGW)
      set(ANTLR4CPP_SHARED_LIBRARIES
        ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.dll.a CACHE INTERNAL "")
      set(ANTLR4CPP_RUNTIME_LIBRARIES
        ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.dll   CACHE INTERNAL "")
    elseif(CYGWIN)
      set(ANTLR4CPP_SHARED_LIBRARIES
        ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.dll.a CACHE INTERNAL "")
      set(ANTLR4CPP_RUNTIME_LIBRARIES
        # https://github.com/antlr/antlr4/pull/2235#discussion_r173871830
        # due to cmake: https://cmake.org/cmake/help/latest/prop_tgt/SOVERSION.html
        ${ANTLR4CPP_OUTPUT_DIR}/cygantlr4-runtime-${ANTLR_VERSION}.dll CACHE INTERNAL "")
    elseif(APPLE)
      set(ANTLR4CPP_RUNTIME_LIBRARIES
        ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.dylib CACHE INTERNAL "")
    else()
      set(ANTLR4CPP_RUNTIME_LIBRARIES
        ${ANTLR4CPP_OUTPUT_DIR}/libantlr4-runtime.so    CACHE INTERNAL "")
    endif()
  endif()

  set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard used" FORCE)
  set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "" FORCE)

  add_subdirectory(
    ${ANTLR4CPP_SOURCE_DIR} 
    ${antlr4_cpp_BINARY_DIR}
  )

  if(ANTLR4CPP_SHARED)
    add_library(antlr4_runtime_shared SHARED IMPORTED)
    target_include_directories(antlr4_runtime_shared
      INTERFACE ${ANTLR4CPP_INCLUDE_DIR}
    )
    set_property(TARGET antlr4_runtime_shared PROPERTY IMPORTED_LOCATION ${ANTLR4CPP_RUNTIME_LIBRARIES})
    set_property(TARGET antlr4_runtime_shared PROPERTY IMPORTED_IMPLIB   ${ANTLR4CPP_SHARED_LIBRARIES})
    add_library(antlr4::antlr4_shared ALIAS antlr4_runtime_shared)
  else()
    target_include_directories(antlr4_static 
      INTERFACE ${ANTLR4CPP_INCLUDE_DIR})
    add_library(antlr4::antlr4_static ALIAS antlr4_static)
  endif()

  # print summary info
  # cmake_print_variables(ANTLR_VERSION)
  # cmake_print_variables(ANTLR4CPP_SOURCE_DIR)
  # cmake_print_variables(ANTLR4CPP_INCLUDE_DIR)
  # cmake_print_variables(ANTLR4CPP_SHARED)
  # cmake_print_variables(ANTLR4CPP_WITH_STATIC_CRT)
  # cmake_print_variables(ANTLR4CPP_WITH_LIBCXX)
  # cmake_print_variables(ANTLR4CPP_DISABLE_WARNINGS)

  if(ANTLR4CPP_SHARED)
    # cmake_print_variables(ANTLR4CPP_SHARED_LIBRARIES)
    # cmake_print_variables(ANTLR4CPP_RUNTIME_LIBRARIES)
  else()
    # cmake_print_variables(ANTLR4CPP_STATIC_LIBRARIES)
    # cmake_print_variables(ANTLR4CPP_RUNTIME_LIBRARIES)
  endif()
endmacro(add_antlr4_cpp)

# 3. generate antlr4 parser
function(add_antlr4_target ANTLR4_TARGET_NAME)
  set(OPTIONS          GEN_LISTENER GEN_VISITOR)
  set(ONE_VALUE_ARGS   NAMESPACE OUTPUT_DIRECTORY)
  set(MULTI_VALUE_ARGS GENERATE_OPTIONS INPUT_FILES)
  cmake_parse_arguments(
    ANTLR4_TARGET
    "${OPTIONS}"
    "${ONE_VALUE_ARGS}"
    "${MULTI_VALUE_ARGS}"
    ${ARGN}
  )

  # cmake_print_variables(ANTLR4_TARGET_NAME)
  # cmake_print_variables(ANTLR4_TARGET_INPUT_FILES)
  # cmake_print_variables(ANTLR4_TARGET_GEN_LISTENER)
  # cmake_print_variables(ANTLR4_TARGET_GEN_VISITOR)
  # cmake_print_variables(ANTLR4_TARGET_GEN_LISTENER)
  # cmake_print_variables(ANTLR4_TARGET_NAMESPACE)
  # cmake_print_variables(ANTLR4_TARGET_OUTPUT_DIRECTORY)
  # cmake_print_variables(ANTLR4_TARGET_GENERATE_OPTIONS)

  if(ANTLR4_TARGET_OUTPUT_DIRECTORY)
    cmake_path(IS_RELATIVE ANTLR4_TARGET_OUTPUT_DIRECTORY is_relative_result)
    if(is_relative_result)
      set(ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${ANTLR4_TARGET_OUTPUT_DIRECTORY})
    else()
      set(ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR ${ANTLR4_TARGET_OUTPUT_DIRECTORY})
    endif()
  else()
    set(ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/antlr4_src/${ANTLR4_TARGET_NAME})
  endif()

  # cmake_print_variables(ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR)

  if(ANTLR4_TARGET_NAMESPACE)
    list(APPEND ANTLR4_TARGET_GENERATE_OPTIONS -package  ${ANTLR4_TARGET_NAMESPACE})
  endif()

  if(ANTLR4_TARGET_GEN_LISTENER)
    list(APPEND ANTLR4_TARGET_GENERATE_OPTIONS -listener)
  else()
    list(APPEND ANTLR4_TARGET_GENERATE_OPTIONS -no-listener)
  endif()

  if(ANTLR4_TARGET_GEN_VISITOR)
    list(APPEND ANTLR4_TARGET_GENERATE_OPTIONS -visitor)
  endif()

  # cmake_print_variables(ANTLR4_TARGET_GENERATE_OPTIONS)

  # cmake_print_variables(ANTLR4_TARGET_INPUT_FILES)
  set(input_files)
  foreach(f ${ANTLR4_TARGET_INPUT_FILES})
    cmake_path(IS_RELATIVE f is_relative_result)
    if(is_relative_result)
      list(APPEND input_files ${CMAKE_CURRENT_SOURCE_DIR}/${f})
    else()
      list(APPEND input_files ${f})
    endif()
  endforeach()
  set(ANTLR4_TARGET_INPUT_FILES ${input_files})
  unset(input_files)
  # cmake_print_variables(ANTLR4_TARGET_INPUT_FILES)

  if(NOT ANTLR4_JAR_PATH)
    message(FATAL_ERROR "ANTLR4_JAR_PATH not set")
  endif()
  
  execute_process(
      COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR4_JAR_PATH}
              ${ANTLR4_TARGET_INPUT_FILES}
              -o ${ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR}
              -no-listener
              -Dlanguage=Cpp
              ${ANTLR4_TARGET_GENERATE_OPTIONS}
  )

  file(GLOB_RECURSE generate_sources "${ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR}/*.cpp")
  # file(GLOB_RECURSE generate_headers "${ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR}/*.h")
  # file(GLOB_RECURSE generate_files "${ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR}/*.*")

  set(antlr4_${ANTLR4_TARGET_NAME}_INCLUDE_DIR ${ANTLR_${ANTLR4_TARGET_NAME}_OUTPUT_DIR} PARENT_SCOPE)
  set(antlr4_${ANTLR4_TARGET_NAME}_SOURCES     ${generate_sources}                       PARENT_SCOPE)
endfunction(add_antlr4_target)