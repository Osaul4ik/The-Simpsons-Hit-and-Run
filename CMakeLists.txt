cmake_minimum_required(VERSION 3.6.3...3.10)

project(SRR2)

set(SRR2_TITLE "Simpsons Hit & Run")
set(SRR2_VERSION "00.61")
set(SRR2_FILENAME "Simpsons")
set(SRR2_REGION "NTSC" CACHE STRING "Select PAL for the EFIGS multi-language version")
set_property(CACHE SRR2_REGION PROPERTY STRINGS "NTSC" "PAL")

option(SRR2_USE_DLMALLOC "Use Doug Lea's memory allocator for fixed-size heaps" OFF)
option(SRR2_REROUTE_ALL_HEAP_ALLOCATIONS "Reroute all memory allocations by new operator to internal heaps" OFF)
option(SRR2_USE_BINK "Use RAD Game Tools Bink SDK instead of FFmpeg" OFF)
set(SRR2_FFMPEG_PREFIX "" CACHE PATH "Install prefix of a custom FFmpeg build to use instead of the system/vitasdk one")
option(SRR2_BUILD_TESTS "Build sample programs for internal libraries" ON)
option(SRR2_MICROPROFILE "Use microprofile instead of the internal profiler" OFF)
option(SRR2_P3D_CG "Use CG shaders rather than GLSL for OpenGL ES" OFF)
option(SRR2_LOG "Enable release debug output and asserts output (written to ux0:data/simpsons/log.txt on the Vita)" OFF)
if(VITA)
	# The native GXM backend is the default on the Vita: it talks to the GPU directly and
	# lets the game place textures in all of the 128 MB of VRAM (CDRAM). The OpenGL/GLES
	# backends only work on the Vita through vitaGL, which is much slower and takes over
	# all of the device memory.
	set(SRR2_P3D_PDDI "GXM" CACHE STRING "Pure3D PDDI implementation")
else()
	set(SRR2_P3D_PDDI "OpenGL" CACHE STRING "Pure3D PDDI implementation")
endif()
if(VITA)
	if(SRR2_P3D_PDDI STREQUAL "GXM")
		set(_SRR2_VITAGL_DEFAULT OFF)
	else()
		set(_SRR2_VITAGL_DEFAULT ON)
	endif()
	option(SRR2_USE_VITAGL "Use VitaGL as an OpenGL (ES) implementation" ${_SRR2_VITAGL_DEFAULT})
	if(SRR2_P3D_PDDI STREQUAL "GXM" AND SRR2_USE_VITAGL)
		message(WARNING "SRR2_P3D_PDDI=GXM does not use vitaGL: forcing SRR2_USE_VITAGL=OFF")
		set(SRR2_USE_VITAGL OFF CACHE BOOL "" FORCE)
	endif()
	set_property(CACHE SRR2_P3D_PDDI PROPERTY STRINGS "OpenGL" "GLES1" "GLES2" "GXM")
else()
	set_property(CACHE SRR2_P3D_PDDI PROPERTY STRINGS "OpenGL" "GLES1" "GLES2")
endif()

if(VITA)
	include("${VITASDK}/share/vita.cmake" REQUIRED)

	add_definitions(-DRAD_VITA)
	## Configuration options for this app
	# Display name (under bubble in LiveArea)
	set(VITA_APP_NAME ${SRR2_TITLE})
	# Unique ID must be exactly 9 characters. Recommended: XXXXYYYYY where X =
	# unique string of developer and Y = a unique number for this app
	set(VITA_TITLEID  "ROADRAGE2")
	# Optional version string to show in LiveArea's more info screen
	set(VITA_VERSION ${SRR2_VERSION})
	# Size of the newlib heap (backing malloc/new) in MB, see code/main/win32main.cpp
	# Memory budget of the 368 MB (GXM backend), measured from the boot log:
	#   newlib heap (SRR2_VITA_HEAP_MB)          192 MB   malloc/new, game heaps
	#   sceLibc heap + executable + stacks      ~17 MB
	#   foundation init (audio, file cache...)  ~37 MB
	#   radMemorySpace user block (below)        80 MB   GPU-readable: vertex data, GXM rings, texture overflow
	#   -> ~40 MB left as headroom
	# The 128 MB of CDRAM is taken at runtime: all of it except 8 MB for GXM (radmemory).
	set(SRR2_VITA_HEAP_MB 192 CACHE STRING "Vita newlib heap size in MB")
	add_definitions(-DRAD_VITA_HEAP_MB=${SRR2_VITA_HEAP_MB})
	set(SRR2_VITA_GXM_USER_MB 80 CACHE STRING "GXM backend: GPU-readable user RAM block in MB")
	add_definitions(-DRAD_VITA_GXM_USER_MB=${SRR2_VITA_GXM_USER_MB})
	# Extended memory mode: raises the user memory budget of the app from 256 MB
	# to 368 MB
	set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif()

if(NOT VITA)
	find_package(SDL3)
endif()
if(SDL3_FOUND)
	get_target_property(SDL_INCLUDE_DIR SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
	include_directories(${SDL_INCLUDE_DIR}/SDL3)
	set(SDL_LIBRARY SDL3::SDL3)
else()
	find_package(SDL2 REQUIRED)
	set(SDL_LIBRARY SDL2::SDL2 SDL2::SDL2main)
endif()
find_package(PNG REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(OpenAL REQUIRED)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	add_compile_options(-fvisibility=hidden)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(
			-Werror
			-ggdb
		)
	endif()
	if(VITA)
		add_compile_options(
			-mtune=cortex-a9
			-mfpu=neon
		)
	endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(-DRAD_DEBUG)
else()
	add_definitions(-DRAD_RELEASE)
endif()

add_definitions(
	-DRAD_CONSOLE
	-DRAD_WIN32
	-DRAD_SDL
)

if(SRR2_REGION STREQUAL "PAL")
	add_definitions(-DPAL)
endif()
if(SRR2_USE_DLMALLOC)
	add_definitions(-DUSE_DOUG_LEA_HEAP)
endif()
if(SRR2_REROUTE_ALL_HEAP_ALLOCATIONS)
	add_definitions(-DREROUTE_ALL_HEAP_ALLOCATIONS)
endif()
if(SRR2_USE_BINK)
	add_definitions(-DRAD_MOVIEPLAYER_USE_BINK)
else()
	find_package(PkgConfig REQUIRED)
	if(SRR2_FFMPEG_PREFIX)
		# Custom FFmpeg build (on the Vita: scripts/build-ffmpeg-vita.sh). Needed
		# because the vdpm FFmpeg has no Theora decoder, which the .rmv movies use.
		set(ENV{PKG_CONFIG_PATH} "${SRR2_FFMPEG_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	elseif(VITA)
		message(WARNING "SRR2_FFMPEG_PREFIX is not set: the stock vitasdk FFmpeg cannot decode "
			"the Theora .rmv movies, so they will be skipped. "
			"Run scripts/build-ffmpeg-vita.sh and pass -DSRR2_FFMPEG_PREFIX=<prefix>.")
	endif()
	pkg_check_modules(FFmpeg REQUIRED IMPORTED_TARGET
			libavformat
			libavcodec
			libavutil
			libswresample
			libswscale
	)
	if(SRR2_FFMPEG_PREFIX)
		# pkg-config can still resolve to the stock vitasdk FFmpeg (no Theora, with
		# libmp3lame), so link the custom static libraries by absolute path.
		set(_ffl "${SRR2_FFMPEG_PREFIX}/lib")
		foreach(_lib avformat avcodec swresample swscale avutil)
			if(NOT EXISTS "${_ffl}/lib${_lib}.a")
				message(FATAL_ERROR "${_ffl}/lib${_lib}.a not found: run scripts/build-ffmpeg-vita.sh")
			endif()
		endforeach()
		set_property(TARGET PkgConfig::FFmpeg PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SRR2_FFMPEG_PREFIX}/include")
		set_property(TARGET PkgConfig::FFmpeg PROPERTY INTERFACE_LINK_LIBRARIES
			"${_ffl}/libavformat.a;${_ffl}/libavcodec.a;${_ffl}/libswresample.a;${_ffl}/libswscale.a;${_ffl}/libavutil.a")
	endif()
	if(VITA)
		# libmp3lame.a (pulled in by FFmpeg) needs mpg123, but its pkg-config file
		# does not list it. Make it a dependency of FFmpeg so that it is placed
		# after libmp3lame on every link line that uses FFmpeg.
		target_link_libraries(PkgConfig::FFmpeg INTERFACE mpg123)
	endif()
endif()
if(SRR2_MICROPROFILE)
	add_definitions(-DMICROPROFILE_ENABLED)
endif()
if(SRR2_LOG)
	add_definitions(-DRAD_LOG)
endif()
if(SRR2_P3D_CG)
	add_definitions(-DRAD_CG)
endif()
if(SRR2_USE_VITAGL)
    add_definitions(-DRAD_VITAGL)
endif()
if(SRR2_P3D_PDDI STREQUAL "GXM")
    add_definitions(-DRAD_GXM)
endif()
if(SRR2_P3D_PDDI STREQUAL "GLES1")
	add_definitions(-DRAD_GLES)
	add_definitions(-DRAD_GLES_VERSION=1)
endif()
if(SRR2_P3D_PDDI STREQUAL "GLES2")
	add_definitions(-DRAD_GLES)
	add_definitions(-DRAD_GLES_VERSION=2)
endif()

if(WIN32)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(libs)
add_subdirectory(code)

if(VITA)
	## Create Vita files
	if(SRR2_P3D_PDDI STREQUAL "GLES2" AND NOT SRR2_USE_VITAGL)
		vita_create_self(${PROJECT_NAME}.self code/${PROJECT_NAME} UNSAFE)
		# The FILE directive lets you add additional files to the VPK, the syntax is
		# FILE src_path dst_path_in_vpk. In this case, we add the LiveArea paths.
		vita_create_vpk(${SRR2_FILENAME}${SRR2_REGION}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
			VERSION ${VITA_VERSION}
			NAME ${VITA_APP_NAME}
			FILE sce_sys/icon0.png sce_sys/icon0.png
			sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
			sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
			sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
			art art

			#suprx
			module/libGLESv2.suprx module/libGLESv2.suprx
			module/libgpu_es4_ext.suprx module/libgpu_es4_ext.suprx
			module/libIMGEGL.suprx module/libIMGEGL.suprx
			module/libpvrPSP2_WSEGL.suprx module/libpvrPSP2_WSEGL.suprx
		)
	else()
		vita_create_self(${PROJECT_NAME}.self code/${PROJECT_NAME})
		# The FILE directive lets you add additional files to the VPK, the syntax is
		# FILE src_path dst_path_in_vpk. In this case, we add the LiveArea paths.
		vita_create_vpk(${SRR2_FILENAME}${SRR2_REGION}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
			VERSION ${VITA_VERSION}
			NAME ${VITA_APP_NAME}
			FILE sce_sys/icon0.png sce_sys/icon0.png
			sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
			sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
			sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
			art art

			#gxp
			libs/pure3d/pddi/gxm/shaders/vertex_cg.gxp shaders/vertex_cg.gxp
			libs/pure3d/pddi/gxm/shaders/color_cg.gxp shaders/color_cg.gxp
			libs/pure3d/pddi/gxm/shaders/texture_cg.gxp shaders/texture_cg.gxp
			libs/pure3d/pddi/gxm/shaders/alpha_cg.gxp shaders/alpha_cg.gxp
		)
	endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
	nx_generate_nacp(OUTPUT ${PROJECT_NAME}.nacp
		NAME ${SRR2_TITLE}
		AUTHOR "Radical Entertainment"
		VERSION ${SRR2_VERSION}
	)
	nx_create_nro(code/${PROJECT_NAME}
		OUTPUT ${SRR2_FILENAME}${SRR2_REGION}.nro
		ICON ${PROJECT_NAME}.jpg
		NACP ${PROJECT_NAME}.nacp
		ROMFS romfs
	)
endif()