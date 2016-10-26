#include "zcashcpuclient.h"
#include "zcash/equihash_original.h"
#include "sodium.h"
#include "sha256.h"

#define NUMCPUs 1

//#include "/data/build/inc/printhex.h"

double GetPrimeDifficulty(unsigned int nBits)
{
    return 10;
}

BaseClient *createClient(zctx_t *ctx)
{
  return new ZCashCPUClient(ctx);
}

void ZCashMiner::InvokeMining(void *args, zctx_t *ctx, void *pipe) {
  
  ((ZCashMiner*)args)->Mining(ctx, pipe); 
}

ZCashMiner::ZCashMiner(unsigned int id) : mID(id), _cancel(false)
{

}

bool ZCashMiner::Initialize()
{
  return true;
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

  zsocket_signal(pipe);
  zsocket_poll(pipe, -1);

//   uint8_t serializedHeader[1024];
  CBlockHeader header;
  uint256 hashTarget;
  crypto_generichash_blake2b_state state;
  SHA_256 sha;
//   xmstream stream(serializedHeader, sizeof(serializedHeader));
  
  bool run = true;
  printf("ZCash CPU miner thread %d started\n", mID);
  while(run){
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
      header.data.nTime = work.time() + mID;
      header.data.nBits = work.bits();
      header.nNonce = 0; 
      
      EhInitialiseState(work.n(), work.k(), state);
      hashTarget = work.bits() & 0x007FFFFF;
      unsigned exponent = work.bits() >> 24;
      if (exponent <= 3)
        hashTarget >>= 8*(3-exponent);
      else
        hashTarget <<= 8*(exponent-3);
      
      fprintf(stderr, "header hash=");
      //printhex(stderr, &header.data, sizeof(header.data), '\n');
      crypto_generichash_blake2b_update(&state, (unsigned char*)&header.data, sizeof(header.data));
      
      sha.init();
      sha.update((unsigned char*)&header.data, sizeof(header.data));
      _cancel = false;
    }
    

    crypto_generichash_blake2b_state curr_state = state;
    crypto_generichash_blake2b_update(&curr_state, header.nNonce.begin(), header.nNonce.size());    

    SHA_256 current_sha = sha;
    current_sha.update(header.nNonce.begin(), header.nNonce.size());
    unsigned nonceu32 = (unsigned)header.nNonce.Get64();
    
    std::function<bool(std::vector<unsigned char>)> validBlock = [this, &nonceu32, &current_sha, &hashTarget](const std::vector<unsigned char> &soln) {
      uint256 headerHash;
      fprintf(stderr, "\n\nnonce: %u, solution:\n", nonceu32);
      //printhex(stderr, &soln[0], soln.size(), '\n');
      current_sha.update(&soln[0], soln.size());
      current_sha.final((unsigned char*)&headerHash);
      current_sha.init();
      current_sha.update((unsigned char*)&headerHash, sizeof(uint256));
      current_sha.final((unsigned char*)&headerHash);
      
      printf("check share: %s <= %s\n", headerHash.ToString().c_str(), hashTarget.ToString().c_str());
      if (headerHash > hashTarget)
        return false;
      
      printf("Miner thread %u found a BLOCK\n", mID);
      return true;
    };    
    
    std::function<bool(EhSolverCancelCheck)> cancelled = [this](EhSolverCancelCheck pos) {
      return _cancel;
    };    
    
    try {
      if (EhOptimisedSolve(work.n(), work.k(), curr_state, validBlock, cancelled))
        break;
    } catch (EhSolverCancelledException&) {
      ;
    }
    
    ++header.nNonce;
  }    
  
  printf("thread %d stopped.\n", mID); 
  zsocket_destroy(ctx, blocksub);
  zsocket_destroy(ctx, worksub);
  zsocket_destroy(ctx, statspush);
  zsocket_destroy(ctx, sharepush);
  
  zsocket_signal(pipe);  
}



ZCashCPUClient::~ZCashCPUClient()
{
}


int ZCashCPUClient::GetStats(proto::ClientStats& stats)
{
  stats.set_cpd(0);
  stats.set_errors(0);
  stats.set_temp(0);  
  return 0;
}

bool ZCashCPUClient::Initialize(Configuration *cfg, bool benchmarkOnly)
{
  for (unsigned i = 0; i < NUMCPUs; ++i) {
    std::pair<ZCashMiner*,void*> worker;
      
    ZCashMiner *miner = new ZCashMiner(i);
    miner->Initialize();
        
    void* pipe = zthread_fork(mCtx, &ZCashMiner::InvokeMining, miner);
    zsocket_wait(pipe);
    zsocket_signal(pipe);
    worker.first = miner;
    worker.second = pipe;
    mWorkers.push_back(worker);
  }
  
  return true;
}

void ZCashCPUClient::NotifyBlock(const proto::Block& block)
{
  SendPub(block, mBlockPub);
}

void ZCashCPUClient::setup_adl()
{
  return;
}

void ZCashCPUClient::TakeWork(const proto::Work& work)
{
  SendPub(work, mWorkPub);
  for (auto &w: mWorkers)
    w.first->cancel();
}

void ZCashCPUClient::Toggle()
{
  for(unsigned i = 0; i < mWorkers.size(); ++i)
    if(mWorkers[i].first){
      zsocket_signal(mWorkers[i].second);
    }
  
  mPaused = !mPaused;
}
