add_executable(client src/client.cpp)

target_include_directories(client PRIVATE
    ${LIBS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)