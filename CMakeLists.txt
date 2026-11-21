cmake_minimum_required(VERSION 3.25)
project(PegglePort C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PP_FETCH_DEPS "Download and build SDL3, OpenJPEG, Ogg and Vorbis instead of using system copies" OFF)

add_library(pp_flags INTERFACE)
if(MSVC)
  target_compile_options(pp_flags INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:/fp:strict;/W3;/utf-8>")
  target_compile_definitions(pp_flags INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(pp_flags INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off;-fno-fast-math;-Wall;-Wextra;-Wno-unused-parameter;-Wno-sign-compare>")
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
  src/gamefind.cpp
  src/gamecopy.cpp
  src/vfs.cpp
  src/level.cpp
  src/sim.cpp
  src/masters.cpp
  src/rules.cpp
  src/rules_powers.cpp
  src/zen.cpp
  src/aiscore.cpp
  src/mover.cpp
  src/follow.cpp
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
  src/online/discovery.cpp
  src/online/endpoint.cpp
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
  target_link_libraries(pp_core PUBLIC ws2_32 advapi32 shlwapi)
endif()

option(PP_STEAM "Steam presence, lobbies and peer networking (needs the Steamworks SDK)" OFF)
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
  message(STATUS "Steam connectivity: ${PP_STEAM_API}")
endif()

include(FetchContent)
include(cmake/openmpt.cmake)

add_library(pp_audio STATIC src/audio.cpp)
target_include_directories(pp_audio PUBLIC src third_party)
target_link_libraries(pp_audio PUBLIC pp_core SDL3::SDL3 ${PP_VORBIS_TARGETS} pp_openmpt)
target_compile_definitions(pp_audio PUBLIC PP_HAVE_OPENMPT)

FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lua)

set(_lua_names lapi lauxlib lctype ldebug ldo ldump lfunc lgc llex lmathlib lmem lopcodes lparser ltable lundump lzio)
set(PP_LUA_SOURCES "")
foreach(_n ${_lua_names})
  list(APPEND PP_LUA_SOURCES ${lua_SOURCE_DIR}/${_n}.c)
endforeach()
set_source_files_properties(${PP_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(pp_lua STATIC ${PP_LUA_SOURCES} src/mods/lua_lstate.cpp src/mods/lua_lbaselib.cpp src/mods/lua_lcode.cpp src/mods/lua_lcorolib.cpp src/mods/lua_lobject.cpp src/mods/lua_lstring.cpp src/mods/lua_lstrlib.cpp src/mods/lua_ltablib.cpp src/mods/lua_ltm.cpp src/mods/lua_lutf8lib.cpp src/mods/lua_lvm.cpp)
target_include_directories(pp_lua PUBLIC ${lua_SOURCE_DIR})
set_target_properties(pp_lua PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(pp_lua PRIVATE /fp:strict /W0)
else()
  target_compile_options(pp_lua PRIVATE -ffp-contract=off -fno-fast-math -w)
endif()

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git GIT_TAG v3.6.7 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(mbedtls)

find_package(Threads REQUIRED)
if(WIN32)
  set(PP_HOSTPROC_SOURCE src/mods/hostproc_win.cpp)
  set(PP_SANDBOX_SOURCE src/modhost/sandbox_win.cpp)
elseif(APPLE)
  set(PP_HOSTPROC_SOURCE src/mods/hostproc_posix.cpp)
  set(PP_SANDBOX_SOURCE src/modhost/sandbox_mac.cpp)
else()
  set(PP_HOSTPROC_SOURCE src/mods/hostproc_posix.cpp)
  set(PP_SANDBOX_SOURCE src/modhost/sandbox_linux.cpp)
endif()
add_library(pp_mods STATIC
  src/mods/json.cpp src/mods/manifest.cpp src/mods/zip.cpp src/mods/scanner.cpp
  src/mods/luamod.cpp src/mods/netapi.cpp src/mods/native.cpp src/mods/manager.cpp ${PP_HOSTPROC_SOURCE})
target_include_directories(pp_mods PUBLIC src third_party include)
target_link_libraries(pp_mods PUBLIC pp_core pp_flags pp_lua SDL3::SDL3 ${CMAKE_DL_LIBS} Threads::Threads)
add_library(pp_tls STATIC src/mods/tls.cpp src/online/websocket.cpp)
target_include_directories(pp_tls PUBLIC src)
target_link_libraries(pp_tls PUBLIC pp_flags PRIVATE mbedtls mbedx509 mbedcrypto)
target_link_libraries(pp_core PUBLIC pp_tls Threads::Threads)
if(WIN32)
  target_link_libraries(pp_tls PUBLIC ws2_32 crypt32 bcrypt)
endif()
if(APPLE)
  target_link_libraries(pp_tls PUBLIC "-framework Security" "-framework CoreFoundation")
endif()

add_library(pp_editor_core STATIC src/editor/model.cpp src/editor/json.cpp src/editor/editor.cpp)
target_include_directories(pp_editor_core PUBLIC src third_party)
target_link_libraries(pp_editor_core PUBLIC pp_core PRIVATE pp_mods)

add_library(pp_ui STATIC
  src/profile.cpp src/profile_import.cpp
  src/ui/res.cpp src/ui/text.cpp src/ui/text_font.cpp src/ui/gfx.cpp src/ui/widgets.cpp src/ui/dialog.cpp src/ui/dialogs.cpp
  src/ui/stages.cpp src/ui/app.cpp src/ui/app2.cpp src/ui/screens.cpp src/ui/screens2.cpp src/ui/other_game.cpp src/ui/game_screen.cpp src/ui/game_control.cpp src/ui/run.cpp src/ui/start.cpp src/ui/nights.cpp src/ui/sparkle.cpp src/ui/fireworks.cpp src/ui/fairies.cpp src/ui/nights_loading.cpp src/ui/storywin.cpp src/ui/pad.cpp
  src/ui/character_dialog.cpp src/ui/slot_dialog.cpp src/ui/replay.cpp src/ui/replay_ui.cpp src/ui/session.cpp src/ui/import_dialog.cpp src/ui/hooks.cpp src/ui/visuals_dialog.cpp
  src/ui/mods_screen.cpp src/ui/mod_apply.cpp src/ui/consent_dialog.cpp src/ui/log_view.cpp
  src/online/discord.cpp src/online/online_ui.cpp src/online/online_match.cpp src/online/level_names.cpp src/online/online_text.cpp)
target_include_directories(pp_ui PUBLIC src third_party)
target_compile_definitions(pp_ui PUBLIC PP_UI_HAVE_FONT)
target_link_libraries(pp_ui PUBLIC pp_core pp_audio pp_mods SDL3::SDL3)

add_executable(peggleport WIN32
  src/main.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
  src/mods/overlay.cpp src/editor/screen.cpp)
target_link_libraries(peggleport PRIVATE pp_ui pp_core pp_audio pp_mods pp_editor_core SDL3::SDL3)
add_executable(pp_edition_test EXCLUDE_FROM_ALL
  tests/edition_test.cpp tests/launcher_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
  src/mods/overlay.cpp src/editor/screen.cpp)
target_link_libraries(pp_edition_test PRIVATE pp_ui pp_editor_core)
add_executable(pp_wow_pack EXCLUDE_FROM_ALL examples/mods/build_wow_pack.cpp)
target_link_libraries(pp_wow_pack PRIVATE pp_core)
set_target_properties(pp_wow_pack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
  target_link_options(pp_wow_pack PRIVATE -municode)
endif()
option(PP_HD_TOOLS "Build the HD art reference exporter and presentation tests" OFF)
if(PP_HD_TOOLS)
  enable_testing()
  add_executable(pp_build_hd_mod tools/build_hd_mod.cpp)
  target_link_libraries(pp_build_hd_mod PRIVATE pp_core)
  add_executable(pp_hd_art_test tests/hd_art_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp src/ui/sparkle.cpp)
  target_link_libraries(pp_hd_art_test PRIVATE pp_core SDL3::SDL3)
  set_target_properties(pp_build_hd_mod PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_test(NAME hd_art COMMAND pp_hd_art_test "${CMAKE_CURRENT_BINARY_DIR}/hd-art-fixtures")
  foreach(_test title_launch dialog_position online_invite_controls shot_meter claude_board_render instant_replay_input fever_cam_render super_guide_composition custom_picker how_to_play_input)
    add_executable(pp_${_test}_test tests/${_test}_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
      src/mods/overlay.cpp src/editor/screen.cpp)
    target_link_libraries(pp_${_test}_test PRIVATE pp_ui pp_editor_core)
  endforeach()
  add_executable(pp_console_menu_test tests/console_menu_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_console_menu_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_gameplay_render_test tests/gameplay_render_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_gameplay_render_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_online_ui_review_test tests/online_ui_review_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_online_ui_review_test PRIVATE pp_ui pp_editor_core)
  set(PP_CONSOLE_TEST_DELUXE "" CACHE PATH "Peggle Deluxe game files for the console menu test")
  set(PP_CONSOLE_TEST_NIGHTS "" CACHE PATH "Peggle Nights game files for the console menu test")
  set(PP_CONSOLE_TEST_ART "" CACHE PATH "Local PS3 art pack for the console menu test")
  if(PP_CONSOLE_TEST_DELUXE AND PP_CONSOLE_TEST_NIGHTS AND PP_CONSOLE_TEST_ART)
    foreach(_test title_launch dialog_position online_invite_controls custom_picker how_to_play_input)
      add_test(NAME ${_test} COMMAND pp_${_test}_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_NIGHTS}"
        "${PP_CONSOLE_TEST_ART}" "${CMAKE_CURRENT_BINARY_DIR}/${_test}-fixtures")
    endforeach()
    if(NOT PP_STEAM)
      add_executable(pp_online_navigation_test tests/online_navigation_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
        src/mods/overlay.cpp src/editor/screen.cpp)
      target_link_libraries(pp_online_navigation_test PRIVATE pp_ui pp_editor_core)
      add_test(NAME online_navigation COMMAND pp_online_navigation_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_NIGHTS}"
        "${PP_CONSOLE_TEST_ART}" "${CMAKE_CURRENT_BINARY_DIR}/online_navigation-fixtures")
    endif()
    set_tests_properties(custom_picker PROPERTIES ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
    foreach(_test shot_meter claude_board_render instant_replay_input fever_cam_render super_guide_composition)
      add_test(NAME ${_test} COMMAND pp_${_test}_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_NIGHTS}" "${PP_CONSOLE_TEST_ART}")
    endforeach()
    add_test(NAME console_menu COMMAND pp_console_menu_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_NIGHTS}"
      "${PP_CONSOLE_TEST_ART}" "${CMAKE_CURRENT_BINARY_DIR}/console-menu-fixtures")
    add_test(NAME gameplay_render COMMAND pp_gameplay_render_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_NIGHTS}"
      "${PP_CONSOLE_TEST_ART}" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME online_ui_art COMMAND pp_online_ui_review_test "${PP_CONSOLE_TEST_DELUXE}" "${PP_CONSOLE_TEST_ART}")
    add_test(NAME online_ui_fallback COMMAND pp_online_ui_review_test "${PP_CONSOLE_TEST_DELUXE}")
    set_tests_properties(online_ui_art online_ui_fallback PROPERTIES ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR};TEMP=${CMAKE_CURRENT_BINARY_DIR};TMP=${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()
if(WIN32)
  enable_language(RC)
  target_sources(peggleport PRIVATE packaging/windows/peggleport.rc)
  target_include_directories(peggleport PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows")
  if(PP_STEAM)
    set(PP_STEAM_RUNTIME "${STEAMWORKS_SDK_DIR}/redistributable_bin/win64/steam_api64.dll")
    if(NOT EXISTS "${PP_STEAM_RUNTIME}")
      message(FATAL_ERROR "Steam runtime not found: ${PP_STEAM_RUNTIME}")
    endif()
    add_custom_command(TARGET peggleport POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PP_STEAM_RUNTIME}" "$<TARGET_FILE_DIR:peggleport>/steam_api64.dll"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/steam_appid.txt" "$<TARGET_FILE_DIR:peggleport>/steam_appid.txt")
  endif()
endif()

add_executable(pp_lobbyd src/lobbyd.cpp)
target_link_libraries(pp_lobbyd PRIVATE pp_core)

add_executable(pp_modhost src/modhost/main.cpp ${PP_SANDBOX_SOURCE})
target_include_directories(pp_modhost PRIVATE src include)
target_link_libraries(pp_modhost PRIVATE pp_flags ${CMAKE_DL_LIBS} Threads::Threads)
if(WIN32)
  target_link_libraries(pp_modhost PRIVATE advapi32 ws2_32)
endif()

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

set_target_properties(peggleport pp_lobbyd pp_modhost PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(PP_BUILD_TESTS "Build regression tests" ON)
option(PP_TEST_LAN_DISCOVERY "Run the LAN broadcast integration test" OFF)
if(PP_BUILD_TESTS)
  enable_testing()
  add_executable(pp_mod_control_api_test tests/mod_control_api_test.cpp)
  target_link_libraries(pp_mod_control_api_test PRIVATE pp_mods)
  add_test(NAME mod_control_api COMMAND pp_mod_control_api_test "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/mcp_agent")
  find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME mcp_server COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_mcp_server.py)
    set_tests_properties(mcp_server PROPERTIES TIMEOUT 60 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR};PYTHONDONTWRITEBYTECODE=1")
  endif()
  if(NOT PP_STEAM)
    set_target_properties(pp_edition_test PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_test(NAME launcher COMMAND pp_edition_test --launcher "${CMAKE_CURRENT_BINARY_DIR}/launcher-fixtures")
  endif()
  add_executable(pp_online_discovery_test tests/online_discovery_test.cpp)
  target_link_libraries(pp_online_discovery_test PRIVATE pp_core)
  if(PP_TEST_LAN_DISCOVERY)
    add_test(NAME online_discovery COMMAND pp_online_discovery_test)
    set_tests_properties(online_discovery PROPERTIES TIMEOUT 20)
  endif()
  add_executable(pp_pad_test tests/pad_test.cpp)
  target_link_libraries(pp_pad_test PRIVATE pp_ui)
  add_test(NAME pad COMMAND pp_pad_test "${CMAKE_CURRENT_BINARY_DIR}/pad-fixtures")
  set_tests_properties(pad PROPERTIES TIMEOUT 20)
  add_executable(pp_side_collision_test tests/side_collision_test.cpp)
  target_link_libraries(pp_side_collision_test PRIVATE pp_core)
  add_executable(pp_collision_regression_test tests/collision_regression_test.cpp)
  target_link_libraries(pp_collision_regression_test PRIVATE pp_core)
  add_executable(pp_claude_flippers_test tests/claude_flippers_test.cpp)
  target_link_libraries(pp_claude_flippers_test PRIVATE pp_core)
  add_executable(pp_miss_coin_toss_test tests/miss_coin_toss_test.cpp src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_miss_coin_toss_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_sweep_regression_test tests/sweep_regression_test.cpp)
  target_link_libraries(pp_sweep_regression_test PRIVATE pp_core)
  add_executable(pp_collision_kernel_test tests/collision_kernel_test.cpp)
  target_link_libraries(pp_collision_kernel_test PRIVATE pp_core)
  add_test(NAME collision_kernel COMMAND pp_collision_kernel_test)
  set_tests_properties(collision_kernel PROPERTIES TIMEOUT 20)
  add_executable(pp_ai_collision_filter_test tests/ai_collision_filter_test.cpp)
  target_link_libraries(pp_ai_collision_filter_test PRIVATE pp_core)
  add_test(NAME ai_collision_filter COMMAND pp_ai_collision_filter_test)
  set_tests_properties(ai_collision_filter PROPERTIES TIMEOUT 20)
  add_executable(pp_authored_width_test tests/authored_width_test.cpp)
  target_link_libraries(pp_authored_width_test PRIVATE pp_core)
  add_test(NAME authored_width COMMAND pp_authored_width_test)
  set_tests_properties(authored_width PROPERTIES TIMEOUT 20)
  add_executable(pp_editor_export_test tests/editor_export_test.cpp)
  target_link_libraries(pp_editor_export_test PRIVATE pp_editor_core)
  add_test(NAME editor_export COMMAND pp_editor_export_test "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(pp_editor_provider_test tests/editor_provider_test.cpp)
  target_link_libraries(pp_editor_provider_test PRIVATE pp_editor_core pp_mods)
  add_test(NAME editor_provider COMMAND pp_editor_provider_test "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(pp_editor_geometry_test tests/editor_geometry_test.cpp)
  target_link_libraries(pp_editor_geometry_test PRIVATE pp_editor_core)
  add_test(NAME editor_geometry COMMAND pp_editor_geometry_test "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(editor_geometry PROPERTIES TIMEOUT 30)
  add_executable(pp_editor_stage_test tests/editor_stage_test.cpp)
  target_link_libraries(pp_editor_stage_test PRIVATE pp_editor_core)
  add_test(NAME editor_stage COMMAND pp_editor_stage_test "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(pp_ui_controls_test
    tests/ui_controls_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_ui_controls_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_mod_scan_test
    tests/mod_scan_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_mod_scan_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_mcp_control_test
    tests/mcp_control_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_mcp_control_test PRIVATE pp_ui pp_editor_core)
  add_executable(pp_authored_render_test
    tests/authored_render_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp)
  target_link_libraries(pp_authored_render_test PRIVATE pp_ui)
  add_executable(pp_wide_background_test
    tests/wide_background_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp)
  target_link_libraries(pp_wide_background_test PRIVATE pp_ui)
  add_executable(pp_guide_regression_test
    tests/guide_regression_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp)
  target_link_libraries(pp_guide_regression_test PRIVATE pp_ui)
  add_executable(pp_authored_session_test
    tests/authored_session_test.cpp
    src/render.cpp src/hud.cpp src/effects.cpp src/emitfx.cpp
    src/mods/overlay.cpp src/editor/screen.cpp)
  target_link_libraries(pp_authored_session_test PRIVATE pp_ui pp_editor_core)
  set(PP_TEST_DELUXE_DIR "" CACHE PATH "Peggle Deluxe game files for regression tests")
  set(PP_TEST_NIGHTS_DIR "" CACHE PATH "Peggle Nights game files for UI regression tests")
  if(PP_TEST_DELUXE_DIR)
    add_test(NAME side_collision COMMAND pp_side_collision_test "${PP_TEST_DELUXE_DIR}")
    set_tests_properties(side_collision PROPERTIES TIMEOUT 20)
    add_test(NAME sweep_regression COMMAND pp_sweep_regression_test "${PP_TEST_DELUXE_DIR}")
    set_tests_properties(sweep_regression PROPERTIES TIMEOUT 30)
    add_test(NAME ui_controls COMMAND pp_ui_controls_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(ui_controls PROPERTIES TIMEOUT 30)
    add_test(NAME mod_scan COMMAND pp_mod_scan_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(mod_scan PROPERTIES TIMEOUT 30 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME authored_render COMMAND pp_authored_render_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(authored_render PROPERTIES TIMEOUT 30)
    add_test(NAME wide_background COMMAND pp_wide_background_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(wide_background PROPERTIES TIMEOUT 45)
    add_test(NAME guide_regression COMMAND pp_guide_regression_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(guide_regression PROPERTIES TIMEOUT 30)
    add_test(NAME authored_session COMMAND pp_authored_session_test "${PP_TEST_DELUXE_DIR}" ${PP_TEST_NIGHTS_DIR})
    set_tests_properties(authored_session PROPERTIES TIMEOUT 30 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
    if(PP_TEST_NIGHTS_DIR)
      add_test(NAME mcp_control COMMAND pp_mcp_control_test "${PP_TEST_DELUXE_DIR}" "${PP_TEST_NIGHTS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
      set_tests_properties(mcp_control PROPERTIES TIMEOUT 45 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
      if(Python3_Interpreter_FOUND)
        add_test(NAME mcp_play COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_play_test.py"
          "$<TARGET_FILE:pp_mcp_control_test>" "${PP_TEST_DELUXE_DIR}" "${PP_TEST_NIGHTS_DIR}"
          "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/mcp_agent")
        set_tests_properties(mcp_play PROPERTIES TIMEOUT 45 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR};PYTHONDONTWRITEBYTECODE=1")
      endif()
      add_test(NAME claude_flippers COMMAND pp_claude_flippers_test "${PP_TEST_DELUXE_DIR}" "${PP_TEST_NIGHTS_DIR}")
      add_test(NAME miss_coin_toss COMMAND pp_miss_coin_toss_test "${PP_TEST_DELUXE_DIR}" "${PP_TEST_NIGHTS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/miss-coin-fixtures")
      add_test(NAME collision_regression COMMAND pp_collision_regression_test "${PP_TEST_DELUXE_DIR}" "${PP_TEST_NIGHTS_DIR}")
      set_tests_properties(collision_regression PROPERTIES TIMEOUT 30)
    endif()
  endif()
endif()
