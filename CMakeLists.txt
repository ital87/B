cmake_minimum_required(VERSION 3.20)
project(B LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM CONFIG REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")

add_executable(b src/b_combined.cpp)

target_include_directories(b PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(b PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(b PRIVATE -fexceptions)

if(LLVM_LINK_LLVM_DYLIB OR TARGET LLVM)
    message(STATUS "Linking against the LLVM shared library")
    set(B_LLVM_LIBS LLVM)
else()
    message(STATUS "Linking against individual LLVM component libraries")
    llvm_map_components_to_libnames(B_LLVM_LIBS
        core support irreader asmparser linker passes
        analysis instcombine scalaropts ipo transformutils
        codegen target
        x86asmparser x86codegen x86desc x86disassembler x86info asmprinter)
endif()

target_link_libraries(b PRIVATE ${B_LLVM_LIBS})

install(TARGETS b RUNTIME DESTINATION bin)
install(DIRECTORY std/ DESTINATION share/b/std FILES_MATCHING PATTERN "*.b")
