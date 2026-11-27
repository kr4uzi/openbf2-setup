# OpenBF2.exe: the shared Qt front end (cmake/frontend.cmake) as a Windows application. The build tree's
# app\ folder is the application as it is shipped: OpenBF2.exe, the Qt DLLs and plugins (windeployqt),
# setup\ with the content manifests, licences\, and the client packages x64\ and x86\ when given.
set(app_dir "${CMAKE_BINARY_DIR}/app")
add_executable(OpenBF2 WIN32 ${frontend_application_sources} OpenBF2.rc OpenBF2.manifest)
target_link_libraries(OpenBF2 PRIVATE bf2_setup_frontend)
target_compile_definitions(OpenBF2 PRIVATE SETUP_UI_TEST_ROOT="${SETUP_SCRATCH}")
set_target_properties(OpenBF2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${app_dir}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${app_dir}")
# The version Windows shows for OpenBF2 (Settings > Apps, the installer, the product version of OpenBF2.exe): the
# OpenBF2 release whose clients the build embeds, such as 0.3.0-rc2 (the engine's tag without its v). Without one, the
# setup's own version. The file version of OpenBF2.exe stays the setup's.
set(SETUP_PRODUCT_VERSION "" CACHE STRING "OpenBF2 release shown by Windows, such as 0.3.0-rc2; empty for the setup's version")
if(SETUP_PRODUCT_VERSION)
  set(product_version "${SETUP_PRODUCT_VERSION}")
else()
  set(product_version "${PROJECT_VERSION}")
endif()
if(NOT product_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "SETUP_PRODUCT_VERSION must look like 0.3.0 or 0.3.0-rc2: ${product_version}")
endif()
set(product_version_numeric "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set_source_files_properties(OpenBF2.rc PROPERTIES COMPILE_DEFINITIONS
  "SETUP_VERSION_COMMAS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0;SETUP_VERSION_STRING=\"${PROJECT_VERSION}\";SETUP_PRODUCT_COMMAS=${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0;SETUP_PRODUCT_STRING=\"${product_version}\"")

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

# The Visual C++ runtime beside OpenBF2.exe, app-local as in the client packages: a clean Windows has no
# MSVCP140.dll. The DLLs come from this compiler's redistributable folder (Microsoft's Distributable Code).
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
include(InstallRequiredSystemLibraries)
if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
  message(FATAL_ERROR "The Visual C++ runtime DLLs of this compiler were not found")
endif()
add_custom_command(TARGET OpenBF2 POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "${app_dir}"
  VERBATIM)

# directx\: Microsoft's DirectX setup with the cab of the 64-bit d3dx9_25.dll, from the DirectX June 2010 redist
# (ci/fetch-dependencies.sh). The launcher runs it, with the user's consent, only when the 64-bit client finds no
# d3dx9_25.dll and the disc did not supply one (src/directx.h). Each file is taken only with its known SHA-256.
set(SETUP_DIRECTX_REDIST "" CACHE PATH "Folder with DXSETUP's files from Microsoft's DirectX June 2010 redist")
if(SETUP_DIRECTX_REDIST)
  foreach(entry IN ITEMS
      "DXSETUP.exe=8f47d7121ef6532ad9ad9901e44e237f5c30448b752028c58a9d19521414e40d"
      "DSETUP.dll=2a61679eeedabf7d0d0ac14e5447486575622d6b7cfa56f136c1576ff96da21f"
      "dsetup32.dll=fb0e534f9b0926e518f1c2980640dfd29f14217cdfa37cf3a0c13349127ed9a8"
      "dxupdate.cab=e18a5404b612e88fa8b403c9b33f064c0a89528db7ef9a79aa116908d0e6afed"
      "Apr2005_d3dx9_25_x64.cab=b4188ce988af9c4771ae0abcc7edc42a091133f9f20196564f51755dc55ea85d")
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 file)
    list(GET pair 1 expected)
    file(SHA256 "${SETUP_DIRECTX_REDIST}/${file}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "SETUP_DIRECTX_REDIST: ${file} is not the file of the DirectX June 2010 redist")
    endif()
    configure_file("${SETUP_DIRECTX_REDIST}/${file}" "${app_dir}/directx/${file}" COPYONLY)
  endforeach()
  configure_file("${CMAKE_SOURCE_DIR}/third_party/DirectX-NOTICE.txt" "${app_dir}/licences/DirectX-NOTICE.txt" COPYONLY)
endif()

# The installer, openbf2-windows-setup.exe: Inno Setup (OpenBF2.iss) packs the app folder as it is
set(SETUP_ISCC "" CACHE FILEPATH "Inno Setup's ISCC.exe; with it, the installer target builds openbf2-windows-setup.exe")
# SETUP_SIGN: Inno signs the installer and its uninstaller with sign.cmd (sign.ps1 and its environment)
option(SETUP_SIGN "Sign the installer and the uninstaller (windows/sign.ps1)" OFF)
if(SETUP_ISCC)
  set(sign_options)
  if(SETUP_SIGN)
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sign.cmd" sign_command)
    set(sign_options "/DSign" "/Sopenbf2=$q${sign_command}$q $f")
  endif()
  add_custom_target(installer
    COMMAND "${SETUP_ISCC}" /Q "/DAppVersion=${product_version}" "/DVersionInfo=${product_version_numeric}" "/DSourceDir=${app_dir}"
      "/DOutputDir=${CMAKE_BINARY_DIR}/installer" "/DOutputBaseFilename=openbf2-windows-setup" ${sign_options}
      "${CMAKE_CURRENT_SOURCE_DIR}/OpenBF2.iss"
    VERBATIM)
  add_dependencies(installer OpenBF2)
  foreach(arch IN ITEMS x64 x86)
    if(TARGET bf2_setup_client_${arch})
      add_dependencies(installer bf2_setup_client_${arch})
    endif()
  endforeach()
endif()
