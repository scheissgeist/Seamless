add_library(ds2_seamless_coop SHARED
    core/dllmain.cpp
    core/mod.cpp
    hooks/session_hooks.cpp
    hooks/network_hooks.cpp
    hooks/game_state_hooks.cpp
    network/peer_manager.cpp
    network/packet_handler.cpp
    session/session_manager.cpp
    sync/progress_sync.cpp
    sync/player_sync.cpp
    utils/logger.cpp
    utils/memory.cpp
    utils/pattern_scanner.cpp
    utils/address_resolver.cpp
    ui/overlay.cpp
    ui/renderer.cpp
    ui/title_notifier.cpp
)

target_include_directories(ds2_seamless_coop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${THIRD_PARTY_DIR}/minhook/include
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/imgui/backends
)

target_link_libraries(ds2_seamless_coop PRIVATE
    minhook
    imgui
    ws2_32
    d3d11
    dxgi
)

# Set output name and compiler flags
set_target_properties(ds2_seamless_coop PROPERTIES
    OUTPUT_NAME "ds2_seamless_coop"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Static link the C++ runtime to avoid dependency issues
target_compile_options(ds2_seamless_coop PRIVATE
    $<$<CONFIG:Release>:/MT>
    $<$<CONFIG:Debug>:/MTd>
)

# Copy to game directory if specified
if(DEFINED DS2_GAME_DIR)
    add_custom_command(TARGET ds2_seamless_coop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ds2_seamless_coop>
        ${DS2_GAME_DIR}/ds2_seamless_coop.dll
    )
endif()

