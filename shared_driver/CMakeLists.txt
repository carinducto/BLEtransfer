cmake_minimum_required(VERSION 3.15)
project(PSoCDriver VERSION 1.0.0 LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find zlib (required for compression)
find_package(ZLIB REQUIRED)

# Source files
set(SOURCES
    src/psoc_driver.cpp
    src/crc32.cpp
    src/compression.cpp
    src/transfer_session.cpp
)

# Header files (for IDE organization)
set(HEADERS
    include/psoc_driver/psoc_driver.h
    include/psoc_driver/protocol.h
    include/psoc_driver/data_types.h
    include/psoc_driver/crc32.h
    include/psoc_driver/compression.h
    include/psoc_driver/transfer_session.h
)

# Create static library
add_library(psoc_driver STATIC ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(psoc_driver
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link libraries
target_link_libraries(psoc_driver
    PUBLIC
        ZLIB::ZLIB
)

# Platform-specific settings
if(APPLE)
    set_target_properties(psoc_driver PROPERTIES
        MACOSX_RPATH ON
    )
endif()

# Installation rules
install(TARGETS psoc_driver
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Build info
message(STATUS "PSoC Driver Library Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
