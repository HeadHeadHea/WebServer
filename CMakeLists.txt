cmake_minimum_required(VERSION 3.0)

project(webserver)

include_directories(${PROJECT_SOURCE_DIR}/include)

aux_source_directory(${PROJECT_SOURCE_DIR}/src src)

add_compile_options(-g)

add_executable(main ${src} main.cpp)

target_link_libraries(main pthread mysqlclient)