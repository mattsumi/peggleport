cmake_minimum_required(VERSION 3.25)
project(PegglePort C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(PP_FETCH_DEPS "Download and build SDL3, OpenJPEG, Ogg and Vorbis instead of using system copies" OFF)

add_library(pp_flags INTERFACE)
if(MSVC)
  target_compile_options(pp_flags INTERFACE /fp:strict /W3 /utf-8)
  target_compile_definitions(pp_flags INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(pp_flags INTERFACE -ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
endif()

if(PP_FETCH_DEPS)
  include(FetchContent)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_CODEC OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_DOC OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.2.24 GIT_SHALLOW TRUE)
  FetchContent_Declare(openjpeg GIT_REPOSITORY https://github.com/uclouvain/openjpeg.git GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
  FetchContent_Declare(ogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.5 GIT_SHALLOW TRUE OVERRIDE_FIND_PACKAGE)
  FetchContent_Declare(vorbis GIT_REPOSITORY https://github.com/xiph/vorbis.git GIT_TAG v1.3.7 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3 openjpeg ogg vorbis)
  set(PP_OPENJPEG_TARGET openjp2)
  set(PP_VORBIS_TARGETS vorbisfile vorbis ogg)
else()
  find_package(SDL3 REQUIRED CONFIG)
  find_package(OpenJPEG REQUIRED CONFIG)
  set(PP_OPENJPEG_TARGET openjp2)
  find_library(VORBISFILE_LIB vorbisfile REQUIRED HINTS /opt/homebrew/lib /usr/local/lib)
  find_library(VORBIS_LIB vorbis REQUIRED HINTS /opt/homebrew/lib /usr/local/lib)
  find_library(OGG_LIB ogg REQUIRED HINTS /opt/homebrew/lib /usr/local/lib)
  find_path(VORBIS_INC vorbis/vorbisfile.h REQUIRED HINTS /opt/homebrew/include /usr/local/include)
  add_library(pp_vorbis INTERFACE)
  target_include_directories(pp_vorbis INTERFACE ${VORBIS_INC})
  target_link_libraries(pp_vorbis INTERFACE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
  set(PP_VORBIS_TARGETS pp_vorbis)
endif()

add_library(pp_core STATIC
  src/log.cpp
  src/vfs.cpp
  src/level.cpp
  src/sim.cpp
  src/rules.cpp
  src/rules_powers.cpp
  src/zen.cpp
  src/aiscore.cpp
  src/mover.cpp
  src/match.cpp
  src/net.cpp
  src/lobby.cpp
  src/image.cpp
  src/content.cpp
  src/physobj.cpp
  src/gun.cpp
  src/gunctl.cpp
  src/sweep.cpp
  src/brick.cpp
  src/font.cpp
  src/online/udp.cpp
  src/online/rudp.cpp
  src/online/rendezvous.cpp
  src/online/direct.cpp
  src/online/invite.cpp
  src/online/content_hash.cpp
  src/online/steam.cpp
)
target_link_libraries(pp_core PUBLIC pp_flags)
target_include_directories(pp_core PUBLIC src third_party)
if(PP_FETCH_DEPS)
  target_include_directories(pp_core PUBLIC ${openjpeg_SOURCE_DIR}/src/lib/openjp2 ${openjpeg_BINARY_DIR}/src/lib/openjp2)
else()
  target_include_directories(pp_core PUBLIC ${OPENJPEG_INCLUDE_DIRS})
endif()
target_link_libraries(pp_core PUBLIC ${PP_OPENJPEG_TARGET})
if(WIN32)
  target_link_libraries(pp_core PUBLIC ws2_32 advapi32)
endif()

option(PP_STEAM "Steam Rich Presence and friend invites (needs the Steamworks SDK)" OFF)
set(STEAMWORKS_SDK_DIR "" CACHE PATH "Steamworks SDK root (contains public/steam/steam_api.h)")
if(PP_STEAM)
  if(NOT EXISTS "${STEAMWORKS_SDK_DIR}/public/steam/steam_api.h")
    message(FATAL_ERROR "PP_STEAM=ON needs -DSTEAMWORKS_SDK_DIR=<sdk root> (public/steam/steam_api.h not found)")
  endif()
  target_compile_definitions(pp_core PUBLIC PP_STEAM)
  target_include_directories(pp_core PUBLIC "${STEAMWORKS_SDK_DIR}/public")
  if(WIN32)
    find_library(PP_STEAM_API steam_api64 PATHS "${STEAMWORKS_SDK_DIR}/redistributable_bin/win64" REQUIRED NO_DEFAULT_PATH)
  elseif(APPLE)
    find_library(PP_STEAM_API steam_api PATHS "${STEAMWORKS_SDK_DIR}/redistributable_bin/osx" REQUIRED NO_DEFAULT_PATH)
  else()
    find_library(PP_STEAM_API steam_api PATHS "${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64" REQUIRED NO_DEFAULT_PATH)
  endif()
  target_link_libraries(pp_core PUBLIC "${PP_STEAM_API}")
  message(STATUS "Steam Rich Presence: ${PP_STEAM_API}")
endif()

add_library(pp_audio STATIC src/audio.cpp)
target_include_directories(pp_audio PUBLIC src third_party)
target_link_libraries(pp_audio PUBLIC pp_core SDL3::SDL3 ${PP_VORBIS_TARGETS})
find_library(OPENMPT_LIB openmpt HINTS /opt/homebrew/lib /usr/local/lib)
find_path(OPENMPT_INC libopenmpt/libopenmpt.h HINTS /opt/homebrew/include /usr/local/include)
if(OPENMPT_LIB AND OPENMPT_INC)
  target_compile_definitions(pp_audio PUBLIC PP_HAVE_OPENMPT)
  target_include_directories(pp_audio PUBLIC ${OPENMPT_INC})
  target_link_libraries(pp_audio PUBLIC ${OPENMPT_LIB})
  message(STATUS "libopenmpt: ${OPENMPT_LIB}")
else()
  message(STATUS "libopenmpt not found: MO3 music disabled")
endif()

include(FetchContent)
FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lua)

set(_lua_names lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex lmathlib lmem lobject lopcodes lparser lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio)
set(PP_LUA_SOURCES "")
foreach(_n ${_lua_names})
  list(APPEND PP_LUA_SOURCES ${lua_SOURCE_DIR}/${_n}.c)
endforeach()
set_source_files_properties(${PP_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(pp_lua STATIC ${PP_LUA_SOURCES} src/mods/lua_lstate.cpp)
target_include_directories(pp_lua PUBLIC ${lua_SOURCE_DIR})
set_target_properties(pp_lua PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(pp_lua PRIVATE /fp:strict /W0)
else()
  target_compile_options(pp_lua PRIVATE -ffp-contract=off -fno-fast-math -w)
endif()

find_package(Threads REQUIRED)
add_library(pp_mods STATIC
  src/mods/json.cpp src/mods/manifest.cpp src/mods/zip.cpp src/mods/scanner.cpp
  src/mods/luamod.cpp src/mods/netapi.cpp src/mods/native.cpp src/mods/manager.cpp)
target_include_directories(pp_mods PUBLIC src third_party include)
target_link_libraries(pp_mods PUBLIC pp_core pp_flags pp_lua SDL3::SDL3 ${CMAKE_DL_LIBS} Threads::Threads)
if(WIN32)
  target_link_libraries(pp_mods PUBLIC ws2_32)
endif()

add_library(pp_editor_core STATIC src/editor/model.cpp src/editor/json.cpp src/editor/editor.cpp)
target_include_directories(pp_editor_core PUBLIC src third_party)
target_link_libraries(pp_editor_core PUBLIC pp_core)

add_library(pp_ui STATIC
  src/profile.cpp
  src/ui/res.cpp src/ui/text.cpp src/ui/text_font.cpp src/ui/gfx.cpp src/ui/widgets.cpp src/ui/dialog.cpp src/ui/dialogs.cpp
  src/ui/stages.cpp src/ui/app.cpp src/ui/app2.cpp src/ui/screens.cpp src/ui/screens2.cpp src/ui/game_screen.cpp src/ui/run.cpp src/ui/launcher.cpp src/ui/nights.cpp
  src/ui/character_dialog.cpp src/ui/slot_dialog.cpp src/ui/replay.cpp src/ui/replay_ui.cpp src/ui/hooks.cpp src/ui/visuals_dialog.cpp
  src/ui/mods_screen.cpp src/ui/consent_dialog.cpp src/ui/log_view.cpp
  src/online/online_ui.cpp src/online/online_match.cpp)
target_include_directories(pp_ui PUBLIC src third_party)
target_compile_definitions(pp_ui PUBLIC PP_UI_HAVE_FONT)
target_link_libraries(pp_ui PUBLIC pp_core pp_audio pp_mods SDL3::SDL3)

add_executable(peggleport WIN32
  src/main.cpp src/render.cpp src/hud.cpp src/effects.cpp
  src/mods/overlay.cpp src/editor/screen.cpp)
target_link_libraries(peggleport PRIVATE pp_ui pp_core pp_audio pp_mods pp_editor_core SDL3::SDL3)

add_executable(pp_lobbyd src/lobbyd.cpp)
target_link_libraries(pp_lobbyd PRIVATE pp_core)

add_library(hello_native MODULE examples/plugin/hello_native.c)
target_include_directories(hello_native PRIVATE include)
set_target_properties(hello_native PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/mods/hello_native"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/mods/hello_native")
foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${_cfg} _CFG)
  set_target_properties(hello_native PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/examples/mods/hello_native"
    RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/examples/mods/hello_native")
endforeach()
if(APPLE)
  set_target_properties(hello_native PROPERTIES SUFFIX ".dylib")
elseif(NOT WIN32)
  set_target_properties(hello_native PROPERTIES SUFFIX ".so")
endif()
add_custom_command(TARGET hello_native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/examples/plugin/hello_native.ini" "${CMAKE_BINARY_DIR}/examples/mods/hello_native/mod.ini")

set_target_properties(peggleport pp_lobbyd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
