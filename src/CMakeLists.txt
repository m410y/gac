cmake_minimum_required(VERSION 3.20)

project(gac LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONIG)

message(STATUS "Found LLVM ${LLVM_VERSION}")
message(STATUS "Using LLVMConfig.cmake from ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS} ../include/)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_compile_options(-g -Wall -Wextra)
# add_executable(gac main.cpp parser.c parser.cpp ast.cpp algebra.cpp codegen.cpp)
add_executable(gac ../test/algebra.cpp)

llvm_map_components_to_libnames(llvm_libs -${LLVM_VERSION_MAJOR})
target_link_libraries(gac PRIVATE ${llvm_libs} -ltree-sitter)
