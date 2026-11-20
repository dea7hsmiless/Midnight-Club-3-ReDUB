# Net: XNet (System Link) API emulation over host UDP sockets.
#
# The title's statically linked XNET library cannot run (no NIC interrupts),
# so its exported thunks are answered by this instead; the game-side wrappers
# live in src/games/<game>/recomp_manual.c, gated on MC3_NETPLAY.

add_library(xbox_net STATIC
    xnet_hle.c
)

target_include_directories(xbox_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XBOXRECOMP_SRC_DIR}
)
target_link_libraries(xbox_net PUBLIC platform)

if(MSVC)
    target_compile_options(xbox_net PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_net PRIVATE -Wall -Wextra -O2)
endif()

# Winsock, on Windows only.
if(WIN32)
    target_link_libraries(xbox_net PUBLIC ws2_32)
endif()
