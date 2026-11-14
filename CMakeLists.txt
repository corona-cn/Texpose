# === 配置 CMake === #
cmake_minimum_required(VERSION 3.21)


# === 配置项目 === #
# 项目定义 #
project(Texpose LANGUAGES C CXX)

# 项目类型 #
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(PROJECT_IS_TOP_LEVEL ON)
else()
	set(PROJECT_IS_TOP_LEVEL OFF)
endif()

# MSVC 编码 #
if(MSVC)
	add_compile_options(/utf-8)
endif()


# === 配置语言标准 === #
# C #
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# C++ #
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# === 配置构建目标 === #
# 项目目标 #
add_library(Texpose STATIC
	src/texpose/expose/ThreadExposer.hpp
	src/texpose/expose/ThreadExposee.hpp
	src/texpose/expose/ThreadExposer.cpp
	src/texpose/expose/ThreadExposee.cpp
	include/texpose/expose/ThreadExposee.hpp
	include/texpose/expose/ThreadExposer.hpp
)
target_include_directories(Texpose PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(Texpose PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

# 项目测试目标 #
if(PROJECT_IS_TOP_LEVEL)
	add_executable(TexposeTest
		test/Main.cpp
		test/util/Logger.hpp
		test/texpose/expose/ThreadExpose.cpp
	)
endif()


# === 配置编译选项 === #
# MSVC #
if(MSVC)
	# 通用配置
	target_compile_options(Texpose PRIVATE $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /Oy /fp:fast>)

	# 顶层配置
	if(PROJECT_IS_TOP_LEVEL)
		target_compile_options(TexposeTest PRIVATE $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /Oy /fp:fast>)
		target_link_options(TexposeTest PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
	endif()
endif()

# LTO #
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
	# 通用配置
	set_target_properties(Texpose PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)

	# 顶层配置
	if(PROJECT_IS_TOP_LEVEL)
		set_target_properties(TexposeTest PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	endif()
else()
	message(WARNING "IPO/LTO not supported: ${IPO_ERROR}")
endif()


# === 配置第三方库 === #
if(PROJECT_IS_TOP_LEVEL)
	include(FetchContent)

	# nanobench #
	FetchContent_Declare(
		nanobench
		GIT_REPOSITORY https://github.com/martinus/nanobench.git
		GIT_TAG v4.3.11
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(nanobench)
endif()


# === 配置宏定义 === #
# 项目目标 #
target_compile_definitions(Texpose PRIVATE
	_CRT_SECURE_NO_WARNINGS
)

# 项目测试目标 #
if(PROJECT_IS_TOP_LEVEL)
	target_compile_definitions(TexposeTest PRIVATE
		# 编译器宏
		_CRT_SECURE_NO_WARNINGS

		# 路径宏
		PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)
endif()


# === 配置链接库 === #
if(PROJECT_IS_TOP_LEVEL)
	# 内部库 #
	target_link_libraries(TexposeTest PRIVATE
		Texpose
	)

	# 外部库 #
	target_link_libraries(TexposeTest PRIVATE
		nanobench
	)
endif()