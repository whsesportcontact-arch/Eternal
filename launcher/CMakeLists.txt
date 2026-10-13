cmake_minimum_required(VERSION 3.20)
project(EntaLauncher CXX RC)

# Set by the GitHub workflow. ENTA_REPO is "owner/name"; the launcher updates itself and Enta.dll from that
# repo's releases ("latest" = stable, "dev" = test builds). ENTA_BUILD_ID is compared with version.txt.
set(ENTA_REPO "" CACHE STRING "GitHub repo to download updates from (owner/name)")
set(ENTA_BUILD_ID "" CACHE STRING "Build id of this launcher (matches version.txt in its release)")
set(ENTA_CHANNEL "latest" CACHE STRING "Release tag this launcher is published in (latest or dev)")
# Path to an Enta.dll to pack inside the launcher, so the launcher is the only file anyone downloads.
# The workflow points this at the DLL it just built; leave it empty for a launcher without one.
set(ENTA_EMBED_DLL "" CACHE FILEPATH "Enta.dll to embed in the launcher")
if(ENTA_EMBED_DLL)
    if(NOT EXISTS "${ENTA_EMBED_DLL}")
        message(FATAL_ERROR "ENTA_EMBED_DLL does not exist: ${ENTA_EMBED_DLL}")
    endif()
    file(TO_CMAKE_PATH "${ENTA_EMBED_DLL}" ENTA_EMBED_DLL_RC) # forward slashes are safe inside an .rc string
    set(ENTA_HAS_EMBEDDED_DLL 1)
else()
    set(ENTA_HAS_EMBEDDED_DLL 0)
endif()
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
configure_file(embedded_dll.rc.in ${CMAKE_CURRENT_BINARY_DIR}/embedded_dll.rc)

add_executable(EntaLauncher WIN32 main.cpp launcher.rc ${CMAKE_CURRENT_BINARY_DIR}/embedded_dll.rc)
if(ENTA_EMBED_DLL)
    # rebuild the launcher when the DLL changes
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/embedded_dll.rc PROPERTIES OBJECT_DEPENDS "${ENTA_EMBED_DLL}")
endif()
set_target_properties(EntaLauncher PROPERTIES OUTPUT_NAME "EntaLauncher"
                      MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ redistributable needed
target_compile_features(EntaLauncher PRIVATE cxx_std_17)
target_include_directories(EntaLauncher PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(EntaLauncher PRIVATE UNICODE _UNICODE)
# /utf-8: the source is UTF-8; without it MSVC garbles non-ASCII text (the old close button showed "âœ•")
target_compile_options(EntaLauncher PRIVATE /FIconfig.h /utf-8)
target_link_libraries(EntaLauncher PRIVATE user32 gdi32 shell32 advapi32 urlmon wininet ole32 windowscodecs
                      msimg32 comdlg32)
