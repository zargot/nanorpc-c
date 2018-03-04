cmake_minimum_required(VERSION 3.0)

project(nano)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(NANO SHARED
	rpc.c
)

