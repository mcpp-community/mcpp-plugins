# A static library whose one function says which C library compiled it.
cmake_minimum_required(VERSION 3.16)
project(answer LANGUAGES C)

add_library(answer STATIC answer.c)
target_include_directories(answer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

install(TARGETS answer ARCHIVE DESTINATION lib)
install(FILES include/answer.h DESTINATION include)

# This executable belongs to the subproject. Its link must use this build's
# archive, without depending on the consumer's future installation prefix.
add_executable(answer-sidecar sidecar.c)
target_link_libraries(answer-sidecar PRIVATE answer)
install(TARGETS answer-sidecar RUNTIME DESTINATION bin)
