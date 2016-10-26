#include "benchmarks.h"

#include "gmpxx.h"

#include <time.h>
#include <chrono>
#include <memory>
#if defined(__GXX_EXPERIMENTAL_CXX0X__) && (__cplusplus < 201103L)
#define steady_clock monotonic_clock
#endif  

#include "prime.h"
 
#include <set>
 
enum OpenCLKernels {
  CLKernelGenConfig = 0,
  CLKernelSquareBenchmark320,
  CLKernelSquareBenchmark352,  
  CLKernelMultiplyBenchmark320,
  CLKernelMultiplyBenchmark352,
  CLKernelFermatTestBenchmark320,
  CLKernelFermatTestBenchmark352,
  CLKernelHashMod,
  CLKernelSieveSetup,
  CLKernelSieve,
  CLKernelSieveSearch,
  CLKernelsNum
};  
 
static const char *gOpenCLKernelNames[] = {
  "getconfig",
  "squareBenchmark320",
  "squareBenchmark352",
  "multiplyBenchmark320",
  "multiplyBenchmark352",
  "fermatTestBenchMark320",
  "fermatTestBenchMark352",
  "bhashmodUsePrecalc",
  "setup_sieve",
  "sieve",
  "s_sieve"
};

const unsigned GroupSize = 256;
const unsigned MulOpsNum = 512; 

uint32_t rand32()
{
  uint32_t result = rand();
  result = (result << 16) | rand();
  return result;
}

uint64_t rand64()
{
  uint64_t result = rand();
  result = (result << 16) | rand();
  result = (result << 16) | rand();
  result = (result << 16);
  return result;
} 
 
bool trialDivisionChainTest(uint32_t *primes,
                            mpz_class &N,
                            bool fSophieGermain,
                            unsigned chainLength,
                            unsigned depth)
{
  N += (fSophieGermain ? -1 : 1);
  for (unsigned i = 0; i < chainLength; i++) {
    for (unsigned divIdx = 0; divIdx < depth; divIdx += 16) { 
      if (mpz_tdiv_ui(N.get_mpz_t(), primes[divIdx]) == 0) {
        return false;
      }
    }
     
    N <<= 1;
    N += (fSophieGermain ? 1 : -1);
  }
  
  return true;
} 

bool sieveResultsTest(uint32_t *primes,
                      mpz_class &fixedMultiplier,
                      const uint8_t *cunningham1,
                      const uint8_t *cunningham2,
                      unsigned sieveSize,
                      unsigned chainLength,
                      unsigned depth,
                      unsigned extensionsNum,
                      std::set<mpz_class> &candidates,
                      unsigned *invalidCount)
{
  const uint32_t layersNum = chainLength + extensionsNum;
  const uint32_t *c1ptr = (const uint32_t*)cunningham1;  
  const uint32_t *c2ptr = (const uint32_t*)cunningham2;    
  unsigned sieveWords = sieveSize/32;
   
  for (unsigned wordIdx = 0; wordIdx < sieveWords; wordIdx++) {
    uint32_t c1Data[layersNum];
    uint32_t c2Data[layersNum];
     
    for (unsigned i = 0; i < layersNum; i++)
      c1Data[i] = c1ptr[wordIdx + sieveWords*i];
     
    for (unsigned firstLayer = 0; firstLayer <= layersNum-chainLength; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength; layer++)
        mask |= c1Data[firstLayer + layer];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            if (!trialDivisionChainTest(primes, chainOrigin, true, chainLength, depth))
              ++*invalidCount;
            
            candidates.insert(candidateMultiplier);
          }
        }
      }
    }

    for (unsigned i = 0; i < layersNum; i++)
      c2Data[i] = c2ptr[wordIdx + sieveWords*i];
     
    for (unsigned firstLayer = 0; firstLayer <= layersNum-chainLength; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength; layer++)
        mask |= c2Data[firstLayer + layer];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            if (!trialDivisionChainTest(primes, chainOrigin, false, chainLength, depth))
              ++*invalidCount;
            
            candidates.insert(candidateMultiplier);            
          }
        }
      }
    } 
 
    for (unsigned firstLayer = 0; firstLayer <= layersNum-chainLength/2; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength/2; layer++)
        mask |= c1Data[firstLayer + layer] | c2Data[firstLayer + layer];
      if (chainLength&0x1 && (firstLayer+chainLength/2) < layersNum)
        mask |= c1Data[firstLayer + chainLength/2];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            mpz_class chainOriginExtra = chainOrigin;            
            if (!trialDivisionChainTest(primes, chainOrigin, true, (chainLength+1)/2, depth) ||
                !trialDivisionChainTest(primes, chainOriginExtra, false, chainLength/2, depth))
              ++*invalidCount;
            candidates.insert(candidateMultiplier);            
          }
        }
      }
    }    
  }
   
  return true;
   
} 
 
void multiplyBenchmark(cl_command_queue queue,
                       cl_kernel *kernels,
                       unsigned groupsNum,                       
                       unsigned mulOperandSize,
                       uint32_t elementsNum,
                       bool isSquaring)
{
  unsigned gmpOpSize = mulOperandSize + (mulOperandSize%2);
  unsigned limbsNum = elementsNum*gmpOpSize;
  clBuffer<uint32_t> m1;
  clBuffer<uint32_t> m2;
  clBuffer<uint32_t> mR;
  clBuffer<uint32_t> cpuR;
  
  m1.init(limbsNum, CL_MEM_READ_WRITE);
  m2.init(limbsNum, CL_MEM_READ_WRITE);
  mR.init(limbsNum*2, CL_MEM_READ_WRITE);
  cpuR.init(limbsNum*2, CL_MEM_READ_WRITE);

  memset(&m1.get(0), 0, limbsNum*sizeof(uint32_t));
  memset(&m2.get(0), 0, limbsNum*sizeof(uint32_t));
  memset(&mR.get(0), 0, 2*limbsNum*sizeof(uint32_t));
  memset(&cpuR.get(0), 0, 2*limbsNum*sizeof(uint32_t));  
  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < mulOperandSize; j++) {
      m1[i*gmpOpSize + j] = rand32();
      m2[i*gmpOpSize + j] = rand32();
    }
  }

  m1.copyToDevice(queue);
  m2.copyToDevice(queue);

  cl_kernel kernel;
  if (isSquaring) {
    if (mulOperandSize == 320/32) {
      kernel = kernels[CLKernelSquareBenchmark320];
    } else if (mulOperandSize == 352/32) {
      kernel = kernels[CLKernelSquareBenchmark352];
    } else {
      fprintf(stderr, "Can't multiply %u-size operands on OpenCL device\n", mulOperandSize*32);
      return;
    }
  } else {
    if (mulOperandSize == 320/32) {
      kernel = kernels[CLKernelMultiplyBenchmark320];
    } else if (mulOperandSize == 352/32) {
      kernel = kernels[CLKernelMultiplyBenchmark352];
    } else {
      fprintf(stderr, "Can't multiply %u-size operands on OpenCL device\n", mulOperandSize*32);
      return;
    }
  }

  
  if (isSquaring) {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(elementsNum), &elementsNum);
  } else {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &m2.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);
  }
  
  std::unique_ptr<mpz_class[]> cpuM1(new mpz_class[elementsNum]);
  std::unique_ptr<mpz_class[]> cpuM2(new mpz_class[elementsNum]);
  std::unique_ptr<mpz_class[]> cpuResult(new mpz_class[elementsNum]);
  
  for (unsigned i = 0; i < elementsNum; i++) {
    mpz_import(cpuM1[i].get_mpz_t(), mulOperandSize, -1, 4, 0, 0, &m1[i*gmpOpSize]);
    mpz_import(cpuM2[i].get_mpz_t(), mulOperandSize, -1, 4, 0, 0, &m2[i*gmpOpSize]);
    mpz_import(cpuResult[i].get_mpz_t(), mulOperandSize*2, -1, 4, 0, 0, &mR[i*mulOperandSize*2]);
  }

  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();  
  
  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      fprintf(stderr, "clEnqueueNDRangeKernel error!\n");
      return;
    }

    if (clWaitForEvents(1, &event) != CL_SUCCESS) {
      fprintf(stderr, "clWaitForEvents error!\n");
      return;
    }
    
    clReleaseEvent(event);
  }
  
  auto gpuEnd = std::chrono::steady_clock::now();  
  
  if (isSquaring) {
    for (unsigned i = 0; i < elementsNum; i++) {
      unsigned gmpLimbsNum = cpuM1[i].get_mpz_t()->_mp_size;
      mp_limb_t *Operand1 = cpuM1[i].get_mpz_t()->_mp_d;
      uint32_t *target = &cpuR[i*mulOperandSize*2];
      for (unsigned j = 0; j < MulOpsNum; j++) {
        mpn_sqr((mp_limb_t*)target, Operand1, gmpLimbsNum);
        memcpy(Operand1, target+mulOperandSize, mulOperandSize*sizeof(uint32_t));
      }
    }
  } else {
    for (unsigned i = 0; i < elementsNum; i++) {
      unsigned gmpLimbsNum = cpuM1[i].get_mpz_t()->_mp_size;
      mp_limb_t *Operand1 = cpuM1[i].get_mpz_t()->_mp_d;
      mp_limb_t *Operand2 = cpuM2[i].get_mpz_t()->_mp_d;
      uint32_t *target = &cpuR[i*mulOperandSize*2];
      for (unsigned j = 0; j < MulOpsNum; j++) {
        mpn_mul_n((mp_limb_t*)target, Operand1, Operand2, gmpLimbsNum);
        memcpy(Operand1, target+mulOperandSize, mulOperandSize*sizeof(uint32_t));
      }
    }
  }

  mR.copyToHost(queue);
  clFinish(queue);

  for (unsigned i = 0; i < elementsNum; i++) {
    if (memcmp(&mR[i*mulOperandSize*2], &cpuR[i*mulOperandSize*2], 4*mulOperandSize*2) != 0) {
      fprintf(stderr, "element index: %u\n", i);
      fprintf(stderr, "gmp: ");
      for (unsigned j = 0; j < mulOperandSize*2; j++)
        fprintf(stderr, "%08X ", cpuR[i*mulOperandSize*2 + j]);
      fprintf(stderr, "\ngpu: ");
      for (unsigned j = 0; j < mulOperandSize*2; j++)
        fprintf(stderr, "%08X ", mR[i*mulOperandSize*2 + j]);
      fprintf(stderr, "\n");
      fprintf(stderr, "results differ!\n");
      break;
    }
  }

  double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;  
  double opsNum = ((elementsNum*MulOpsNum) / 1000000.0) / gpuTime * 1000.0;
  
  printf("%s %u bits: %.3lfms (%.3lfM ops/sec)\n", (isSquaring ? "square" : "multiply"), mulOperandSize*32, gpuTime, opsNum);
}


void fermatTestBenchmark(cl_command_queue queue,
                         cl_kernel *kernels,
                         unsigned groupsNum, 
                         unsigned operandSize,
                         unsigned elementsNum)
{ 
  unsigned numberLimbsNum = elementsNum*operandSize;
  
  clBuffer<uint32_t> numbers;
  clBuffer<uint32_t> gpuResults;
  clBuffer<uint32_t> cpuResults;
  
  numbers.init(numberLimbsNum, CL_MEM_READ_WRITE);
  gpuResults.init(numberLimbsNum, CL_MEM_READ_WRITE);
  cpuResults.init(numberLimbsNum, CL_MEM_READ_WRITE);
  
  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < operandSize; j++)
      numbers[i*operandSize + j] = (j == operandSize-1) ? (1 << (i % 32)) : rand32();
    numbers[i*operandSize] |= 0x1; 
  }

  numbers.copyToDevice(queue);
  gpuResults.copyToDevice(queue);

  cl_kernel kernel;
  if (operandSize == 320/32) {
    kernel = kernels[CLKernelFermatTestBenchmark320];
  } else if (operandSize == 352/32) {
    kernel = kernels[CLKernelFermatTestBenchmark352];
  } else {
    fprintf(stderr, "Can't do Fermat test on %ubit operand\n", operandSize*32);
    return;
  }
  
  clSetKernelArg(kernel, 0, sizeof(cl_mem), &numbers.DeviceData);
  clSetKernelArg(kernel, 1, sizeof(cl_mem), &gpuResults.DeviceData);
  clSetKernelArg(kernel, 2, sizeof(elementsNum), &elementsNum);
  
  std::unique_ptr<mpz_t[]> cpuNumbersBuffer(new mpz_t[elementsNum]);
  std::unique_ptr<mpz_t[]> cpuResultsBuffer(new mpz_t[elementsNum]);
  mpz_class mpzTwo = 2;
  mpz_class mpzE;
  mpz_import(mpzE.get_mpz_t(), operandSize, -1, 4, 0, 0, &numbers[0]);
  for (unsigned i = 0; i < elementsNum; i++) {
    mpz_init(cpuNumbersBuffer[i]);
    mpz_init(cpuResultsBuffer[i]);
    mpz_import(cpuNumbersBuffer[i], operandSize, -1, 4, 0, 0, &numbers[i*operandSize]);
    mpz_import(cpuResultsBuffer[i], operandSize, -1, 4, 0, 0, &cpuResults[i*operandSize]);
  }
  
  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();  

  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      fprintf(stderr, "clEnqueueNDRangeKernel error!\n");
      return;
    }
      
    cl_int error;
    if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
      fprintf(stderr, "clWaitForEvents error %i!\n", error);
      return;
    }
      
    clReleaseEvent(event);
  }
  
  auto gpuEnd = std::chrono::steady_clock::now();  
  
  
  for (unsigned i = 0; i < elementsNum; i++) {
    mpz_sub_ui(mpzE.get_mpz_t(), cpuNumbersBuffer[i], 1);
    mpz_powm(cpuResultsBuffer[i], mpzTwo.get_mpz_t(), mpzE.get_mpz_t(), cpuNumbersBuffer[i]);
  }

  gpuResults.copyToHost(queue);
  clFinish(queue);
  
  memset(&cpuResults[0], 0, 4*operandSize*elementsNum);
  for (unsigned i = 0; i < elementsNum; i++) {
    size_t exportedLimbs;
    mpz_export(&cpuResults[i*operandSize], &exportedLimbs, -1, 4, 0, 0, cpuResultsBuffer[i]);
    if (memcmp(&gpuResults[i*operandSize], &cpuResults[i*operandSize], 4*operandSize) != 0) {
      fprintf(stderr, "element index: %u\n", i);
      fprintf(stderr, "gmp: ");
      for (unsigned j = 0; j < operandSize; j++)
        fprintf(stderr, "%08X ", cpuResults[i*operandSize + j]);
      fprintf(stderr, "\ngpu: ");
      for (unsigned j = 0; j < operandSize; j++)
        fprintf(stderr, "%08X ", gpuResults[i*operandSize + j]);
      fprintf(stderr, "\n");
      fprintf(stderr, "results differ!\n");
      break;
    }
  }
  
  double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;  
  double opsNum = ((elementsNum) / 1000000.0) / gpuTime * 1000.0;
  
  printf("%s %u bits: %.3lfms (%.3lfM ops/sec)\n", "Fermat tests", operandSize*32, gpuTime, opsNum);
}


void hashmodBenchmark(cl_command_queue queue,
                      cl_kernel *kernels,
                      unsigned defaultGroupSize,
                      unsigned groupsNum,
                      mpz_class *allPrimorials,
                      unsigned mPrimorial)
{
  printf("\n *** hashmod benchmark ***\n");  
  
  const unsigned iterationsNum = 64;
  cl_kernel mHashMod = kernels[CLKernelHashMod];
  
  PrimeMiner::search_t hashmod;
  PrimeMiner::block_t blockheader;
  
  hashmod.midstate.init(8*sizeof(cl_uint), CL_MEM_READ_ONLY);
  hashmod.found.init(32768, CL_MEM_READ_WRITE);
  hashmod.primorialBitField.init(2048, CL_MEM_READ_WRITE);
  hashmod.count.init(1, CL_MEM_READ_WRITE);

  clSetKernelArg(mHashMod, 0, sizeof(cl_mem), &hashmod.found.DeviceData);
  clSetKernelArg(mHashMod, 1, sizeof(cl_mem), &hashmod.count.DeviceData);
  clSetKernelArg(mHashMod, 2, sizeof(cl_mem), &hashmod.primorialBitField.DeviceData);
  clSetKernelArg(mHashMod, 3, sizeof(cl_mem), &hashmod.midstate.DeviceData);

  uint64_t totalTime = 0;
  unsigned totalHashes = 0;
  int numhash = 64 * 131072;

  unsigned multiplierSizes[128];
  memset(multiplierSizes, 0, sizeof(multiplierSizes));
  
  for (unsigned i = 0; i < iterationsNum; i++) {
    {
      uint8_t *pHeader = (uint8_t*)&blockheader;
      for (unsigned i = 0; i < sizeof(blockheader); i++)
        pHeader[i] = rand32();
      blockheader.version = PrimeMiner::block_t::CURRENT_VERSION;
      blockheader.nonce = 1;  
      
      simplePrecalcSHA256(&blockheader, hashmod.midstate, queue, mHashMod);
    }    

    hashmod.count.copyToDevice(queue, false);
    
    size_t globalSize[] = { numhash, 1, 1 };
    size_t localSize[] = { defaultGroupSize, 1 };
 
    hashmod.count[0] = 0;
    hashmod.count.copyToDevice(queue);
    clFinish(queue);
    auto gpuBegin = std::chrono::steady_clock::now();  
    
    {
      cl_event event;
      cl_int result;
      if ((result = clEnqueueNDRangeKernel(queue,
                                           mHashMod,
                                           1,
                                           0,
                                           globalSize,
                                           localSize,
                                           0,
                                           0, &event)) != CL_SUCCESS) {
        fprintf(stderr, "clEnqueueNDRangeKernel error!\n");
        return;
      }
        
      cl_int error;
      if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
        fprintf(stderr, "clWaitForEvents error %i!\n", error);
        return;
      }
        
      clReleaseEvent(event);
    } 
    
    auto gpuEnd = std::chrono::steady_clock::now();  
    
    hashmod.found.copyToHost(queue, false);
    hashmod.primorialBitField.copyToHost(queue, false);
    hashmod.count.copyToHost(queue, false);
    clFinish(queue);
    
    totalTime += std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count();
    totalHashes += hashmod.count[0];
    
    for (unsigned i = 0; i < hashmod.count[0]; i++) {
      uint256 hashValue;
      PrimeMiner::block_t b = blockheader;
      b.nonce = hashmod.found[i];      
      
      uint32_t primorialBitField = hashmod.primorialBitField[i];
      uint32_t primorialIdx = primorialBitField >> 16;
      uint64_t realPrimorial = 1;
      for (unsigned j = 0; j < primorialIdx+1; j++) {
        if (primorialBitField & (1 << j))
          realPrimorial *= gPrimes[j];
      }      
      
      mpz_class mpzRealPrimorial;        
      mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);            
      primorialIdx = std::max(mPrimorial, primorialIdx) - mPrimorial;
      mpz_class mpzHashMultiplier = allPrimorials[primorialIdx] / mpzRealPrimorial;
      unsigned hashMultiplierSize = mpz_sizeinbase(mpzHashMultiplier.get_mpz_t(), 2);      
      multiplierSizes[hashMultiplierSize]++;
      
      SHA_256 sha;
      sha.init();
      sha.update((const unsigned char*)&b, sizeof(b));
      sha.final((unsigned char*)&hashValue);
      sha.init();
      sha.update((const unsigned char*)&hashValue, sizeof(uint256));
      sha.final((unsigned char*)&hashValue);      
      
      if(hashValue < (uint256(1) << 255)){
        printf(" * error: hash does not meet minimum.\n");
        continue;
      }
      
        
      mpz_class mpzHash;
      mpz_set_uint256(mpzHash.get_mpz_t(), hashValue);
      if(!mpz_divisible_p(mpzHash.get_mpz_t(), mpzRealPrimorial.get_mpz_t())){
        printf(" * error: mpz_divisible_ui_p failed.\n");
        continue;
      }    
      
      
      uint32_t multiplierBitField = hashmod.primorialBitField[i];
      
      unsigned multiplierCount = 0;
      for (unsigned j = 0; j < 32; j++)
        multiplierCount += ((multiplierBitField & (1 << j)) != 0);
    }
  }
  
  double averageHashes = (double)totalHashes / iterationsNum;
  printf(" MHash per second: %.3lf\n", iterationsNum*numhash / (double)totalTime);
  printf(" Hash per iteration: %.3lf (%.6lf %%)\n", averageHashes, averageHashes*100/numhash);
 
  uint64_t totalSize = 0;
  unsigned hashes = 0;
  for (unsigned i = 0; i < 128; i++) {
    if (multiplierSizes[i]) {
      hashes += multiplierSizes[i];
      totalSize += multiplierSizes[i] * i;
    }
  }
  printf(" Average hash multiplier size: %.3lf\n", totalSize / (double)hashes);  
}

void sieveTestBenchmark(cl_command_queue queue,
                        cl_kernel *kernels,
                        unsigned defaultGroupSize,
                        unsigned groupsNum,
                        mpz_class *allPrimorial,
                        unsigned mPrimorial,
                        config_t mConfig,
                        unsigned mDepth,
                        bool checkCandidates)
{
  printf("\n *** sieve (%s) benchmark ***\n", checkCandidates ? "check" : "performance");  
  
  cl_kernel mHashMod = kernels[CLKernelHashMod];
  cl_kernel mSieveSetup = kernels[CLKernelSieveSetup];
  cl_kernel mSieve = kernels[CLKernelSieve];
  cl_kernel mSieveSearch = kernels[CLKernelSieveSearch];
  
  PrimeMiner::search_t hashmod;
  PrimeMiner::block_t blockheader;
  lifoBuffer<PrimeMiner::hash_t> hashes(PW);
  clBuffer<cl_uint> hashBuf;
  clBuffer<cl_uint> sieveBuf[2];
  clBuffer<cl_uint> sieveOff[2];  
  clBuffer<PrimeMiner::fermat_t> sieveBuffers[64][FERMAT_PIPELINES];
  clBuffer<cl_uint> candidatesCountBuffers[64];

  cl_mem primeBuf[maxHashPrimorial];
  cl_mem primeBuf2[maxHashPrimorial];
  
  for (unsigned i = 0; i < maxHashPrimorial - mPrimorial; i++) {
    cl_int error = 0;
    primeBuf[i] = clCreateBuffer(gContext, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 mConfig.PCOUNT*sizeof(cl_uint), &gPrimes[mPrimorial+i+1], &error);
    OCL(error);
    primeBuf2[i] = clCreateBuffer(gContext, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  mConfig.PCOUNT*2*sizeof(cl_uint), &gPrimes2[2*(mPrimorial+i)+2], &error);
    OCL(error);
  }
  
  clBuffer<cl_uint> modulosBuf[maxHashPrimorial];
  unsigned modulosBufferSize = mConfig.PCOUNT*(mConfig.N-1);   
  for (unsigned bufIdx = 0; bufIdx < maxHashPrimorial-mPrimorial; bufIdx++) {
    clBuffer<cl_uint> &current = modulosBuf[bufIdx];
    current.init(modulosBufferSize, CL_MEM_READ_ONLY);
    for (unsigned i = 0; i < mConfig.PCOUNT; i++) {
      mpz_class X = 1;
      for (unsigned j = 0; j < mConfig.N-1; j++) {
        X <<= 32;
        mpz_class mod = X % gPrimes[i+mPrimorial+bufIdx+1];
        current[mConfig.PCOUNT*j+i] = mod.get_ui();
      }
    }
    
    current.copyToDevice(queue);
  }  
  
  hashmod.midstate.init(8*sizeof(cl_uint), CL_MEM_READ_ONLY);
  hashmod.found.init(2048, CL_MEM_READ_WRITE);
  hashmod.primorialBitField.init(2048, CL_MEM_READ_WRITE);
  hashmod.count.init(1, CL_MEM_READ_WRITE);
  hashBuf.init(PW*mConfig.N, CL_MEM_READ_WRITE);

  clSetKernelArg(mHashMod, 0, sizeof(cl_mem), &hashmod.found.DeviceData);
  clSetKernelArg(mHashMod, 1, sizeof(cl_mem), &hashmod.count.DeviceData);
  clSetKernelArg(mHashMod, 2, sizeof(cl_mem), &hashmod.primorialBitField.DeviceData);
  clSetKernelArg(mHashMod, 3, sizeof(cl_mem), &hashmod.midstate.DeviceData);

  int numhash = 64*262144;

  unsigned hashm[32];
  memset(hashm, 0, sizeof(hashm));

  {
    uint8_t *pHeader = (uint8_t*)&blockheader;
    for (unsigned i = 0; i < sizeof(blockheader); i++)
      pHeader[i] = rand();
    blockheader.version = PrimeMiner::block_t::CURRENT_VERSION;
    blockheader.nonce = 1;    
    simplePrecalcSHA256(&blockheader, hashmod.midstate, queue, mHashMod);
  }    

  hashmod.count.copyToDevice(queue, false);
    
  size_t globalSize[] = { numhash, 1, 1 };
  size_t localSize[] = { defaultGroupSize, 1 };    
 
  hashmod.count[0] = 0;
  hashmod.count.copyToDevice(queue);

  {
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         mHashMod,
                                         1,
                                         0,
                                         globalSize,
                                         localSize,
                                         0,
                                         0, &event)) != CL_SUCCESS) {
      fprintf(stderr, "[mHashMod] clEnqueueNDRangeKernel error!\n");
      return;
    }
        
    cl_int error;
    if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
      fprintf(stderr, "[mHashMod] clWaitForEvents error %i!\n", error);
      return;
    }
      
    clReleaseEvent(event);
  }
    
  hashmod.found.copyToHost(queue, false);
  hashmod.primorialBitField.copyToHost(queue, false);
  hashmod.count.copyToHost(queue, false);
  clFinish(queue);

  for(unsigned i = 0; i < hashmod.count[0]; ++i) {
    PrimeMiner::hash_t hash;
    hash.time = blockheader.time;
    hash.nonce = hashmod.found[i];
    uint32_t primorialBitField = hashmod.primorialBitField[i];
    uint32_t primorialIdx = primorialBitField >> 16;
    uint64_t realPrimorial = 1;
    for (unsigned j = 0; j < primorialIdx+1; j++) {
      if (primorialBitField & (1 << j))
        realPrimorial *= gPrimes[j];
    }      
    
    mpz_class mpzRealPrimorial;        
    mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);            
    primorialIdx = std::max(mPrimorial, primorialIdx) - mPrimorial;
    mpz_class mpzHashMultiplier = allPrimorial[primorialIdx] / mpzRealPrimorial;
    unsigned hashMultiplierSize = mpz_sizeinbase(mpzHashMultiplier.get_mpz_t(), 2);      
    mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);        
    
    PrimeMiner::block_t b = blockheader;
    b.nonce = hash.nonce;
    
    SHA_256 sha;
    sha.init();
    sha.update((const unsigned char*)&b, sizeof(b));
    sha.final((unsigned char*)&hash.hash);
    sha.init();
    sha.update((const unsigned char*)&hash.hash, sizeof(uint256));
    sha.final((unsigned char*)&hash.hash);

    if(hash.hash < (uint256(1) << 255)){
      printf(" * error: hash does not meet minimum.\n");
      continue;
    }
    
    mpz_class mpzHash;
    mpz_set_uint256(mpzHash.get_mpz_t(), hash.hash);
    if(!mpz_divisible_p(mpzHash.get_mpz_t(), mpzRealPrimorial.get_mpz_t())){
      printf(" * error: mpz_divisible_ui_p failed.\n");
      continue;
    }    

    mpz_set_uint256(mpzHash.get_mpz_t(), hash.hash);
    hash.primorialIdx = primorialIdx;
    hash.primorial = mpzHashMultiplier;
    hash.shash = mpzHash * hash.primorial;      
    
    unsigned hid = hashes.push(hash);
    memset(&hashBuf[hid*mConfig.N], 0, sizeof(uint32_t)*mConfig.N);
    mpz_export(&hashBuf[hid*mConfig.N], 0, -1, 4, 0, 0, hashes.get(hid).shash.get_mpz_t());        
  }

  hashBuf.copyToDevice(queue, false);
  
  for(int sieveIdx = 0; sieveIdx < 64; ++sieveIdx) {
    for (int pipelineIdx = 0; pipelineIdx < FERMAT_PIPELINES; pipelineIdx++)
      sieveBuffers[sieveIdx][pipelineIdx].init(MSO, CL_MEM_READ_WRITE);
      
    candidatesCountBuffers[sieveIdx].init(FERMAT_PIPELINES, CL_MEM_READ_WRITE);
  }  
  
  for(int k = 0; k < 2; ++k){
    sieveBuf[k].init(mConfig.SIZE*mConfig.STRIPES/2*mConfig.WIDTH, CL_MEM_READ_WRITE);
    sieveOff[k].init(mConfig.PCOUNT*mConfig.WIDTH, CL_MEM_READ_WRITE);
  }  

  clSetKernelArg(mSieveSetup, 0, sizeof(cl_mem), &sieveOff[0].DeviceData);
  clSetKernelArg(mSieveSetup, 1, sizeof(cl_mem), &sieveOff[1].DeviceData);
  clSetKernelArg(mSieveSetup, 3, sizeof(cl_mem), &hashBuf.DeviceData);
  clSetKernelArg(mSieveSearch, 0, sizeof(cl_mem), &sieveBuf[0].DeviceData);
  clSetKernelArg(mSieveSearch, 1, sizeof(cl_mem), &sieveBuf[1].DeviceData);
  clSetKernelArg(mSieveSearch, 7, sizeof(cl_uint), &mDepth);

  unsigned count = checkCandidates ? 1 : std::min(64u, hashmod.count[0]);
  unsigned candidates320[64];
  unsigned candidates352[64];
  
  clFinish(queue);  
  auto gpuBegin = std::chrono::steady_clock::now();  
  
  for (unsigned i = 0; i < count; i++) {
    cl_int hid = hashes.pop();
    unsigned primorialIdx = hashes.get(hid).primorialIdx;
    clSetKernelArg(mSieveSetup, 2, sizeof(cl_mem), &primeBuf[primorialIdx]);
    clSetKernelArg(mSieveSetup, 5, sizeof(cl_mem), &modulosBuf[primorialIdx].DeviceData);          
    clSetKernelArg(mSieve, 2, sizeof(cl_mem), &primeBuf2[primorialIdx]);        

    {
      clSetKernelArg(mSieveSetup, 4, sizeof(cl_int), &hid);
      size_t globalSize[] = { mConfig.PCOUNT, 1, 1 };
      clEnqueueNDRangeKernel(queue, mSieveSetup, 1, 0, globalSize, 0, 0, 0, 0);
    }

    {
      size_t globalSize[] = { defaultGroupSize*mConfig.STRIPES/2, mConfig.WIDTH };
      size_t localSize[] = { defaultGroupSize, 1 };
      clSetKernelArg(mSieve, 0, sizeof(cl_mem), &sieveBuf[0].DeviceData);
      clSetKernelArg(mSieve, 1, sizeof(cl_mem), &sieveOff[0].DeviceData);      
      OCL(clEnqueueNDRangeKernel(queue, mSieve, 2, 0, globalSize, localSize, 0, 0, 0));
    }
    
    {
      size_t globalSize[] = { defaultGroupSize*mConfig.STRIPES/2, mConfig.WIDTH };
      size_t localSize[] = { defaultGroupSize, 1 };
      clSetKernelArg(mSieve, 0, sizeof(cl_mem), &sieveBuf[1].DeviceData);
      clSetKernelArg(mSieve, 1, sizeof(cl_mem), &sieveOff[1].DeviceData);      
      OCL(clEnqueueNDRangeKernel(queue, mSieve, 2, 0, globalSize, localSize, 0, 0, 0));
    }    

    candidatesCountBuffers[i][0] = 0;
    candidatesCountBuffers[i][1] = 0;    
    candidatesCountBuffers[i].copyToDevice(queue, false);
        
    {
      cl_uint multiplierSize = mpz_sizeinbase(hashes.get(hid).shash.get_mpz_t(), 2);
      clSetKernelArg(mSieveSearch, 2, sizeof(cl_mem), &sieveBuffers[i][0].DeviceData);
      clSetKernelArg(mSieveSearch, 3, sizeof(cl_mem), &sieveBuffers[i][1].DeviceData);          
      clSetKernelArg(mSieveSearch, 4, sizeof(cl_mem), &candidatesCountBuffers[i].DeviceData);
      clSetKernelArg(mSieveSearch, 5, sizeof(cl_int), &hid);
      clSetKernelArg(mSieveSearch, 6, sizeof(cl_uint), &multiplierSize);
      size_t globalSize[] = { mConfig.SIZE*mConfig.STRIPES/2, 1, 1 };
      size_t localSize[] = { 256, 1 };
      OCL(clEnqueueNDRangeKernel(queue, mSieveSearch, 1, 0, globalSize, localSize, 0, 0, 0));
          
      candidatesCountBuffers[i].copyToHost(queue, false);   
    }
  
    if (checkCandidates) {
      sieveBuf[0].copyToHost(queue);
      sieveBuf[1].copyToHost(queue);
      sieveBuffers[i][0].copyToHost(queue);
      sieveBuffers[i][1].copyToHost(queue);
      clFinish(queue);
      
      std::set<mpz_class> multipliers;
      unsigned invalidCount = 0;
      sieveResultsTest(gPrimes,
                       hashes.get(hid).shash,
                       (uint8_t*)sieveBuf[0].HostData,
                       (uint8_t*)sieveBuf[1].HostData,
                       mConfig.SIZE*32*mConfig.STRIPES/2,
                       mConfig.TARGET,
                       mConfig.PCOUNT,
                       mConfig.WIDTH-mConfig.TARGET,
                       multipliers,
                       &invalidCount);

      unsigned n320 = candidatesCountBuffers[i][0];
      unsigned n352 = candidatesCountBuffers[i][1];
      unsigned diff = 0;
      for (unsigned j = 0; j < n320; j++) {
        PrimeMiner::fermat_t &c = sieveBuffers[i][0].get(j);
        mpz_class X = ((mpz_class)c.index) << c.origin;
        diff += !multipliers.count(X);
      }
      
      for (unsigned j = 0; j < n352; j++) {
        PrimeMiner::fermat_t &c = sieveBuffers[i][1].get(j);
        mpz_class X = ((mpz_class)c.index) << c.origin;
        diff += !multipliers.count(X);
      }      
      
      double coeff = fabs(n320+n352 - multipliers.size()) / multipliers.size();

      printf(" * [%s] found candidates by CPU: %u by GPU: %u\n",
             coeff <= 0.01  ? "OK" : "FAILED",
             (unsigned)multipliers.size(),
             n320 + n352);
      printf(" * [%s] invalid candidates: %u\n", !invalidCount ? "OK" : "FAILED", invalidCount);
      printf(" * [%s] CPU/GPU candidates difference: %u\n", !diff ? "OK" : "FAILED", diff);
    }
  }

  if (!checkCandidates) {
    clFinish(queue);
    auto gpuEnd = std::chrono::steady_clock::now();  
    auto totalTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count();
    double iterationTime = (double)totalTime / count;
    uint64_t bitsInSieve = mConfig.SIZE*32*mConfig.STRIPES/2*mConfig.WIDTH;
    double scanSpeed = bitsInSieve / iterationTime;
  
    unsigned n320 = 0, n352 = 0;
    for (unsigned i = 0; i < count; i++) {
      n320 += candidatesCountBuffers[i][0];
      n352 += candidatesCountBuffers[i][1];
    }

    printf(" * scan speed: %.3lf G\n", scanSpeed/1000.0);
    printf(" * iteration time: %.3lfms\n", iterationTime/1000.0);  
    printf(" * candidates per second: %.3lf\n", (n320+n352)/(totalTime/1000000.0));
    printf(" * candidates per iteration: %.2lf (%.2lf 320bit, %.2lf 352bit)\n",
           (double)(n320+n352) / count,
           (double)n320 / count,
           (double)n352 / count);
    printf(" * 320bit/352bit ratio: %.3lf/1\n", (double)n320/(double)n352);
    printf("\n");
  }
}

void runBenchmarks(cl_context context,
                   cl_program program,
                   cl_device_id deviceId,
                   unsigned mPrimorial,
                   unsigned depth,
                   unsigned defaultGroupSize)
{
  char deviceName[128] = {0};
  cl_uint computeUnits;
  clBuffer<config_t> mConfig;
  mpz_class allPrimorials[maxHashPrimorial];

  clGetDeviceInfo(deviceId, CL_DEVICE_NAME, sizeof(deviceName), deviceName, 0);
  clGetDeviceInfo(deviceId, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(computeUnits), &computeUnits, 0);
  printf("%s; %u compute units\n", deviceName, computeUnits);  
  
  std::unique_ptr<cl_kernel[]> kernels(new cl_kernel[CLKernelsNum]);
  for (unsigned i = 0; i < CLKernelsNum; i++) {
    cl_int clResult;
    kernels[i] = clCreateKernel(program, gOpenCLKernelNames[i], &clResult);
    if (clResult != CL_SUCCESS) {
      fprintf(stderr, " * Error: can't found kernel %s\n", gOpenCLKernelNames[i]);
      return;
    }
  }
  
  cl_int error;  
  cl_command_queue queue = clCreateCommandQueue(context, deviceId, 0, &error);
  if (!queue || error != CL_SUCCESS) {
    fprintf(stderr, " * Error: can't create command queue\n");
    return;
  }
  
  // Get miner config
  {
    mConfig.init(1);
    
    cl_kernel getconf = clCreateKernel(gProgram, "getconfig", &error);
    clSetKernelArg(getconf, 0, sizeof(cl_mem), &mConfig.DeviceData);
    clEnqueueTask(queue, getconf, 0, 0, 0);
    mConfig.copyToHost(queue, true);
    clFinish(queue);
  }  
  
  {
    for (unsigned i = 0; i < maxHashPrimorial - mPrimorial; i++) {
      mpz_class p = 1;
      for(unsigned j = 0; j <= mPrimorial+i; j++)
        p *= gPrimes[j];
      
      allPrimorials[i] = p;
    }    
  }  

  multiplyBenchmark(queue, kernels.get(), computeUnits*4, 320/32, 262144, true);  

  multiplyBenchmark(queue, kernels.get(), computeUnits*4, 320/32, 262144, true);
  multiplyBenchmark(queue, kernels.get(), computeUnits*4, 320/32, 262144, false);
  multiplyBenchmark(queue, kernels.get(), computeUnits*4, 352/32, 262144, true);    
  multiplyBenchmark(queue, kernels.get(), computeUnits*4, 352/32, 262144, false);

  fermatTestBenchmark(queue, kernels.get(), computeUnits*4, 320/32, 131072);
  fermatTestBenchmark(queue, kernels.get(), computeUnits*4, 352/32, 131072);

  hashmodBenchmark(queue, kernels.get(), defaultGroupSize, 0, allPrimorials, mPrimorial);
  sieveTestBenchmark(queue, kernels.get(), defaultGroupSize, computeUnits*4, allPrimorials, mPrimorial, *mConfig.HostData, depth, true);
  sieveTestBenchmark(queue, kernels.get(), defaultGroupSize, computeUnits*4, allPrimorials, mPrimorial, *mConfig.HostData, depth, false);
}
