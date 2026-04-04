cmake_minimum_required(VERSION 3.15)

project(riverFinderLibJ LANGUAGES C CXX)

# ========= 基本设置 =========

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ========= 查找 JNI =========

find_package(JNI REQUIRED)

# ========= 源文件 =========

set(SOURCES
        sunnyslopes_riverfinder_RiverFinderBridge.cpp
        ../cubiomes/finders.c
        ../cubiomes/generator.c
        ../cubiomes/layers.c
        ../cubiomes/biomenoise.c
        ../cubiomes/biomes.c
        ../cubiomes/noise.c
        ../cubiomes/util.c
        ../cubiomes/quadbase.c
)

# ========= 构建 JNI 动态库 =========

add_library(riverFinderLibJ SHARED ${SOURCES})

target_include_directories(riverFinderLibJ PRIVATE
        ${JNI_INCLUDE_DIRS}
        ../cubiomes
)

# ========= 编译优化 =========

if(MSVC)
    target_compile_options(riverFinderLibJ PRIVATE
            /O2
            /DNDEBUG
    )
else()
    target_compile_options(riverFinderLibJ PRIVATE
            -O3
            -DNDEBUG
            -ffast-math
            -march=native
    )
endif()

# ========= 静态链接 MinGW runtime =========

if(MINGW)
    target_link_options(riverFinderLibJ PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()

# ========= 输出目录 =========

set_target_properties(riverFinderLibJ PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/native
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/native
)

# ========= 自动 strip（减小体积） =========

if(NOT MSVC)
    add_custom_command(TARGET riverFinderLibJ POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:riverFinderLibJ>
    )
endif()