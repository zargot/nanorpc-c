cmake_minimum_required(VERSION 3.0)

project(nano)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions("-std=gnu11")

add_library(nano_rpc SHARED
	rpc.c
)

add_executable(nano_rpc_test
	rpc.c
)
target_link_libraries(nano_rpc_test
	json-c
	curl
	m
)

