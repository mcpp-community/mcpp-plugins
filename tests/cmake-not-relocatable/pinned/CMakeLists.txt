# Installs a header and a configuration file that states the absolute prefix,
# as a `configure_file` over `@CMAKE_INSTALL_PREFIX@` does.
cmake_minimum_required(VERSION 3.16)
project(pinned NONE)

file(WRITE "${CMAKE_BINARY_DIR}/pinned-config.cmake" "set(PINNED_PREFIX \"${CMAKE_INSTALL_PREFIX}\")\n")
install(FILES include/pinned.h DESTINATION include)
install(FILES "${CMAKE_BINARY_DIR}/pinned-config.cmake" DESTINATION lib/cmake/pinned)
