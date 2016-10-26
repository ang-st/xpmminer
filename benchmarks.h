#include "opencl.h"
#include "xpmclient.h"

void runBenchmarks(cl_context context,
                   cl_program program,
                   cl_device_id deviceId,
                   unsigned primorial,
                   unsigned depth,
                   unsigned defaultGroupSize);
