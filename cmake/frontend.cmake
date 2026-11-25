# The graphical front end, shared by macOS, Linux and Windows: Qt Widgets written in plain C++ (no Designer
# files, no QML), SDL3 for the microphone level meter. platform_mac.mm / platform_linux.cpp /
# platform_windows.cpp hold the few platform differences (src/platform_qt.h).
if(NOT SETUP_HAVE_BOOST)
  message(FATAL_ERROR "The app requires Boost >=1.86 (Boost.Asio); set SETUP_BOOST_ROOT or disable SETUP_BUILD_APP")
endif()
# Local toolchains outside the default search path, e.g. the official universal Qt in ~/Qt/<version>/macos
set(SETUP_QT_ROOT "" CACHE PATH "Qt 6 installation prefix (contains lib/cmake/Qt6)")
set(SETUP_SDL3_ROOT "" CACHE PATH "SDL3 installation prefix (contains lib/cmake/SDL3)")
foreach(prefix IN ITEMS "${SETUP_QT_ROOT}" "${SETUP_SDL3_ROOT}")
  if(prefix)
    list(APPEND CMAKE_PREFIX_PATH "${prefix}")
  endif()
endforeach()
if(APPLE)
  # QMicrophonePermission (the macOS microphone prompt) needs Qt 6.5
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
  enable_language(OBJCXX)
  set(frontend_platform "${CMAKE_SOURCE_DIR}/src/platform_mac.mm")
elseif(WIN32)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
  set(frontend_platform "${CMAKE_SOURCE_DIR}/src/platform_windows.cpp")
else()
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
  set(frontend_platform "${CMAKE_SOURCE_DIR}/src/platform_linux.cpp")
endif()
find_package(SDL3 3.2 REQUIRED CONFIG)

add_library(bf2_setup_frontend STATIC
  "${CMAKE_SOURCE_DIR}/src/window_qt.cpp" "${CMAKE_SOURCE_DIR}/src/client_qt.cpp" "${CMAKE_SOURCE_DIR}/src/microphone_sdl.cpp" "${frontend_platform}")
target_link_libraries(bf2_setup_frontend PUBLIC bf2_setup_session bf2_setup_boost Qt6::Widgets Qt6::Network SDL3::SDL3)
target_compile_options(bf2_setup_frontend PRIVATE $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Wall -Wextra -Wpedantic> $<$<CXX_COMPILER_ID:MSVC>:/W3>)
if(APPLE)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/platform_mac.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_target_properties(bf2_setup_frontend PROPERTIES OBJCXX_STANDARD 23)
  target_link_libraries(bf2_setup_frontend PUBLIC "-framework AppKit" "-framework CoreGraphics")
endif()
set(frontend_application_sources
  "${CMAKE_SOURCE_DIR}/src/main_qt.cpp" "${CMAKE_SOURCE_DIR}/src/setup.cpp" "${CMAKE_SOURCE_DIR}/src/installer.cpp" "${CMAKE_SOURCE_DIR}/src/store.cpp")

if(BUILD_TESTING)
  # Real Qt controls and the real Setup coroutine against the fake machine (tests/fake.cpp)
  add_executable(bf2_setup_qt_workflow_checks "${CMAKE_SOURCE_DIR}/tests/linux_workflow.cpp" "${CMAKE_SOURCE_DIR}/tests/fake.cpp" "${CMAKE_SOURCE_DIR}/src/setup.cpp")
  target_compile_definitions(bf2_setup_qt_workflow_checks PRIVATE SETUP_REAL_WINDOW)
  target_include_directories(bf2_setup_qt_workflow_checks PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_link_libraries(bf2_setup_qt_workflow_checks PRIVATE bf2_setup_frontend)
  add_test(NAME qt_workflow_checks COMMAND bf2_setup_qt_workflow_checks --launcher)
  add_test(NAME qt_complete_workflow_checks COMMAND bf2_setup_qt_workflow_checks --launcher --complete)
  set_tests_properties(qt_workflow_checks qt_complete_workflow_checks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  if(WIN32)
    # the Qt DLLs and the offscreen plugin come straight from the Qt installation when the tests run
    get_filename_component(frontend_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set_tests_properties(qt_workflow_checks qt_complete_workflow_checks PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${frontend_qt_prefix}/bin;QT_PLUGIN_PATH=set:${frontend_qt_prefix}/plugins")
  endif()
endif()
