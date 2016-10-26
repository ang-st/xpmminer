#include "baseclient.h"
#include "uint256.h"

class ZCashMiner {
private:
  unsigned mID;
  
public:
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
    std::vector<unsigned char> nSolution;
  };
  
  bool _cancel;
  
public:
  ZCashMiner(unsigned id);
  bool Initialize();
  static void InvokeMining(void *args, zctx_t *ctx, void *pipe);
  void Mining(zctx_t *ctx, void *pipe);
  void cancel() { _cancel = true; }
};

class ZCashCPUClient : public BaseClient {
public:
  ZCashCPUClient(zctx_t *ctx) : BaseClient(ctx) {};
  virtual ~ZCashCPUClient();
  
  bool Initialize(Configuration* cfg, bool benchmarkOnly = false);
  void NotifyBlock(const proto::Block& block);
  void TakeWork(const proto::Work& work);
  int GetStats(proto::ClientStats& stats);
  void Toggle();
  void setup_adl();  
  
private:
  std::vector<std::pair<ZCashMiner*, void*> > mWorkers;
};
