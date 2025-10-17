include(GNUInstallDirs)         # WTF?
#----------------------------------------------------------------------

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

cmake_minimum_required(VERSION 3.13.4)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)" FORCE)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

#----------------------------------------------------------------------
project(Voidc)
#----------------------------------------------------------------------

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)


include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})


#----------------------------------------------------------------------
find_package(Immer REQUIRED CONFIG)


#----------------------------------------------------------------------
add_executable(voidc
    compiler/stage0/voidc_ast.cpp
    compiler/stage0/voidc_types.cpp
    compiler/stage0/voidc_target.cpp
    compiler/stage0/voidc_util.cpp
    compiler/stage0/voidc_main.cpp
    compiler/stage0/voidc_quark.cpp
    compiler/stage0/voidc_visitor.cpp
    compiler/stage0/voidc_compiler.cpp
    compiler/stage0/vpeg_parser.cpp
    compiler/stage0/vpeg_grammar.cpp
    compiler/stage0/vpeg_context.cpp
    compiler/stage0/vpeg_voidc.cpp
    compiler/stage0/voidc_stdio.cpp
)


#----------------------------------------------------------------------
if (UNIX)

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL DEBUG)

       set(MYFLAGS -fsanitize=address -fno-omit-frame-pointer)

    endif()

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL RELEASE)

       set(MYFLAGS -O3 -DNDEBUG)

    endif()

    target_compile_options(voidc PRIVATE ${MYFLAGS})

    target_link_options(voidc PRIVATE -rdynamic ${MYFLAGS})

endif (UNIX)


if (MINGW)

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL DEBUG)

       set(MYFLAGS )

    endif()

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL RELEASE)

       set(MYFLAGS -O3 -DNDEBUG)

    endif()

    target_compile_options(voidc PRIVATE ${MYFLAGS} -fcxx-exceptions -frtti)

endif (MINGW)


# Link against LLVM library(!)
target_link_libraries(voidc LLVM)



