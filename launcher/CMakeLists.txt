cmake_minimum_required(VERSION 3.20)
project(EntaLauncher CXX RC)

# Where the launcher downloads the newest Enta.dll from (set by the GitHub workflow).
set(ENTA_DLL_URL "" CACHE STRING "URL of the latest Enta.dll")
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_executable(EntaLauncher WIN32 main.cpp launcher.rc)
set_target_properties(EntaLauncher PROPERTIES OUTPUT_NAME "EntaLauncher"
                      MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ redistributable needed
target_compile_features(EntaLauncher PRIVATE cxx_std_17)
target_include_directories(EntaLauncher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(EntaLauncher PRIVATE UNICODE _UNICODE)
target_compile_options(EntaLauncher PRIVATE /FIconfig.h)
target_link_libraries(EntaLauncher PRIVATE user32 gdi32 shell32 advapi32 urlmon wininet)
