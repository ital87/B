cmake_minimum_required(VERSION 3.20)
project(Arc LANGUAGES CXX)

# This CMakeLists.txt exists for the Windows build path only (see install.ps1).
# On Linux/macOS, install.sh compiles src/arc_combined.cpp directly with
# g++ and llvm-config, without CMake.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM Include dir: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM Definitions: ${LLVM_DEFINITIONS}")

add_executable(arc src/arc_combined.cpp)

target_include_directories(arc PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(arc PRIVATE ${LLVM_DEFINITIONS})

if(MSVC)
    target_compile_options(arc PRIVATE /EHsc)
else()
    target_compile_options(arc PRIVATE -fexceptions)
endif()

llvm_map_components_to_libnames(LLVM_LIBS core support irreader codegen target
    x86asmparser x86codegen x86desc x86disassembler x86info asmprinter)

target_link_libraries(arc PRIVATE ${LLVM_LIBS})

install(TARGETS arc RUNTIME DESTINATION bin)

