cmake_minimum_required(VERSION 3.0)

project(tevclient)

add_library(tevclient STATIC)
target_include_directories(tevclient PUBLIC include)
target_sources(tevclient PRIVATE src/tevclient.cpp)
target_compile_features(tevclient PUBLIC cxx_std_17)

if (MSVC)
    target_link_libraries(tevclient PRIVATE wsock32 ws2_32)
endif()

add_executable(example example/example.cpp)
target_link_libraries(example PRIVATE tevclient)
