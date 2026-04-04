cmake_minimum_required(VERSION 3.9)
project(riverFinder)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/Od /Zi /DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    # 类似处理 C++ 标志
else()
	set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
	set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

if(WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_WIN32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32")
endif()
set(CMAKE_CXX_STANDARD 20)

set(HEADERS
		cubiomes/finders.h
		cubiomes/generator.h
		cubiomes/layers.h
		cubiomes/biomenoise.h
		cubiomes/biomes.h
		cubiomes/noise.h
		cubiomes/rng.h
		cubiomes/util.h
		cubiomes/quadbase.h
		Thread.h
)
set(SOURCES
		cubiomes/finders.c
		cubiomes/generator.c
		cubiomes/layers.c
		cubiomes/biomenoise.c
		cubiomes/biomes.c
		cubiomes/noise.c
		cubiomes/util.c
		cubiomes/quadbase.c
		river_finder.cpp
)

add_executable(riverFinder ${SOURCES})
