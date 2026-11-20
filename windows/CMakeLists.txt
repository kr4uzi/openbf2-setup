# OpenBF2.exe: the shared Qt front end (cmake/frontend.cmake) as a Windows application. The build tree's
# app\ folder is the application as it is shipped: OpenBF2.exe, the Qt DLLs and plugins (windeployqt),
# setup\ with the content manifests, licences\, and the client packages x64\ and x86\ when given.
set(app_dir "${CMAKE_BINARY_DIR}/app")
add_executable(OpenBF2 WIN32 ${frontend_application_sources} OpenBF2.rc OpenBF2.manifest)
target_link_libraries(OpenBF2 PRIVATE bf2_setup_frontend)
target_compile_definitions(OpenBF2 PRIVATE SETUP_UI_TEST_ROOT="${SETUP_SCRATCH}")
set_target_properties(OpenBF2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${app_dir}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${app_dir}")
set_source_files_properties(OpenBF2.rc PROPERTIES COMPILE_DEFINITIONS
  "SETUP_VERSION_COMMAS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0;SETUP_VERSION_STRING=\"${PROJECT_VERSION}\"")

file(MAKE_DIRECTORY "${app_dir}/setup" "${app_dir}/licences")
file(COPY "${CMAKE_SOURCE_DIR}/core/content-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-deluxe-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-deluxe-base-150.tsv" "${CMAKE_SOURCE_DIR}/core/content-complete-150.tsv" DESTINATION "${app_dir}/setup")
configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${app_dir}/licences/OpenBF2-Setup-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/rtptool/LICENSE" "${app_dir}/licences/rtptool-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/THIRD-PARTY-NOTICES.txt" "${app_dir}/licences/Rust-dependencies.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/Qt-NOTICE.txt" "${app_dir}/licences/Qt-NOTICE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/SDL3-LICENSE.txt" "${app_dir}/licences/SDL3-LICENSE.txt" COPYONLY)

# Qt's DLLs and plugins next to OpenBF2.exe, from the same Qt installation. The app downloads provider files over
# https (the tls plugin stays) but uses no Direct3D shader compilation and no network information
find_program(SETUP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET OpenBF2 POST_BUILD
  COMMAND "${SETUP_WINDEPLOYQT}" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime
    --skip-plugin-types networkinformation "$<TARGET_FILE:OpenBF2>"
  VERBATIM)

# openbf2-hubtool.exe, built by ci/build-hubtool.sh from the commit pinned in ci/versions.env: the BF2Hub provider file
# from the user's own BF2Hub installer. Optional; without it the launcher's BF2Hub button says the build has none
set(SETUP_HUBTOOL "" CACHE FILEPATH "openbf2-hubtool.exe (ci/build-hubtool.sh)")
if(SETUP_HUBTOOL)
  if(NOT EXISTS "${SETUP_HUBTOOL}")
    message(FATAL_ERROR "SETUP_HUBTOOL: ${SETUP_HUBTOOL} does not exist")
  endif()
  get_filename_component(hubtool_dir "${SETUP_HUBTOOL}" DIRECTORY)
  add_custom_target(bf2_setup_hubtool ALL
    COMMAND "${CMAKE_COMMAND}" -E copy "${SETUP_HUBTOOL}" "${app_dir}/openbf2-hubtool.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy "${hubtool_dir}/openbf2-hubtool-LICENSE.txt" "${app_dir}/licences/openbf2-hubtool-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${hubtool_dir}/LZMA-SDK-NOTICE.txt" "${app_dir}/licences/LZMA-SDK-NOTICE.txt"
    VERBATIM)
  add_dependencies(bf2_setup_hubtool OpenBF2)
endif()

# The client packages are supplied by the engine's build, never fetched here: the x64 one (BF2.exe with its
# DLLs and BF2OpenAL.dll) and the x86 one for PunkBuster servers. Each folder is copied as it is.
set(SETUP_CLIENT_X64 "" CACHE PATH "Folder with the x64 client package (BF2.exe and its DLLs)")
set(SETUP_CLIENT_X86 "" CACHE PATH "Folder with the x86 client package (BF2.exe and its DLLs)")
foreach(arch IN ITEMS x64 x86)
  string(TOUPPER "${arch}" option)
  set(client "${SETUP_CLIENT_${option}}")
  if(client)
    # the files the launcher requires (windowsClientLayout in src/launch.cpp)
    foreach(file IN ITEMS BF2.exe dice_py.dll RendDX9.dll SwiffPlayer.dll BF2Audio.dll)
      if(NOT EXISTS "${client}/${file}")
        message(FATAL_ERROR "SETUP_CLIENT_${option}: ${file} is missing in ${client}")
      endif()
    endforeach()
    add_custom_target(bf2_setup_client_${arch} ALL
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${app_dir}/${arch}"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${client}" "${app_dir}/${arch}"
      VERBATIM)
    add_dependencies(bf2_setup_client_${arch} OpenBF2)
  endif()
endforeach()
