cmake_minimum_required(VERSION 3.10)

project(MutiPeopleChat)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(MutiPeopleChat
    src/main.cpp
    src/easylogging++.cc
    src/server.cpp
    src/threadpool.cpp
    src/user.cpp
)