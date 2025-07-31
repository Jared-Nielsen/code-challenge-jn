cmake_minimum_required(VERSION 3.10)
project(DocumentSigningServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(backend/include)

# Source files
set(SOURCES
    backend/src/main.cpp
)

# Create executable
add_executable(signing-server ${SOURCES})

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Define CPPHTTPLIB_OPENSSL_SUPPORT
add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)

# Link libraries
if(WIN32)
    target_link_libraries(signing-server ws2_32 OpenSSL::SSL OpenSSL::Crypto)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(signing-server ${CMAKE_THREAD_LIBS_INIT} OpenSSL::SSL OpenSSL::Crypto)
elseif(APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(signing-server ${CMAKE_THREAD_LIBS_INIT} OpenSSL::SSL OpenSSL::Crypto)
endif()

# Set output directory
set_target_properties(signing-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)