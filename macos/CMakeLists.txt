# The application is the shared Qt front end (cmake/frontend.cmake); this file makes it a bundle.
add_executable(OpenBF2 MACOSX_BUNDLE ${frontend_application_sources})
# the version Finder shows is the OpenBF2 release (the top-level CMakeLists.txt), the bundle version its numeric part
set_target_properties(OpenBF2 PROPERTIES OUTPUT_NAME "OpenBF2" MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${setup_version}" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
target_compile_definitions(OpenBF2 PRIVATE SETUP_UI_TEST_ROOT="${SETUP_SCRATCH}")
target_link_libraries(OpenBF2 PRIVATE bf2_setup_frontend)
set(content_manifests "${CMAKE_SOURCE_DIR}/core/content-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-deluxe-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-deluxe-base-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-complete-150.tsv")
set_source_files_properties(${content_manifests} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Setup)
target_sources(OpenBF2 PRIVATE ${content_manifests})
# The app icon: an original drawing (macos/icon/make-icon.swift regenerates it), no game artwork.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/OpenBF2.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(OpenBF2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/OpenBF2.icns")
# Qt's frameworks and plugins go into the bundle (macdeployqt from the same Qt installation), then
# everything is signed. Client artifacts are supplied by a separate engine build, never fetched here.
find_program(SETUP_MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH REQUIRED)
set(SETUP_CLIENT_ARTIFACT "" CACHE PATH "Local directory with bf2_client, libdice_py.dylib, libaidll.dylib (read-only)")
option(SETUP_CLIENT_ALLOW_THIN "Developer builds only: accept a client artifact built for the host architecture alone (DMG creation still rejects it)" OFF)
if(SETUP_CLIENT_ARTIFACT)
  set(client_thin 0)
  if(SETUP_CLIENT_ALLOW_THIN)
    set(client_thin 1)
  endif()
  add_custom_target(bf2_setup_client ALL
    COMMAND "${CMAKE_COMMAND}" -E env "SETUP_CLIENT_ALLOW_THIN=${client_thin}" /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/embed-client.sh" "${SETUP_CLIENT_ARTIFACT}" "$<TARGET_BUNDLE_DIR:OpenBF2>/Contents/MacOS"
    COMMAND "${SETUP_MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:OpenBF2>" -always-overwrite
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign-bundle.sh" "$<TARGET_BUNDLE_DIR:OpenBF2>"
    DEPENDS OpenBF2 VERBATIM)
else()
  add_custom_target(bf2_setup_client ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_BUNDLE_DIR:OpenBF2>/Contents/MacOS/bf2_client" "$<TARGET_BUNDLE_DIR:OpenBF2>/Contents/MacOS/libdice_py.dylib" "$<TARGET_BUNDLE_DIR:OpenBF2>/Contents/MacOS/libaidll.dylib" "$<TARGET_BUNDLE_DIR:OpenBF2>/Contents/Resources/Setup/client-artifact.sha256"
    COMMAND "${SETUP_MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:OpenBF2>" -always-overwrite
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign-bundle.sh" "$<TARGET_BUNDLE_DIR:OpenBF2>"
    DEPENDS OpenBF2 VERBATIM)
endif()
if(BUILD_TESTING)
  add_test(NAME client_packaging_checks COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/tests/client_packaging.sh" "${SETUP_SCRATCH}" "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME release_path_checks COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/tests/release_paths.sh" "${SETUP_SCRATCH}" "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/licences")
configure_file("${CMAKE_SOURCE_DIR}/third_party/rtptool/LICENSE" "licences/rtptool-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/THIRD-PARTY-NOTICES.txt" "licences/Rust-dependencies.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "licences/OpenBF2-Setup-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/docs/prototype-LICENSE" "licences/Prototype-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/Qt-NOTICE.txt" "licences/Qt-NOTICE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/SDL3-LICENSE.txt" "licences/SDL3-LICENSE.txt" COPYONLY)
file(GLOB licences "${CMAKE_CURRENT_BINARY_DIR}/licences/*.txt")
set_source_files_properties(${licences} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Setup/Licences)
target_sources(OpenBF2 PRIVATE ${licences})
