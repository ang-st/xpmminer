#include "zcashgpuclient.h"
#include "../sha256.h"
extern "C" {
#include "../adl.h"
}
#include <gmpxx.h>

cl_context gContext = 0;
cl_program gProgram = 0;

double GetPrimeDifficulty(unsigned int nBits)
{
  uint256 powLimit256("03ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  uint32_t powLimit = powLimit256.GetCompact(false);
  int nShift = (nBits >> 24) & 0xff;
  int nShiftAmount = (powLimit >> 24) & 0xff;

  double dDiff = (double)(powLimit & 0x00ffffff) /  (double)(nBits & 0x00ffffff);
  while (nShift < nShiftAmount) {
    dDiff *= 256.0;
    nShift++;
  }
  
  while (nShift > nShiftAmount) {
    dDiff /= 256.0;
    nShift--;
  }

  return dDiff;
}

unsigned writeCompactSize(size_t size, uint8_t *out)
{
  if (size < 253) {
    out[0] = size;
    return 1;
  } else if (size <= std::numeric_limits<unsigned short>::max()) {
    out[0] = 253;
    *(uint16_t*)(out+1) = size;
    return 3;
  } else if (size <= std::numeric_limits<unsigned int>::max()) {
    out[0] = 254;
    *(uint32_t*)(out+1) = size;
    return 5;
  } else {
    out[0] = 255;
    *(uint64_t*)(out+1) = size;
  }
  return 0;
}

BaseClient *createClient(zctx_t *ctx)
{
  return new ZCashGPUClient(ctx);
}

inline void mpz_set_uint256(mpz_t r, uint256& u)
{
    mpz_import(r, 32 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, &u);
}

static void setheader(blake2b_state *ctx, const char *header, const uint32_t headerlen)
{
  uint32_t le_N = WN;
  uint32_t le_K = WK;
  char personal[] = "ZcashPoW01230123";
  memcpy(personal+8,  &le_N, 4);
  memcpy(personal+12, &le_K, 4);
  blake2b_param P[1];
  P->digest_length = HASHOUT;
  P->key_length    = 0;
  P->fanout        = 1;
  P->depth         = 1;
  P->leaf_length   = 0;
  P->node_offset   = 0;
  P->node_depth    = 0;
  P->inner_length  = 0;
  memset(P->reserved, 0, sizeof(P->reserved));
  memset(P->salt,     0, sizeof(P->salt));
  memcpy(P->personal, (const uint8_t *)personal, 16);
  blake2b_init_param(ctx, P);
  blake2b_update(ctx, (const uint8_t*)header, headerlen);
}

static void setnonce(blake2b_state *ctx, const uint8_t *nonce)
{
  blake2b_update(ctx, nonce, 32);
}

static int compu32(const void *pa, const void *pb)
{
  uint32_t a = *(uint32_t *)pa, b = *(uint32_t *)pb;
  return a<b ? -1 : a==b ? 0 : +1;
}

static bool duped(proof prf)
{
  proof sortprf;
  memcpy(sortprf, prf, sizeof(proof));
  qsort(sortprf, PROOFSIZE, sizeof(uint32_t), &compu32);
  for (uint32_t i=1; i<PROOFSIZE; i++)
    if (sortprf[i] <= sortprf[i-1])
      return true;
  return false;
}


static int inline digit(cl_command_queue clQueue, cl_kernel kernel, size_t nthreads, size_t threadsPerBlock)
{
  size_t globalSize[] = { nthreads, 1, 1 };
  size_t localSize[] = { threadsPerBlock, 1 };  
  OCLR(clEnqueueNDRangeKernel(clQueue, kernel, 1, 0, globalSize, localSize, 0, 0, 0), 1); 
  return 0;
}

static void CompressArray(const unsigned char* in, size_t in_len,
                   unsigned char* out, size_t out_len, size_t bit_len, size_t byte_pad)
{
    size_t in_width { (bit_len+7)/8 + byte_pad };
    uint32_t bit_len_mask { ((uint32_t)1 << bit_len) - 1 };

    // The acc_bits least-significant bits of acc_value represent a bit sequence
    // in big-endian order.
    size_t acc_bits = 0;
    uint32_t acc_value = 0;

    size_t j = 0;
    for (size_t i = 0; i < out_len; i++) {
        // When we have fewer than 8 bits left in the accumulator, read the next
        // input element.
        if (acc_bits < 8) {
            acc_value = acc_value << bit_len;
            for (size_t x = byte_pad; x < in_width; x++) {
                acc_value = acc_value | (
                    (
                        // Apply bit_len_mask across byte boundaries
                        in[j+x] & ((bit_len_mask >> (8*(in_width-x-1))) & 0xFF)
                    ) << (8*(in_width-x-1))); // Big-endian
            }
            j += in_width;
            acc_bits += bit_len;
        }

        acc_bits -= 8;
        out[i] = (acc_value >> acc_bits) & 0xFF;
    }
}

static void GetIndices(proof solution, uint8_t *out)
{
  size_t bytePad { sizeof(uint32_t) - ((COLLISION_BIT_LENGTH+1)+7)/8 };
  CompressArray((uint8_t*)solution, PROOFSIZE, out, COMPRESSED_PROOFSIZE, COLLISION_BIT_LENGTH+1, bytePad); 
}

ZCashMiner::ZCashMiner(unsigned id) : mID(id) {}


bool MinerInstance::init(cl_device_id dev,
                         unsigned int threadsNum,
                         unsigned int threadsPerBlock)
{
  cl_int error;
  
  queue = clCreateCommandQueue(gContext, dev, 0, &error);
  
  blake2bState.init(1, CL_MEM_READ_WRITE);
  heap0.init(sizeof(digit0)/sizeof(uint32_t), CL_MEM_HOST_NO_ACCESS);
  heap1.init(sizeof(digit1)/sizeof(uint32_t), CL_MEM_HOST_NO_ACCESS);
  nslots.init(2, CL_MEM_READ_WRITE);
  sols.init(MAXSOLS, CL_MEM_READ_WRITE);  
  numSols.init(1, CL_MEM_READ_WRITE);
  
  _digitHKernel = clCreateKernel(gProgram, "digitH", &error);
  _digitOKernel = clCreateKernel(gProgram, "digitOdd", &error);    
  _digitEKernel = clCreateKernel(gProgram, "digitEven", &error);  
  _digitKKernel = clCreateKernel(gProgram, "digitK", &error);     
  OCLR(clSetKernelArg(_digitHKernel, 0, sizeof(cl_mem), &blake2bState.DeviceData), 1);
  OCLR(clSetKernelArg(_digitHKernel, 1, sizeof(cl_mem), &heap0.DeviceData), 1);  
  OCLR(clSetKernelArg(_digitHKernel, 2, sizeof(cl_mem), &nslots.DeviceData), 1);  
  
  OCLR(clSetKernelArg(_digitOKernel, 1, sizeof(cl_mem), &heap0.DeviceData), 1);
  OCLR(clSetKernelArg(_digitOKernel, 2, sizeof(cl_mem), &heap1.DeviceData), 1);
  OCLR(clSetKernelArg(_digitOKernel, 3, sizeof(cl_mem), &nslots.DeviceData), 1);  
  OCLR(clSetKernelArg(_digitEKernel, 1, sizeof(cl_mem), &heap0.DeviceData), 1);
  OCLR(clSetKernelArg(_digitEKernel, 2, sizeof(cl_mem), &heap1.DeviceData), 1);
  OCLR(clSetKernelArg(_digitEKernel, 3, sizeof(cl_mem), &nslots.DeviceData), 1);  
  
  for (unsigned i = 1; i <= 8; i++) {
    char kernelName[32];
    sprintf(kernelName, "digit_%u", i);
    _digitKernels[i] = clCreateKernel(gProgram, kernelName, &error);
    OCLR(clSetKernelArg(_digitKernels[i], 0, sizeof(cl_mem), &heap0.DeviceData), 1);
    OCLR(clSetKernelArg(_digitKernels[i], 1, sizeof(cl_mem), &heap1.DeviceData), 1);  
    OCLR(clSetKernelArg(_digitKernels[i], 2, sizeof(cl_mem), &nslots.DeviceData), 1);     
  }
  
  OCLR(clSetKernelArg(_digitKKernel, 0, sizeof(cl_mem), &heap0.DeviceData), 1);   
  OCLR(clSetKernelArg(_digitKKernel, 1, sizeof(cl_mem), &heap1.DeviceData), 1);   
  OCLR(clSetKernelArg(_digitKKernel, 2, sizeof(cl_mem), &nslots.DeviceData), 1);
  OCLR(clSetKernelArg(_digitKKernel, 3, sizeof(cl_mem), &sols.DeviceData), 1);   
  OCLR(clSetKernelArg(_digitKKernel, 4, sizeof(cl_mem), &numSols.DeviceData), 1);       
}


bool ZCashMiner::Initialize(cl_device_id dev, unsigned pipelines, unsigned threadsNum, unsigned threadsPerBlock)
{
  pipelinesNum = pipelines;
  miners = new MinerInstance[pipelines];
  for (unsigned i = 0; i < pipelines; i++)
    miners[i].init(dev, threadsNum, threadsPerBlock);
  _threadsNum = threadsNum;
  _threadsPerBlocksNum = threadsPerBlock;
  printf("threads: %u, work size: %u\n", threadsNum, threadsPerBlock);
  return true;
}

void ZCashMiner::InvokeMining(void *args, zctx_t *ctx, void *pipe) {
  
  ((ZCashMiner*)args)->Mining(ctx, pipe); 
}


void ZCashMiner::Mining(zctx_t *ctx, void *pipe)
{
  void* blocksub = zsocket_new(ctx, ZMQ_SUB);
  void* worksub = zsocket_new(ctx, ZMQ_SUB);
  void* statspush = zsocket_new(ctx, ZMQ_PUSH);
  void* sharepush = zsocket_new(ctx, ZMQ_PUSH);
  
  zsocket_connect(blocksub, "inproc://blocks");
  zsocket_connect(worksub, "inproc://work");
  zsocket_connect(statspush, "inproc://stats");
  zsocket_connect(sharepush, "inproc://shares");
  
  {
    const char one[2] = {1, 0};
    zsocket_set_subscribe(blocksub, one);
    zsocket_set_subscribe(worksub, one);
  }
  
  proto::Block block;
  proto::Work work;
  proto::Share share;
  
  block.set_height(1);
  work.set_height(0);
  
  share.set_addr(gAddr);
  share.set_name(gClientName);
  share.set_clientid(gClientID);

  stats_t stats;
  stats.id = mID;
  stats.sols = 0;
  
  zsocket_signal(pipe);
  zsocket_poll(pipe, -1);  
  
  uint8_t compactSizeData[16];
  unsigned compactSize = writeCompactSize(COMPRESSED_PROOFSIZE, compactSizeData);
  
  CBlockHeader header;
  uint256 hashTarget;
  uint256 shareTarget;
  SHA_256 sha;  
  bool run = true;
  blake2b_state initialCtx;
  time_t statsTimeLabel = time(0);
  
  int currentInstance = 0;
  int readyInstance = -(int)pipelinesNum;
  
  printf("ZCash GPU miner thread %d started\n", mID);
  while(run){
    auto timeDiff = time(0) - statsTimeLabel;
    if (timeDiff >= 10) {
      stats.sols = calcStats();
      zsocket_sendmem(statspush, &stats, sizeof(stats), 0);
      statsTimeLabel = 0;
      cleanupStats();
    }
    
    if(zsocket_poll(pipe, 0)){
      zsocket_wait(pipe);
      zsocket_wait(pipe);
    }
    
    // get work
    bool reset = false;
    {
      bool getwork = true;
      while(getwork && run){
        
        if(zsocket_poll(worksub, 0) || work.height() < block.height()){
          run = ReceivePub(work, worksub);
          reset = true;
        }
        
        getwork = false;
        if(zsocket_poll(blocksub, 0) || work.height() > block.height()){
          run = ReceivePub(block, blocksub);
          getwork = true;
        }
      }
    }
    if(!run)
      break;
    
    // reset if new work
    if(reset) {
      header.data.nVersion = ZCashMiner::CBlockHeader::CURRENT_VERSION;
      header.data.hashPrevBlock.SetHex(block.hash());
      header.data.hashMerkleRoot.SetHex(work.merkle());
      header.data.hashReserved.SetHex(work.hashreserved());
      header.data.nTime = work.time();
      header.data.nBits = work.bits();
      header.nNonce = mID;
      header.nNonce <<= 64;
      setheader(&initialCtx, (const char*)&header.data, sizeof(header.data));
      
      {
        // setCompact
        hashTarget = work.bits() & 0x007FFFFF;
        unsigned exponent = work.bits() >> 24;
        if (exponent <= 3)
          hashTarget >>= 8*(3-exponent);
        else
          hashTarget <<= 8*(exponent-3);
      }
      
      {
        // setCompact
        shareTarget = block.reqdiff() & 0x007FFFFF;
        unsigned exponent = block.reqdiff() >> 24;
        if (exponent <= 3)
          shareTarget >>= 8*(3-exponent);
        else
          shareTarget <<= 8*(exponent-3);
      }

      sha.init();
      sha.update((unsigned char*)&header.data, sizeof(header.data));
      
      currentInstance = 0;
      readyInstance = -(int)pipelinesNum;      
    }
    
    MinerInstance &miner = miners[currentInstance];
    clFlush(miner.queue);
    
    miner.nonce = header.nNonce;
    *miner.blake2bState.HostData = initialCtx;
    setnonce(miner.blake2bState.HostData, header.nNonce.begin());
    memset(miner.nslots.HostData, 0, 2*sizeof(bsizes));
    *miner.numSols.HostData = 0;
    miner.blake2bState.copyToDevice(miner.queue, false);
    miner.nslots.copyToDevice(miner.queue, false);
    miner.numSols.copyToDevice(miner.queue, false);
    
    digit(miner.queue, miner._digitHKernel, _threadsNum, _threadsPerBlocksNum);
#if BUCKBITS == 16 && RESTBITS == 4 && defined XINTREE && defined(UNROLL)
    for (unsigned i = 1; i <= 8; i++)
      digit(miner.queue, miner._digitKernels[i], _threadsNum, _threadsPerBlocksNum);
#else    
    size_t globalSize[] = { _threadsNum, 1, 1 };
    size_t localSize[] = { _threadsPerBlocksNum, 1 };    
    for (unsigned r = 1; r < WK; r++) {
      if (r & 1) {
        OCL(clSetKernelArg(miner._digitOKernel, 0, sizeof(cl_uint), &r));
        OCL(clEnqueueNDRangeKernel(miner.queue, miner._digitOKernel, 1, 0, globalSize, localSize, 0, 0, 0)); 
      } else {
        OCL(clSetKernelArg(miner._digitEKernel, 0, sizeof(cl_uint), &r));
        OCL(clEnqueueNDRangeKernel(miner.queue, miner._digitEKernel, 1, 0, globalSize, localSize, 0, 0, 0)); 
      }
    }
#endif

    digit(miner.queue, miner._digitKKernel, _threadsNum, _threadsPerBlocksNum);

    if (readyInstance >= 0) {
      MinerInstance &readyMiner = miners[readyInstance];
    
      // get solutions
      readyMiner.sols.copyToHost(readyMiner.queue, true);
      readyMiner.numSols.copyToHost(readyMiner.queue, true);    
    
      unsigned nsols = 0;

      for (unsigned s = 0; s < readyMiner.numSols.HostData[0]; s++) {
        if (duped(readyMiner.sols[s])) {
          continue;
        }
      
        nsols++;

        uint8_t compressed[COMPRESSED_PROOFSIZE];
        for (unsigned i = 0; i < PROOFSIZE; i++)
          readyMiner.sols.HostData[s][i] = __builtin_bswap32(readyMiner.sols.HostData[s][i]);      
        GetIndices(readyMiner.sols.HostData[s], compressed);
      
        // compressed - use for hash calc
        bool isShare = false;
        bool isBlock = false;
        uint256 headerHash;
      

        SHA_256 current_sha = sha;
        
        current_sha.update(header.nNonce.begin(), header.nNonce.size());        
        current_sha.update(compactSizeData, compactSize);
        current_sha.update(compressed, COMPRESSED_PROOFSIZE);
        current_sha.final((unsigned char*)&headerHash);
        current_sha.init();
        current_sha.update((unsigned char*)&headerHash, sizeof(uint256));
        current_sha.final((unsigned char*)&headerHash);
      
        if (headerHash <= hashTarget) {
          // block found
          isShare = true;
          isBlock = true;
          printf("GPU %u found a BLOCK! hash=%s\n", mID, headerHash.ToString().c_str());
        } else if (headerHash <= shareTarget) {
          // share found
          isShare = true;
          printf("GPU %d found share\n", mID);
        }
      
        if (isShare) {
          share.set_hash(headerHash.GetHex());
          share.set_merkle(work.merkle()); 
          share.set_time(header.data.nTime);
          share.set_bits(work.bits());
          share.set_nonce(0);
          share.set_multi("");
          share.set_height(block.height());
          share.set_length(0);
          share.set_chaintype(0);
          share.set_isblock(isBlock);
          share.set_bignonce(readyMiner.nonce.GetHex());
          share.set_proofofwork(compressed, COMPRESSED_PROOFSIZE);
          Send(share, sharepush);
        }
      }

    
      pushStats(nsols);
    }
    
    ++header.nNonce;
    currentInstance = (currentInstance+1) % pipelinesNum;
    readyInstance = (readyInstance+1);
    if (readyInstance >= 0)
      readyInstance %= pipelinesNum;
  }
  
  printf("thread %d stopped.\n", mID); 
  zsocket_destroy(ctx, blocksub);
  zsocket_destroy(ctx, worksub);
  zsocket_destroy(ctx, statspush);
  zsocket_destroy(ctx, sharepush);
  
  zsocket_signal(pipe);    
}

ZCashGPUClient::~ZCashGPUClient()
{
}

int ZCashGPUClient::GetStats(proto::ClientStats& stats)
{
  unsigned nw = mWorkers.size();
  std::vector<bool> running(nw);
  std::vector<stats_t> wstats(nw);
  
  while(zsocket_poll(mStatsPull, 0)) {
    zmsg_t* msg = zmsg_recv(mStatsPull);
    if (!msg)
      break;
    
    zframe_t *frame = zmsg_last(msg);
    size_t fsize = zframe_size(frame);
    byte *fbytes = zframe_data(frame);
    
    if (fsize >= sizeof(stats_t)) {
      stats_t *tmp = (stats_t*)fbytes;
      if(tmp->id < nw) {
        running[tmp->id] = true;
        wstats[tmp->id] = *tmp;
      }
    }
    
    zmsg_destroy(&msg);
  }
  
  double sols = 0;
  int maxtemp = 0;
  unsigned ngpus = 0;
  int crashed = 0;
  
  for(unsigned i = 0; i < nw; ++i) {
    int devid = mDeviceMapRev[i];
    int temp = gpu_temp(devid);
    int activity = gpu_activity(devid);
    
    if(temp > maxtemp)
      maxtemp = temp;
    
    sols += wstats[i].sols;
    
    if(running[i]) {
      ngpus++;
      printf("[GPU %d] T=%dC A=%d%% sols=%.3lf\n", i, temp, activity, wstats[i].sols);
    } else if (!mWorkers[i].first)
      printf("[GPU %d] failed to start!\n", i);
    else if(mPaused) {
      printf("[GPU %d] paused\n", i);
    } else {
      crashed++;
      printf("[GPU %d] crashed!\n", i);
    }
  }
  
//   if (crashed) {
//     if (exitType == 1) {
//       exit(1);
//     } else if (exitType == 2) {
// #ifdef WIN32
//       ExitWindowsEx(EWX_REBOOT, 0);
// #else
//       system("/sbin/reboot");
// #endif
//     }
//   }
  
  if(mStatCounter % 10 == 0)
    for(unsigned i = 0; i < mNumDevices; ++i){
      int gpuid = mDeviceMap[i];
      if(gpuid >= 0)
        printf("GPU %d: core=%dMHz mem=%dMHz powertune=%d fanspeed=%d\n",
            gpuid, gpu_engineclock(i), gpu_memclock(i), gpu_powertune(i), gpu_fanspeed(i));
    }
  
  stats.set_cpd(sols);
  stats.set_errors(0);
  stats.set_temp(maxtemp);
  stats.set_unittype(1); // 0 for CPU, 1 for GPU
  
  mStatCounter++;
  
  return ngpus;  
}


bool ZCashGPUClient::Initialize(Configuration *cfg, bool benchmarkOnly)
{
  const char *platformId = cfg->lookupString("", "platform");
  const char *platformName = "";
  if (strcmp(platformId, "amd") == 0) {
    platformName = "AMD Accelerated Parallel Processing";
    platformType = ptAMD;    
  } else if (strcmp(platformId, "nvidia") == 0) {
    platformName = "NVIDIA CUDA";
    platformType = ptNVidia;    
  }
  
  std::vector<cl_device_id> allGpus;
  if (!clInitialize(platformName, allGpus)) {
    return false;
  }
    
  mNumDevices = allGpus.size();
  std::vector<bool> usegpu(mNumDevices, true);
  std::vector<int> threads(mNumDevices, 8192);
  std::vector<int> worksize(mNumDevices, 128);  
  mCoreFreq = std::vector<int>(mNumDevices, -1);
  mMemFreq = std::vector<int>(mNumDevices, -1);
  mPowertune = std::vector<int>(mNumDevices, 42);
  mFanSpeed = std::vector<int>(mNumDevices, 70);
  
  {
    StringVector cworksizes;
    StringVector cthreads;
    StringVector cdevices;
    StringVector ccorespeed;
    StringVector cmemspeed;
    StringVector cpowertune;
    StringVector cfanspeed;
    
    try {
      cfg->lookupList("", "worksizes", cworksizes);
      cfg->lookupList("", "threads", cthreads);      
      cfg->lookupList("", "devices", cdevices);
      cfg->lookupList("", "corefreq", ccorespeed);
      cfg->lookupList("", "memfreq", cmemspeed);
      cfg->lookupList("", "powertune", cpowertune);
      cfg->lookupList("", "fanspeed", cfanspeed);
    } catch(const ConfigurationException& ex) {}
    
    for(int i = 0; i < (int)mNumDevices; ++i) {
      if(i < cdevices.length())
        usegpu[i] = !strcmp(cdevices[i], "1");     
      if(i < cworksizes.length())
        worksize[i] = atoi(cworksizes[i]);
      if(i < cthreads.length())
        threads[i] = atoi(cthreads[i]);      
      if(i < ccorespeed.length())
        mCoreFreq[i] = atoi(ccorespeed[i]);
      if(i < cmemspeed.length())
        mMemFreq[i] = atoi(cmemspeed[i]);
      if(i < cpowertune.length())
        mPowertune[i] = atoi(cpowertune[i]);
      if(i < cfanspeed.length())
        mFanSpeed[i] = atoi(cfanspeed[i]);                        
    }
  }  
  
  std::vector<cl_device_id> gpus;
  for(unsigned i = 0; i < mNumDevices; ++i) {
    if (usegpu[i]) {
      printf("Using device %d as GPU %d\n", i, (int)gpus.size());
      mDeviceMap[i] = gpus.size();
      mDeviceMapRev[gpus.size()] = i;
      gpus.push_back(allGpus[i]);
    }else{
      mDeviceMap[i] = -1;
    }
  }
  
  if(!gpus.size()){
    printf("EXIT: config.txt says not to use any devices!?\n");
    return false;
  }
  
  std::vector<cl_int> binstatus;
  if (!clCompileKernel(allGpus,
                       "equiw200k9.bin",
                       { "zcash/gpu/equihash.cl" },
                       "-I./zcash/gpu -DXINTREE -DWN=200 -DWK=9 -DRESTBITS=4 -DUNROLL",
                       binstatus)) {
    return false;
  }
  
  for(unsigned i = 0; i < gpus.size(); ++i) {
      std::pair<ZCashMiner*,void*> worker;
      if(binstatus[i] == CL_SUCCESS){
      
        ZCashMiner *miner = new ZCashMiner(i);
        if (!miner->Initialize(gpus[i], 2, threads[i], worksize[i]))
          return false;
        
        void *pipe = zthread_fork(mCtx, &ZCashMiner::InvokeMining, miner);
        zsocket_wait(pipe);
        zsocket_signal(pipe);
        worker.first = miner;
        worker.second = pipe;
      } else {
        printf("GPU %d: failed to load kernel\n", i);
        worker.first = 0;
        worker.second = 0;
      
      }
    
      mWorkers.push_back(worker);
    }  
  
  return true;
}

void ZCashGPUClient::NotifyBlock(const proto::Block& block)
{
  SendPub(block, mBlockPub); 
}

void ZCashGPUClient::TakeWork(const proto::Work& work)
{
  SendPub(work, mWorkPub);
}

void ZCashGPUClient::Toggle()
{
  for(unsigned i = 0; i < mWorkers.size(); ++i)
    if(mWorkers[i].first){
      zsocket_signal(mWorkers[i].second);
    }
  
  mPaused = !mPaused;
}


void ZCashGPUClient::setup_adl(){
  
  init_adl(mNumDevices);
  
  for(unsigned i = 0; i < mNumDevices; ++i){
    
    if(mCoreFreq[i] > 0)
      if(set_engineclock(i, mCoreFreq[i]))
        printf("set_engineclock(%d, %d) failed.\n", i, mCoreFreq[i]);
    if(mMemFreq[i] > 0)
      if(set_memoryclock(i, mMemFreq[i]))
        printf("set_memoryclock(%d, %d) failed.\n", i, mMemFreq[i]);
    if(mPowertune[i] >= -20 && mPowertune[i] <= 20)
      if(set_powertune(i, mPowertune[i]))
        printf("set_powertune(%d, %d) failed.\n", i, mPowertune[i]);
    if (mFanSpeed[i] > 0)
      if(set_fanspeed(i, mFanSpeed[i]))
        printf("set_fanspeed(%d, %d) failed.\n", i, mFanSpeed[i]);
  }
}
