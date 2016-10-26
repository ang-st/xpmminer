cmake_minimum_required(VERSION 2.8)

option(STATIC_BUILD "Build with static libraries on Linux")

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_SOURCE_DIR}/cmake
)

include(FindDependencies)
find_package(Protobuf REQUIRED)

include_directories(
  ${GMP_INCLUDE_DIRECTORY}
  ${ZMQ_INCLUDE_DIRECTORY}
  ${CZMQ_INCLUDE_DIRECTORY}
  ${PROTOBUF_INCLUDE_DIRECTORY}
  ${OPENCL_INCLUDE_DIRECTORY}
  ${CMAKE_SOURCE_DIR}/config4cpp/include
  ${CMAKE_BINARY_DIR}
)

if (WIN32)
  message("Building for Win32")
  add_definitions(-D_WIN32 -D__WINDOWS__ -DLIBCZMQ_EXPORTS -D__USE_MINGW_ANSI_STDIO=0)
  if (InstallPrefix)
    include_directories(${InstallPrefix}/include)
  endif()
else()
  message("Building for Linux")
  add_definitions(-DLINUX)
  if (STATIC_BUILD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
  endif()
endif()

file(GLOB config4cppFiles "${CMAKE_SOURCE_DIR}/config4cpp/src/*.cpp")
add_library(config4cpp ${config4cppFiles})

PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${CMAKE_SOURCE_DIR}/protocol.proto)

set(LIBRARIES
  ${CZMQ_LIBRARY}
  ${ZMQ_LIBRARY}
  ${SODIUM_LIBRARY}
  ${GMP_LIBRARY}
  ${PROTOBUF_LIBRARY}
  ${OPENCL_LIBRARY}
  config4cpp
)

if (WIN32)
  set(LIBRARIES ${LIBRARIES} ws2_32)
else()
  set(LIBRARIES ${LIBRARIES} dl rt pthread)
endif()

# XPM GPU Client

add_executable(xpmclient
  adl.cpp
  baseclient.cpp
  prime.cpp
  primes.cpp
  sha256.cpp
  xpmclient.cpp
  benchmarks.cpp
  ${ProtoSources}
)

SET_TARGET_PROPERTIES(xpmclient PROPERTIES
  COMPILE_FLAGS "-std=c++0x"
)

target_link_libraries(xpmclient ${LIBRARIES})

if (STATIC_BUILD)
  set_target_properties(xpmclient PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

# ZCash CPU Client

add_executable(zcashcpuclient
  baseclient.cpp
  sha256.cpp
  zcashcpuclient.cpp
  zcash/equihash_original.cpp
  zcash/sha256.cpp
  ${ProtoSources}
)

SET_TARGET_PROPERTIES(zcashcpuclient PROPERTIES
  COMPILE_FLAGS "-std=c++0x"
)

target_link_libraries(zcashcpuclient ${LIBRARIES})

if (STATIC_BUILD)
  set_target_properties(zcashcpuclient PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

# ZCash GPU Client
add_definitions(-DXINTREE -DWN=200 -DWK=9 -DRESTBITS=4) 

add_executable(zcashgpuclient
  adl.cpp
  baseclient.cpp
  opencl.cpp
  sha256.cpp
  zcash/zcashgpuclient.cpp
  zcash/blake2b.cpp
  ${ProtoSources}
)

SET_TARGET_PROPERTIES(zcashgpuclient PROPERTIES
  COMPILE_FLAGS "-std=c++0x -msse -msse2 -msse3" 
)

target_link_libraries(zcashgpuclient ${LIBRARIES})

if (STATIC_BUILD)
  set_target_properties(zcashgpuclient PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()