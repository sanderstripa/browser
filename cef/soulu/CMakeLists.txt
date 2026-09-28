# Soulu native CEF shell. This directory is copied into cef-project/examples
# by the Windows workflow, so it intentionally uses cef-project's helpers.
set(SOULU_SRCS
  app_factory.h
  app_browser.cc
  app_renderer.cc
  app_other.cc
  browser_client.h
  browser_client.cc
  browser_window.h
  browser_window.cc
  frosted_backdrop.h
  frosted_backdrop.cc
  shell_surface.h
  shell_surface.cc
  main.cc
  resource.h
  win/resource.rc
)

# CEF appends /std:c++17 in its target flags; normalize it before its
# helpers run so that it cannot override CXX_STANDARD for C++/WinRT.
string(REPLACE "/std:c++17" "/std:c++20" CEF_COMPILER_FLAGS "${CEF_COMPILER_FLAGS}")
string(REPLACE "/std:c++17" "/std:c++20" CEF_CXX_COMPILER_FLAGS "${CEF_CXX_COMPILER_FLAGS}")
set(EXAMPLE_TARGET "soulu")
SET_EXAMPLE_SOURCE_GROUPS("${SOULU_SRCS}")

if(USE_SANDBOX)
  add_library(${EXAMPLE_TARGET} SHARED ${SOULU_SRCS})
  SET_EXAMPLE_LIBRARY_TARGET_PROPERTIES(${EXAMPLE_TARGET})
  COPY_SINGLE_FILE(${EXAMPLE_TARGET} ${CEF_BINARY_DIR}/bootstrap.exe ${EXAMPLE_TARGET_OUT_DIR}/${EXAMPLE_TARGET}.exe)
else()
  add_executable(${EXAMPLE_TARGET} WIN32 ${SOULU_SRCS})
  SET_EXAMPLE_EXECUTABLE_TARGET_PROPERTIES(${EXAMPLE_TARGET})
endif()

add_dependencies(${EXAMPLE_TARGET} shared)
target_link_libraries(${EXAMPLE_TARGET} shared dwmapi ws2_32 advapi32 windowsapp CoreMessaging)
target_compile_definitions(${EXAMPLE_TARGET} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(${EXAMPLE_TARGET} PRIVATE /utf-8 /EHsc /Zc:__cplusplus)
  target_link_options(${EXAMPLE_TARGET} PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/win/soulu.manifest")
endif()
set_target_properties(${EXAMPLE_TARGET} PROPERTIES OUTPUT_NAME "Soulu" CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

add_custom_command(TARGET ${EXAMPLE_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui"
          "$<TARGET_FILE_DIR:${EXAMPLE_TARGET}>/ui"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets/vpn"
          "$<TARGET_FILE_DIR:${EXAMPLE_TARGET}>/vpn"
  VERBATIM)

