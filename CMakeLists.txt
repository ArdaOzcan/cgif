cmake_minimum_required(VERSION 3.10)

project(GifBuf C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

add_compile_options(-Werror -g)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "Disable building examples")

FetchContent_Declare(
    ccore
    GIT_REPOSITORY https://github.com/ArdaOzcan/ccore.git
    GIT_TAG main
)
FetchContent_MakeAvailable(ccore)


FetchContent_Declare(
    munit
    GIT_REPOSITORY https://github.com/nemequ/munit.git
    GIT_TAG        v0.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(munit)

set(SRC_DIR ${CMAKE_SOURCE_DIR})

set(LIB_FILES
    ${SRC_DIR}/src/gifbuf.c
)

set(MAIN_FILE
    ${SRC_DIR}/examples/main.c
)

set(TEST_FILES
    ${SRC_DIR}/test/test.c
    ${munit_SOURCE_DIR}/munit.c
)

add_library(gifbuf ${LIB_FILES})

target_include_directories(gifbuf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(gifbuf PUBLIC ccore)

find_library(raylib raylib)
add_executable(gifbuf_example ${MAIN_FILE})
target_include_directories(gifbuf_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gifbuf_example PRIVATE gifbuf ccore raylib)

add_executable(test ${TEST_FILES})
target_include_directories(test PRIVATE ${munit_SOURCE_DIR})
target_link_libraries(test PRIVATE gifbuf ccore)
