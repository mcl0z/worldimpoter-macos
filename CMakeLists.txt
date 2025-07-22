cmake_minimum_required(VERSION 3.20.0)
project(WorldImporter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS specific settings
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    set(CMAKE_PREFIX_PATH "/opt/homebrew")

    # Add Homebrew paths
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)

    # For zlib (keg-only)
    include_directories(/opt/homebrew/opt/zlib/include)
    link_directories(/opt/homebrew/opt/zlib/lib)

    # For OpenMP (keg-only)
    include_directories(/opt/homebrew/opt/libomp/include)
    link_directories(/opt/homebrew/opt/libomp/lib)
endif()

# Find packages

# Find zlib
find_library(ZLIB_LIBRARY z REQUIRED)
if(NOT ZLIB_LIBRARY)
    message(FATAL_ERROR "zlib not found")
endif()

# Find libzip
find_library(LIBZIP_LIBRARY zip REQUIRED)
if(NOT LIBZIP_LIBRARY)
    message(FATAL_ERROR "libzip not found")
endif()

# Include directories
include_directories(WorldImporter/include)

# Source files
file(GLOB SOURCE_FILES "WorldImporter/*.cpp")
add_executable(WorldImporter ${SOURCE_FILES})

# Link libraries
target_link_libraries(WorldImporter PRIVATE ${LIBZIP_LIBRARY} ${ZLIB_LIBRARY})

# macOS specific linking
if(APPLE)
    target_link_libraries(WorldImporter PRIVATE "-framework Foundation" omp)
endif()
