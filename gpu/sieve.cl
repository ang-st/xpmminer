/*
 * sieve.cl
 *
 *  Created on: 18.12.2013
 *      Author: mad
 */

#define S1RUNS (sizeof(nps_all)/sizeof(uint))
#define NLIFO 4

#ifdef __NVIDIA
__constant uint nps_all[] = { 4, 4, 5, 6, 7, 8, 8, 8 };
#else
__constant uint nps_all[] = { 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6 };
#endif


#ifdef __NVIDIA
__attribute__((reqd_work_group_size(LSIZE, 1, 1)))
__kernel void sieve(  __global uint* gsieve_all,
                      __global const uint* offset_all,
                      __global const uint2* primes)
{
  __local uint sieve[SIZE];
  
  const uint id = get_local_id(0);
  const uint stripe = get_group_id(0);
  const uint line = get_group_id(1);
  
  const uint entry = SIZE*32*(stripe+STRIPES/2);
  const float fentry = entry;
  
  __global const uint* offset = &offset_all[PCOUNT*line];
  
  for (uint i = id; i < SIZE; i += LSIZE)
    sieve[i] = 0;
  barrier(CLK_LOCAL_MEM_FENCE);
  
  uint poff = 0;
  
  // NVidia drivers version 343.x+ contains a bug, can't unroll this loop
  // #pragma unroll
  for(int b = 0; b < S1RUNS; b++) {
    uint nps = nps_all[b];
    const uint var = LSIZE >> nps;
    const uint lpoff = id & (var-1);
    uint ip = id >> (LSIZELOG2-nps);
    
    const uint2 tmp1 = primes[poff+ip];
    const uint prime = tmp1.x;
    const float fiprime = as_float(tmp1.y);
    
    const uint loffset = offset[poff+ip];
    const uint orb = (loffset >> 31) ^ 0x1;
    uint pos = loffset & 0x7FFFFFFF;
    
    poff += 1u << nps;
    pos = mad24((uint)(fentry * fiprime), prime, pos) - entry;
    pos = mad24((uint)((int)pos < (int)0), prime, pos);
    pos = mad24((uint)((int)pos < (int)0), prime, pos); // NVidia requires repeat this, why?
    pos = mad24(lpoff, prime, pos);
    
    uint4 vpos = {pos,
                  mad24(var, prime, pos),
                  mad24(var*2, prime, pos),
                  mad24(var*3, prime, pos)};
      
    const uint add = var*4*prime;
    while (vpos.w < SIZE*32) {
      uint4 bit = (uint4){orb, orb, orb, orb} << vpos;
      uint4 offset = vpos >> 5;
      atomic_or(&sieve[offset.x], bit.x);
      atomic_or(&sieve[offset.y], bit.y);
      atomic_or(&sieve[offset.z], bit.z);
      atomic_or(&sieve[offset.w], bit.w);        
      vpos += add;
    }    
      
    if (vpos.x < SIZE*32)
      atomic_or(&sieve[vpos.x >> 5], orb << vpos.x);
    if (vpos.y < SIZE*32)
      atomic_or(&sieve[vpos.y >> 5], orb << vpos.y);
    if (vpos.z < SIZE*32)
      atomic_or(&sieve[vpos.z >> 5], orb << vpos.z);
  }
  
  __global const uint2* pprimes = &primes[id];
  __global const uint* poffset = &offset[id];
  __local uint8_t *sieve8 = (__local uint8_t*)sieve;
  
  uint plifo[NLIFO];
  uint fiplifo[NLIFO];
  uint olifo[NLIFO];

  for(int i = 0; i < NLIFO; ++i){
    pprimes += LSIZE;
    poffset += LSIZE;
    
    const uint2 tmp = *pprimes;
    plifo[i] = tmp.x;
    fiplifo[i] = tmp.y;
    olifo[i] = *poffset;
  }
  
  uint lpos = 0;
  
#pragma unroll
  for(uint ip = 1; ip < SCOUNT/LSIZE; ++ip) {
    const uint prime = plifo[lpos];
    const float fiprime = as_float(fiplifo[lpos]);
    uint pos = olifo[lpos];
    
    pos = mad24((uint)(fentry * fiprime), prime, pos) - entry;
    pos = mad24((uint)((int)pos < (int)0), prime, pos);
    
    uint index = pos >> 5;
    
    if(ip < sieveRanges[0]){
      uint2 vpos = {pos,
                    mad24(1u, prime, pos)};
        
      const uint add = 2*prime;                    
      while (vpos.y < SIZE*32) {
        uint2 bit = (uint2){1u, 1u} << vpos;
        uint2 offset = vpos >> 5;
        atomic_or(&sieve[offset.x], bit.x);
        atomic_or(&sieve[offset.y], bit.y);
        vpos += add;
      }    
        
      if (vpos.x < SIZE*32)
        atomic_or(&sieve[vpos.x >> 5], 1u << vpos.x);
    } else if (ip < sieveRanges[1]) {
      if(index < SIZE){
        atomic_or(&sieve[index], 1u << pos);
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomic_or(&sieve[index], 1u << pos);
          pos += prime;
          index = pos >> 5;
          if(index < SIZE){
            atomic_or(&sieve[index], 1u << pos);
          }
        }
      }
    } else if(ip < sieveRanges[2]) {
      if(index < SIZE){
        atomic_or(&sieve[index], 1u << pos);
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomic_or(&sieve[index], 1u << pos);
        }
      }
    } else {
      if(index < SIZE){
        atomic_or(&sieve[index], 1u << pos);
      }
    }
    
    if(ip+NLIFO < SCOUNT/LSIZE){
      pprimes += LSIZE;
      poffset += LSIZE;
      
      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      fiplifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }
    
    lpos++;
    lpos = lpos % NLIFO;
  }
  
  barrier(CLK_LOCAL_MEM_FENCE);
  
  __global uint *gsieve = &gsieve_all[SIZE*(STRIPES/2*line + stripe)];
  for (uint i = id; i < SIZE; i += LSIZE)
    gsieve[i] = sieve[i];
}

#else

__attribute__((reqd_work_group_size(LSIZE, 1, 1)))
__kernel void sieve(  __global uint* gsieve_all,
            __global const uint* offset_all,
            __global const uint2* primes)
{
  
  __local uint sieve[SIZE];
  
  const uint id = get_local_id(0);
  const uint stripe = get_group_id(0);
  const uint line = get_group_id(1);
  
  const uint entry = SIZE*32*(stripe+STRIPES/2);
  const float fentry = entry;
  
  __global const uint* offset = &offset_all[PCOUNT*line];
  
  for(uint i = 0; i < SIZE/LSIZE; ++i)
    sieve[i*LSIZE+id] = 0;
  barrier(CLK_LOCAL_MEM_FENCE);
  
  {
    uint lprime[2];
    float lfiprime[2];
    uint lpos[2];
   
    uint poff = 0;
    uint nps = nps_all[0];
    uint ip = id >> (LSIZELOG2-nps);
   
    const uint2 tmp1 = primes[poff+ip];
    lprime[0] = tmp1.x;
    lfiprime[0] = as_float(tmp1.y);
    lpos[0] = offset[poff+ip];

#pragma unroll
    for(int b = 0; b < S1RUNS; b++) {
      const uint var = LSIZE >> nps;
      const uint lpoff = id & (var-1);
     
      if (b < S1RUNS-1) {
        poff += 1u << nps;
        nps = nps_all[b+1];
        ip = id >> (LSIZELOG2-nps);
       
        const uint2 tmp2 = primes[poff+ip];
        lprime[(b+1)%2] = tmp2.x;
        lfiprime[(b+1)%2] = as_float(tmp2.y);
        lpos[(b+1)%2] = offset[poff+ip];
      }
     
      const uint prime = lprime[b%2];
      const float fiprime = lfiprime[b%2];
      uint pos = lpos[b%2];
      
      pos = mad24((uint)(fentry * fiprime), prime, pos) - entry;
      pos = mad24((uint)((int)pos < (int)0), prime, pos);
      pos = mad24(lpoff, prime, pos);
      
      // This cycle must give best performance in theory.. but only in theory
      //for (unsigned i = 0; i < count[b]; i++) {
      //  atomic_or(&sieve[pos >> 5], 1u << pos);
      //  pos = mad24(var, prime, pos);
      //}

      uint32_t sieve32 = (uint32_t)sieve + pos;      
      uint4 vpos = {sieve32,
                    mad24(var, prime, sieve32),
                    mad24(var*2, prime, sieve32),
                    mad24(var*3, prime, sieve32)};
      
      while (vpos.x < SIZE*32) {
        uint4 ptr = vpos >> 3;
        uint4 bit = (uint4){1u, 1u, 1u, 1u} << vpos;
        
        atomic_or((__local uint32_t*)ptr.x, bit.x);
        atomic_or((__local uint32_t*)ptr.y, bit.y);
        atomic_or((__local uint32_t*)ptr.z, bit.z);
        atomic_or((__local uint32_t*)ptr.w, bit.w);
        
        vpos = mad24(var*4, prime, vpos);
      }
    }
  }
  
  __global const uint2* pprimes = &primes[id];
  __global const uint* poffset = &offset[id];
  __local uint8_t *sieve8 = (__local uint8_t*)sieve;
  
  uint plifo[NLIFO];
  uint fiplifo[NLIFO];
  uint olifo[NLIFO];
  
  for(int i = 0; i < NLIFO; ++i){
    pprimes += LSIZE;
    poffset += LSIZE;
    
    const uint2 tmp = *pprimes;
    plifo[i] = tmp.x;
    fiplifo[i] = tmp.y;
    olifo[i] = *poffset;
  }
  
  uint lpos = 0;
  
#pragma unroll
  for(uint ip = 1; ip < sieveRanges[2]; ++ip){
    const uint prime = plifo[lpos];
    const float fiprime = as_float(fiplifo[lpos]);
    uint pos = olifo[lpos];
    
    pos = mad24((uint)(fentry * fiprime), prime, pos) - entry;
    pos = mad24((uint)((int)pos < (int)0), prime, pos);

    if (ip < sieveRanges[0]) {
      while (pos < SIZE*32) {
        atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos); pos = mad24(1u, prime, pos);
        atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos); pos = mad24(1u, prime, pos);
        atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos); pos = mad24(1u, prime, pos);
        atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos); pos = mad24(1u, prime, pos);        
      }
    } else if(ip < sieveRanges[1]) {
      atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
      pos += prime;
      atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
      pos += prime;
      atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
    } else if(ip < sieveRanges[2]) {
      atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
      pos += prime;
      atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
    }
    
    if(ip+NLIFO < SCOUNT/LSIZE){
      
      pprimes += LSIZE;
      poffset += LSIZE;
      
      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      fiplifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
      
    }
    
    lpos++;
    lpos = lpos % NLIFO;
  }
  
  barrier(CLK_LOCAL_MEM_FENCE);

#pragma unroll
  for(uint ip = sieveRanges[2]; ip < SCOUNT/LSIZE; ++ip){
    
    const uint prime = plifo[lpos];
    const float fiprime = as_float(fiplifo[lpos]);
    uint pos = olifo[lpos];
    
    pos = mad24((uint)(fentry * fiprime), prime, pos) - entry;
    pos = mad24((uint)((int)pos < (int)0), prime, pos);

    atomic_or((__local uint32_t*)&sieve8[pos >> 3], 1u << pos);
    
    if(ip+NLIFO < SCOUNT/LSIZE){
      
      pprimes += LSIZE;
      poffset += LSIZE;
      
      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      fiplifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }
    
    lpos++;
    lpos = lpos % NLIFO;
  } 
  
  barrier(CLK_LOCAL_MEM_FENCE);
  
  __global uint *gsieve = &gsieve_all[SIZE*(STRIPES/2*line + stripe)];
  for (uint i = id; i < SIZE; i += LSIZE)
    gsieve[i] = sieve[i];
}

#endif

__kernel void s_sieve(	__global const uint* gsieve1,
						__global const uint* gsieve2,
						__global fermat_t* found320,
            __global fermat_t* found352,
						__global uint* fcount,
						uint hashid,
            uint hashSize,
            uint depth)
{
	
	const uint id = get_global_id(0);
	
	uint tmp1[WIDTH];
#pragma unroll  
	for(int i = 0; i < WIDTH; ++i)
		tmp1[i] = gsieve1[SIZE*STRIPES/2*i + id];
	
#pragma unroll
	for(int start = 0; start <= WIDTH-TARGET; ++start){
		
		uint mask = 0;
#pragma unroll    
		for(int line = 0; line < TARGET; ++line)
			mask |= tmp1[start+line];
		
		if(mask != 0xFFFFFFFF) {
      unsigned bit = 31-clz(~mask);
      unsigned multiplier = mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-clz(multiplier)) + start + depth;
      const uint addr = atomic_inc(&fcount[(maxSize <= 320) ? 0 : 1]);     
      __global fermat_t *found = (maxSize <= 320) ? found320 : found352;
					
			fermat_t info;
      info.index = multiplier;
			info.origin = start;
			info.chainpos = 0;
			info.type = 0;
			info.hashid = hashid;
      found[addr] = info;
    }
	}
	
	uint tmp2[WIDTH];
#pragma unroll  
	for(int i = 0; i < WIDTH; ++i)
		tmp2[i] = gsieve2[SIZE*STRIPES/2*i + id];
	
#pragma unroll  
	for(int start = 0; start <= WIDTH-TARGET; ++start){
		
		uint mask = 0;
#pragma unroll    
		for(int line = 0; line < TARGET; ++line)
			mask |= tmp2[start+line];
		
		if(mask != 0xFFFFFFFF) {
      unsigned bit = 31-clz(~mask);
      unsigned multiplier = mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-clz(multiplier)) + start + depth;
      const uint addr = atomic_inc(&fcount[(maxSize <= 320) ? 0 : 1]);     
      __global fermat_t *found = (maxSize <= 320) ? found320 : found352;
					
			fermat_t info;
      info.index = multiplier;
			info.origin = start;
			info.chainpos = 0;
			info.type = 1;
			info.hashid = hashid;
      found[addr] = info;
    }
	}
	
#pragma unroll	
	for(int i = 0; i < WIDTH; ++i)
    tmp2[i] |= tmp1[i];	
#pragma unroll  
	for(int start = 0; start <= WIDTH-TARGET/2; ++start){
		
		uint mask = 0;
#pragma unroll    
		for(int line = 0; line < TARGET/2; ++line)
			mask |= tmp2[start+line];
		
		if(TARGET & 1u && (start+TARGET/2) < WIDTH)
			mask |= tmp1[start+TARGET/2];
		
		if(mask != 0xFFFFFFFF) {
      unsigned bit = 31-clz(~mask);
      unsigned multiplier = mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-clz(multiplier)) + start + (depth/2) + (depth&1);
      const uint addr = atomic_inc(&fcount[(maxSize <= 320) ? 0 : 1]);     
      __global fermat_t *found = (maxSize <= 320) ? found320 : found352;
					
			fermat_t info;
      info.index = multiplier;
			info.origin = start;
			info.chainpos = 0;
			info.type = 2;
			info.hashid = hashid;
      found[addr] = info;
    }
	}
}
