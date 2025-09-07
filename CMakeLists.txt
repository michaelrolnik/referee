#
#   MIT License
#
#   Copyright (c) 2022 Michael Rolnik
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to deal
#   in the Software without restriction, including without limitation the rights
#   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#   copies of the Software, and to permit persons to whom the Software is
#   furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in all
#   copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#   SOFTWARE.
#

cmake_minimum_required(VERSION 3.16)

project(referee)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/submodules/cmake-scripts)
set(ANTLR4TAG 4.13.1)

include(code-coverage)
include(formatting)
include(FetchContent)
include(ExternalProject)
include(add-antlr4)

add_compile_options(-Wno-multichar)
add_compile_options(-g -ggdb)
add_compile_options(-std=c++20)
add_compile_options(-O0)
# TODO: remove this once we have fixed all the deprecation warnings
add_compile_options(-Wno-deprecated-declarations)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(LLVM_DIR            /opt/homebrew/opt/llvm/lib/cmake/llvm )
endif()

enable_testing()

find_package(LLVM             REQUIRED CONFIG)
find_package(FMT              REQUIRED)

add_code_coverage()

# Setup ANTLR4 first
add_antlr4_jar(${ANTLR4TAG})
add_antlr4_cpp(${ANTLR4TAG} SHARED WITH_LIBCXX DISABLE_WARNINGS)

# Then use ANTLR4 to generate parser
add_antlr4_target(
    referee_parser
    INPUT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/core/referee.g4
    NAMESPACE referee
    GEN_VISITOR
)


if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include_directories(
        /opt/homebrew/Cellar/googletest/1.11.0/include
        /opt/homebrew/Cellar/antlr4-cpp-runtime/4.10.1/include/
    )

    link_directories(
        /opt/homebrew/Cellar/googletest/1.11.0/lib
        /opt/homebrew/Cellar/antlr4-cpp-runtime/4.10.1/lib
    )
endif()

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(llvm_libs support core irreader orcjit aarch64asmparser aarch64codegen aarch64info x86asmparser x86codegen x86desc x86disassembler x86info)

file(GLOB_RECURSE ALL_CODE_FILES
    ${PROJECT_SOURCE_DIR}/core/*.[ch]pp
    ${PROJECT_SOURCE_DIR}/core/*.[ch]
    ${PROJECT_SOURCE_DIR}/rdb/*.[h]pp
    ${PROJECT_SOURCE_DIR}/rdb/*.[h]
)

clang_format(format ${ALL_CODE_FILES})


add_library(
    core
    core/visitors/compile.cpp
    core/visitors/canonic.cpp
    core/visitors/negated.cpp
    core/visitors/typecalc.cpp
    core/visitors/printer.cpp
    core/visitors/rewrite.cpp
    core/visitors/csvHeaders.cpp
    core/antlr2ast.cpp
    core/syntax.cpp
    core/strings.cpp
    core/module.cpp
    core/utils.cpp
    rdb/database.cpp
    referee.cpp
    ${antlr4_referee_parser_SOURCES}
)

target_include_directories(core PRIVATE
    ${ANTLR4CPP_INCLUDE_DIR}
    ${antlr4_referee_parser_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_BINARY_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/submodules/rapidcsv/src
    ${PROJECT_SOURCE_DIR}/submodules/spdlog/include
)

# Main executable
add_executable(
    referee
    main.cpp
)

target_include_directories(referee PRIVATE
    ${PROJECT_SOURCE_DIR}/submodules/spdlog/include
)

add_dependencies(
    referee
    antlr4::antlr4_shared
)

target_link_libraries(
    referee PUBLIC
    fmt::fmt
    core
    antlr4::antlr4_shared
    ${llvm_libs}
)
target_code_coverage(referee)

# Tests executable
add_executable(
    tests
    test/main.cpp
    test/strings.cpp
    test/canonic.cpp
    test/logic.cpp
)

target_include_directories(tests PRIVATE
    ${ANTLR4CPP_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core
    ${antlr4_referee_parser_INCLUDE_DIR}
    ${LLVM_INCLUDE_DIRS}
)

target_link_libraries(
    tests PUBLIC
    fmt::fmt
    core
    gtest
    gtest_main
    pthread
    antlr4::antlr4_shared
    ${llvm_libs}
)
target_code_coverage(tests)

add_test(tests tests)

# RDB executable
add_executable(
    rdb
    rdb/database.cpp
    rdb/main.cpp
)

target_include_directories(rdb PRIVATE
    ${PROJECT_SOURCE_DIR}/submodules/rapidcsv/src
    ${PROJECT_SOURCE_DIR}/submodules/spdlog/include
    ${PROJECT_SOURCE_DIR}/core
)

target_link_libraries(
    rdb
    fmt::fmt
)
