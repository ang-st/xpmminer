if (InstallPrefix)
  find_path(GMP_INCLUDE_DIRECTORY gmp.h gmpxx.h PATHS ${InstallPrefix}/include NO_DEFAULT_PATH)
  find_library(GMP_LIBRARY gmp PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)
  find_library(GMPXX_LIBRARY gmpxx PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)

  find_path(ZMQ_INCLUDE_DIRECTORY zmq.h PATHS ${InstallPrefix}/include NO_DEFAULT_PATH)
  if (WIN32)
    find_library(ZMQ_LIBRARY zmq.dll PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)
  else()
    find_library(ZMQ_LIBRARY zmq PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)
    find_library(SODIUM_LIBRARY sodium PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)
  endif()

  find_path(CZMQ_INCLUDE_DIRECTORY czmq.h PATHS ${InstallPrefix}/include NO_DEFAULT_PATH)
  find_library(CZMQ_LIBRARY czmq PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)

  find_path(PROTOBUF_INCLUDE_DIRECTORY google/protobuf/message.h PATHS ${InstallPrefix}/include NO_DEFAULT_PATH)
  find_library(PROTOBUF_LIBRARY protobuf PATHS ${InstallPrefix}/lib NO_DEFAULT_PATH)
else()
  find_path(GMP_INCLUDE_DIRECTORY gmp.h gmpxx.h)
  find_library(GMP_LIBRARY gmp)
  find_library(GMPXX_LIBRARY gmpxx)

  find_path(ZMQ_INCLUDE_DIRECTORY zmq.h)
  if (WIN32)
    find_library(ZMQ_LIBRARY zmq.dll)
  else()
    find_library(ZMQ_LIBRARY zmq)
    find_library(SODIUM_LIBRARY sodium)
  endif()

  find_path(CZMQ_INCLUDE_DIRECTORY czmq.h)
  find_library(CZMQ_LIBRARY czmq)

  find_path(PROTOBUF_INCLUDE_DIRECTORY google/protobuf/message.h)
  find_library(PROTOBUF_LIBRARY protobuf)
endif()

find_path(OPENCL_INCLUDE_DIRECTORY CL/cl.h
  PATHS /opt/AMDAPP/include NO_DEFAULT_PATH
)

find_library(OPENCL_LIBRARY OpenCL
  PATHS
    /opt/AMDAPP/lib/x86_64
    /opt/AMDAPPSDK-2.9/lib/x86_64
    /opt/AMDAPPSDK-3.0/lib/x86_64
  NO_DEFAULT_PATH
)
