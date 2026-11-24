# A CMake subproject standing in for a vendored library: one library, shared
# when the consumer asks for it with BUILD_SHARED_LIBS (static otherwise, as
# CMake builds it by default), installed with the layout `install()` gives it.
cmake_minimum_required(VERSION 3.16)
project(greet LANGUAGES C)

add_library(greet greet.c)
target_include_directories(greet PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
set_target_properties(greet PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

install(TARGETS greet RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES include/greet.h DESTINATION include)
