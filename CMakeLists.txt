# === 配置 CMake === #
cmake_minimum_required(VERSION 3.20)


# === 配置项目 === #
project(Texpose)



# === 配置 C++ === #
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# === 配置通用编译器选项 === #
option(DEBUG_MODE "Build with debug info (for easier debugging)" OFF)
if(MSVC)
	if(DEBUG_MODE)
		# 禁用优化，完整调试
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /O0")
		set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
		set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
	else()
		# 禁用调试，完整优化
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot /Oy /fp:fast")
		set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
		set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
	endif()
	add_compile_options(/utf-8)
endif()


# === 引入第三方库 === #
include(FetchContent)

# nanobench #
FetchContent_Declare(
	nanobench
	GIT_REPOSITORY https://github.com/martinus/nanobench.git
	GIT_TAG v4.3.11
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nanobench)


# === 配置项目文件 === #
add_executable(Texpose
	test/Main.cpp
	test/util/Logger.hpp
	src/texpose/expose/ThreadExposer.hpp
	src/texpose/expose/ThreadExposee.hpp
	src/texpose/expose/ThreadExposer.cpp
	src/texpose/expose/ThreadExposee.cpp
	include/texpose/expose/ThreadExposee.hpp
	include/texpose/expose/ThreadExposer.hpp
	test/texpose/expose/ThreadExpose.cpp
)


# === 链接第三方库 === #
target_link_libraries(Texpose PRIVATE
	nanobench
)


# === 配置 LTO === #
if(NOT DEBUG_MODE)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
	if(IPO_SUPPORTED)
		set_target_properties(Texpose PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		message(WARNING "IPO/LTO not supported: ${IPO_ERROR}")
	endif()
endif()


# === 定义编译器宏 === #
target_compile_definitions(Texpose PRIVATE
	_CRT_SECURE_NO_WARNINGS
)


# === 定义项目路径宏 === #
target_compile_definitions(Texpose PRIVATE
	PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)