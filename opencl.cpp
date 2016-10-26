#include "opencl.h"
#include <fstream>
#include <vector>
#include <memory>
#include <stdio.h>

extern cl_program gProgram;

bool clInitialize(const char *requiredPlatform, std::vector<cl_device_id> &gpus)
{
  cl_platform_id platforms[64];
  cl_uint numPlatforms;
  OCLR(clGetPlatformIDs(sizeof(platforms)/sizeof(cl_platform_id), platforms, &numPlatforms), false);
  if (!numPlatforms) {
    printf("<error> no OpenCL platforms found\n");
    return false;
  }
  
  int platformIdx = -1;
  if (requiredPlatform) {
    for (decltype(numPlatforms) i = 0; i < numPlatforms; i++) {
      char name[1024] = {0};
      OCLR(clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, 0), false);
      printf("found platform[%i] name = '%s'\n", (int)i, name);
      if (strcmp(name, requiredPlatform) == 0) {
        platformIdx = i;
        break;
      }
    }
  } else {
    platformIdx = 0;
  }
  
  
  if (platformIdx == -1) {
    printf("<error> platform %s not exists\n", requiredPlatform);
    return false;
  }
  
  auto platform = platforms[platformIdx];
  
  cl_uint numDevices = 0;
  cl_device_id devices[64];
  clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, sizeof(devices)/sizeof(cl_device_id), devices, &numDevices);
  if (numDevices) {
    printf("<info> found %d devices\n", numDevices);
  } else {
    printf("<error> no OpenCL GPU devices found.\n");
    return false;
  }

  for (decltype(numDevices) i = 0; i < numDevices; i++) {
    gpus.push_back(devices[i]);
  }
 
  {
    cl_context_properties props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    cl_int error;
    gContext = clCreateContext(props, gpus.size(), &gpus[0], 0, 0, &error);
    OCLR(error, false);
  }
  
  return true;
}

bool clCompileKernel(const std::vector<cl_device_id> &gpus,
                     const char *binaryName,
                     const std::vector<const char*> &sources,
                     const char *arguments,
                     std::vector<cl_int> &binstatus)
{
  std::ifstream testfile(binaryName);
  if(!testfile) {
    printf("<info> compiling ...\n");
    
    std::string sourceFile;
    for (auto &i: sources) {
      std::ifstream stream(i);
      std::string str((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      sourceFile.append(str);
    }
    
    printf("<info> source: %u bytes\n", (unsigned)sourceFile.size());
    if(sourceFile.size() < 1){
      fprintf(stderr, "<error> source files not found or empty\n");
      return false;
    }
    
    cl_int error;
    const char *sources[] = { sourceFile.c_str(), 0 };
    gProgram = clCreateProgramWithSource(gContext, 1, sources, 0, &error);
    OCLR(error, false);
    
    if (clBuildProgram(gProgram, gpus.size(), &gpus[0], arguments, 0, 0) != CL_SUCCESS) {    
      size_t logSize;
      clGetProgramBuildInfo(gProgram, gpus[0], CL_PROGRAM_BUILD_LOG, 0, 0, &logSize);
      
      std::unique_ptr<char[]> log(new char[logSize]);
      clGetProgramBuildInfo(gProgram, gpus[0], CL_PROGRAM_BUILD_LOG, logSize, log.get(), 0);
      printf("%s\n", log.get());

      return false;
    }
    
    size_t binsizes[64];
    OCLR(clGetProgramInfo(gProgram, CL_PROGRAM_BINARY_SIZES, sizeof(binsizes), binsizes, 0), false);
    size_t binsize = binsizes[0];
    if(!binsize){
      printf("<error> no binary available!\n");
      return false;
    }
    
    printf("<info> binsize = %u bytes\n", (unsigned)binsize);
    std::unique_ptr<unsigned char[]> binary(new unsigned char[binsize+1]);
    unsigned char *binaries[64];
    for (auto &b: binaries)
      b = binary.get();
    OCLR(clGetProgramInfo(gProgram, CL_PROGRAM_BINARIES, sizeof(binaries), binaries, 0), false);
    
    {
      std::ofstream bin(binaryName, std::ofstream::binary | std::ofstream::trunc);
      bin.write((const char*)binary.get(), binsize);
      bin.close();      
    }
   
    OCLR(clReleaseProgram(gProgram), false);
  }
  
  std::ifstream bfile(binaryName, std::ifstream::binary);
  if(!bfile) {
    printf("<error> %s not found\n", binaryName);
    return false;
  }  
  
  bfile.seekg(0, bfile.end);
  int binsize = bfile.tellg();
  bfile.seekg(0, bfile.beg);
  if(!binsize){
    printf("<error> %s empty\n", binaryName);
    return false;
  }
  
  std::vector<char> binary(binsize+1);
  bfile.read(&binary[0], binsize);
  bfile.close();
  
  cl_int error;
  binstatus.resize(gpus.size(), 0);
  std::vector<size_t> binsizes(gpus.size(), binsize);
  std::vector<const unsigned char*> binaries(gpus.size(), (const unsigned char*)&binary[0]);
  gProgram = clCreateProgramWithBinary(gContext, gpus.size(), &gpus[0], &binsizes[0], &binaries[0], &binstatus[0], &error);
  OCLR(error, false);
  OCLR(clBuildProgram(gProgram, gpus.size(), &gpus[0], 0, 0, 0), false);  
  return true;
}
