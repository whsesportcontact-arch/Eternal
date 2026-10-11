cmake_minimum_required(VERSION 3.20)
project(EternalLauncher CXX RC)

# Where the launcher downloads the newest Eternal.dll from (set by the GitHub workflow).
set(ETERNAL_DLL_URL "" CACHE STRING "URL of the latest Eternal.dll")
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_executable(EternalLauncher WIN32 main.cpp launcher.rc)
set_target_properties(EternalLauncher PROPERTIES OUTPUT_NAME "EternalLauncher"
                      MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ redistributable needed
target_compile_features(EternalLauncher PRIVATE cxx_std_17)
target_include_directories(EternalLauncher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(EternalLauncher PRIVATE UNICODE _UNICODE)
target_compile_options(EternalLauncher PRIVATE /FIconfig.h)
target_link_libraries(EternalLauncher PRIVATE user32 gdi32 shell32 advapi32 urlmon wininet)
