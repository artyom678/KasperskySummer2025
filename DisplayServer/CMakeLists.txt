add_executable(display_server src/display_server.cpp)

target_include_directories(display_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBS_DIR}
)