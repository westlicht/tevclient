cmake_minimum_required(VERSION 3.0)

project(tevclient)

if(CMAKE_VERSION VERSION_LESS 3.21)
    get_property(not_top DIRECTORY PROPERTY PARENT_DIRECTORY)
    if(NOT not_top)
        set(PROJECT_IS_TOP_LEVEL true)
    endif()
endif()

add_library(tevclient STATIC)
target_include_directories(tevclient PUBLIC include)
target_sources(tevclient PRIVATE src/tevclient.cpp)
target_compile_features(tevclient PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(tevclient PRIVATE wsock32 ws2_32)
endif()

if(PROJECT_IS_TOP_LEVEL)
    add_executable(example example/example.cpp)
    target_link_libraries(example PRIVATE tevclient)
    if(WIN32)
        target_compile_definitions(example PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()
