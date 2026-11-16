# Configure inside the pinned Ubuntu 22.04 SDK. Nothing is fetched by CMake.
set(CMAKE_C_COMPILER gcc-15 CACHE FILEPATH "Baseline C compiler")
set(CMAKE_CXX_COMPILER g++-15 CACHE FILEPATH "Baseline C++ compiler")
set(CMAKE_C_FLAGS_INIT "-march=x86-64 -mtune=generic")
set(CMAKE_CXX_FLAGS_INIT "-march=x86-64 -mtune=generic")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
