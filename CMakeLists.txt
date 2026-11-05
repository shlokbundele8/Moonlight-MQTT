# Moonlight MQTT - desktop and Raspberry Pi build.
# This builds the library. Microcontroller builds use the Arduino IDE or
# PlatformIO instead and do not need CMake.
cmake_minimum_required(VERSION 3.10)
project(MoonlightMQTT CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# The library: one source file plus the headers under src/.
add_library(moonlightmqtt STATIC src/moonlight/Client.cpp)
target_include_directories(moonlightmqtt PUBLIC src)

# TLS support (OpenSSL) for the desktop and Raspberry Pi transport. Programs
# that include MoonlightMQTT.h on these systems link OpenSSL through this target.
find_package(OpenSSL REQUIRED)
target_link_libraries(moonlightmqtt PUBLIC OpenSSL::SSL OpenSSL::Crypto)
