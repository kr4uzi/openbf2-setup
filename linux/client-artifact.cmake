# Match macOS: an optional read-only client artifact, copied on every build.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(SETUP_CLIENT_ARTIFACT "" CACHE PATH "Local Linux engine artifact (bin/, lib/, share/; read-only)")
option(SETUP_CLIENT_ALLOW_DEVELOPMENT "Developer builds only: allow newer libc/CPU and debug paths without release metadata" OFF)
set(client_development)
if(SETUP_CLIENT_ALLOW_DEVELOPMENT)
  list(APPEND client_development --allow-development)
endif()
set(client_embed_script "${CMAKE_CURRENT_LIST_DIR}/embed-client.py")
add_custom_target(bf2_setup_client
  COMMAND "${Python3_EXECUTABLE}" "${client_embed_script}"
    --source "${SETUP_CLIENT_ARTIFACT}" --destination "${CMAKE_BINARY_DIR}" ${client_development}
  VERBATIM)
add_dependencies(OpenBF2 bf2_setup_client)
# cmake --install must apply the same selection, including clearing stale files
# after reconfiguring to a frontend-only build. Respect --prefix and DESTDIR.
configure_file("${CMAKE_CURRENT_LIST_DIR}/install-client.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/install-client.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-client.cmake")
