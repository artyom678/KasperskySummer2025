add_executable(processing_server src/proc_server.cpp)

target_include_directories(processing_server PRIVATE
    ${LIBS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(processing_server PRIVATE pthread)