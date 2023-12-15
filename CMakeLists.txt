cmake_minimum_required(VERSION 3.20)
project(depend)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include(ExternalProject)
ExternalProject_Add(libgit_ep
    PREFIX "3rd_party"

    GIT_REPOSITORY "https://github.com/libgit2/libgit2.git"
    GIT_TAG "origin/main"
    GIT_SHALLOW TRUE

    CMAKE_CACHE_ARGS 
        "-DBUILD_SHARED_LIBS:BOOL=OFF"
        "-DBUILD_TESTS:BOOL=OFF"
        "-DCMAKE_INSTALL_PREFIX:FILEPATH=<INSTALL_DIR>"
)
add_library(libgit STATIC IMPORTED)
add_dependencies(libgit libgit_ep)
set_target_properties(libgit PROPERTIES IMPORTED_LOCATION "${PROJECT_BINARY_DIR}/3rd_party/lib/libgit2.a")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/3rd_party/include")
target_include_directories(libgit INTERFACE "${PROJECT_BINARY_DIR}/3rd_party/include")

file(GLOB_RECURSE SRCS "src/*cpp")
add_executable(${PROJECT_NAME} ${SRCS})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_link_libraries(${PROJECT_NAME} libgit OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB pcre)
