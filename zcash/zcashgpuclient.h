#include "../baseclient.h"
#include "../uint256.h"
#include "../opencl.h"

#include <list>

#define __OPENCL_HOST__
#include "gpu/common.h"

struct stats_t {
  unsigned id;
  double sols;
  
  stats_t(){
    id = 0;
    sols = 0;
  }
  
};

struct MinerInstance {
  cl_command_queue queue;
  clBuffer<blake2b_state> blake2bState;
  clBuffer<uint32_t> heap0;
  clBuffer<uint32_t> heap1;
  clBuffer<bsizes> nslots;
  clBuffer<proof> sols;    
  clBuffer<uint32_t> numSols;
  cl_kernel _digitHKernel;
  cl_kernel _digitOKernel;  
  cl_kernel _digitEKernel;
  cl_kernel _digitKKernel;
  cl_kernel _digitKernels[9];
  
  uint256 nonce;
  
  bool init(cl_device_id dev, unsigned threadsNum, unsigned threadsPerBlock);
};

class ZCashMiner {
private:
  struct solsPerSecond {
    time_t time;
    unsigned sols;
  };  
  
private:
  unsigned mID;
  unsigned pipelinesNum;
  MinerInstance *miners;
  unsigned _threadsNum;
  unsigned _threadsPerBlocksNum;  
//   cl_command_queue *queues;
  
//   unsigned _threadsNum;
//   unsigned _threadsPerBlocksNum;

//   clBuffer<blake2b_state> blake2bState;
//   clBuffer<uint32_t> heap0;
//   clBuffer<uint32_t> heap1;
//   clBuffer<bsizes> nslots;
//   clBuffer<proof> sols;    
//   clBuffer<uint32_t> numSols;
//   
//   cl_kernel _digitHKernel;
//   cl_kernel _digitOKernel;  
//   cl_kernel _digitEKernel;
//   cl_kernel _digitKKernel;
//   cl_kernel _digitKernels[10];
  
  std::list<solsPerSecond> _stats;
  
  void pushStats(unsigned solsNum) {
    auto currentTime = time(0);
    if (_stats.empty()) {
      solsPerSecond s = {currentTime, solsNum};
      _stats.push_back(s);
    } else if (_stats.back().time == currentTime) {
      _stats.back().sols += solsNum;
    } else {
      solsPerSecond s = {currentTime, solsNum};
      _stats.push_back(s);
    }
  }
  
  void cleanupStats() {
    auto currentTime = time(0);
    while (!_stats.empty() &&_stats.front().time - currentTime >= 60)
      _stats.pop_front();
  }
  
  double calcStats() {
    auto currentTime = time(0);
    auto It = _stats.rbegin();
    auto last = currentTime;
    unsigned sum = 0;
    while (It != _stats.rend() && (It->time - currentTime) < 60) {
      last = It->time;
      sum += It->sols;
      ++It;
    }
    
    auto timeDiff = currentTime - last;
    return timeDiff ? (double)sum / timeDiff : 0;
  }
  
public:
#pragma pack(push, 1)
  struct CBlockHeader {
    // header
    static const size_t HEADER_SIZE=4+32+32+32+4+4+32; // excluding Equihash solution
    static const int32_t CURRENT_VERSION=4;
    
    struct {
      int32_t nVersion;
      uint256 hashPrevBlock;
      uint256 hashMerkleRoot;
      uint256 hashReserved;
      uint32_t nTime;
      uint32_t nBits;
    } data;
    
    uint256 nNonce;
#pragma pack(pop)
    std::vector<unsigned char> nSolution;
  };
  
  bool _cancel;
  
public:
  ZCashMiner(unsigned id);
  bool Initialize(cl_device_id dev, unsigned pipelines, unsigned threadsNum, unsigned threadsPerBlock);
  static void InvokeMining(void *args, zctx_t *ctx, void *pipe);
  void Mining(zctx_t *ctx, void *pipe);
  void cancel() { _cancel = true; }
};

class ZCashGPUClient : public BaseClient {
public:
  ZCashGPUClient(zctx_t *ctx) : BaseClient(ctx) {};
  virtual ~ZCashGPUClient();
  
  bool Initialize(Configuration* cfg, bool benchmarkOnly = false);
  void NotifyBlock(const proto::Block& block);
  void TakeWork(const proto::Work& work);
  int GetStats(proto::ClientStats& stats);
  void Toggle();
  void setup_adl();  
  
private:
  std::vector<std::pair<ZCashMiner*, void*> > mWorkers;
};
