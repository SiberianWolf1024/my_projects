

cmake_minimum_required(VERSION 3.13)


include_directories(ini_parser file_check network)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
project(net_port_playback LANGUAGES C)

message("CMAKE_C_FLAGS is ${CMAKE_C_FLAGS}")


file(GLOB SOURCES_FILE
    ${CMAKE_SOURCE_DIR}/net_port_data_transfer.c
    ${CMAKE_SOURCE_DIR}/ini_parser/ini_parser.c
    ${CMAKE_SOURCE_DIR}/network/eth_server.c
	)


# 将变量添加到可执行文件的链接库列表中
add_executable(net_port_playback ${SOURCES_FILE})

target_link_libraries(${PROJECT_NAME}  PRIVATE "-static-libgcc -static-libstdc++ -static -lwinmm -lpthread -lws2_32 -liphlpapi")


# cmake -G "MinGW Makefiles" ../
# 进入build目录 cmake --build .