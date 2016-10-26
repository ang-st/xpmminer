/*
 * fermat.cl
 *
 *  Created on: 26.12.2013
 *      Author: mad
 */





#define N 12
#define SCOUNT PCOUNT


__constant uint pow2[9] = {1, 2, 4, 8, 16, 32, 64, 128, 256};

__constant uint32_t binvert_limb_table[128] = {
  0x01, 0xAB, 0xCD, 0xB7, 0x39, 0xA3, 0xC5, 0xEF,
  0xF1, 0x1B, 0x3D, 0xA7, 0x29, 0x13, 0x35, 0xDF,
  0xE1, 0x8B, 0xAD, 0x97, 0x19, 0x83, 0xA5, 0xCF,
  0xD1, 0xFB, 0x1D, 0x87, 0x09, 0xF3, 0x15, 0xBF,
  0xC1, 0x6B, 0x8D, 0x77, 0xF9, 0x63, 0x85, 0xAF,
  0xB1, 0xDB, 0xFD, 0x67, 0xE9, 0xD3, 0xF5, 0x9F,
  0xA1, 0x4B, 0x6D, 0x57, 0xD9, 0x43, 0x65, 0x8F,
  0x91, 0xBB, 0xDD, 0x47, 0xC9, 0xB3, 0xD5, 0x7F,
  0x81, 0x2B, 0x4D, 0x37, 0xB9, 0x23, 0x45, 0x6F,
  0x71, 0x9B, 0xBD, 0x27, 0xA9, 0x93, 0xB5, 0x5F,
  0x61, 0x0B, 0x2D, 0x17, 0x99, 0x03, 0x25, 0x4F,
  0x51, 0x7B, 0x9D, 0x07, 0x89, 0x73, 0x95, 0x3F,
  0x41, 0xEB, 0x0D, 0xF7, 0x79, 0xE3, 0x05, 0x2F,
  0x31, 0x5B, 0x7D, 0xE7, 0x69, 0x53, 0x75, 0x1F,
  0x21, 0xCB, 0xED, 0xD7, 0x59, 0xC3, 0xE5, 0x0F,
  0x11, 0x3B, 0x5D, 0xC7, 0x49, 0x33, 0x55, 0xFF
};


typedef struct {
  uint index;
  uint hashid;
  uchar origin;
  uchar chainpos;
  uchar type;
  uchar reserved;
} fermat_t;


typedef struct {
	
	uint N_;
	uint SIZE_;
	uint STRIPES_;
	uint WIDTH_;
	uint PCOUNT_;
	uint TARGET_;
} config_t;



__kernel void getconfig(__global config_t* conf)
{
	config_t c;
	c.N_ = N;
	c.SIZE_ = SIZE;
	c.STRIPES_ = STRIPES;
	c.WIDTH_ = WIDTH;
	c.PCOUNT_ = PCOUNT;
	c.TARGET_ = TARGET;
	*conf = c;
}

void shl32(uint32_t *data, unsigned size)
{
  #pragma unroll
  for (int j = size-1; j > 0; j--)
    data[j] = data[j-1];
  data[0] = 0;
}

void shr32(uint32_t *data, unsigned size)
{
  #pragma unroll
  for (int j = 1; j < size; j++)
    data[j-1] = data[j];
  data[size-1] = 0;
}

void shl(uint32_t *data, unsigned size, unsigned bits)
{
  #pragma unroll
  for(int i = size-1; i > 0; i--)
    data[i] = (data[i] << bits) | (data[i-1] >> (32-bits));
  
  data[0] = data[0] << bits;
}

void shr(uint32_t *data, unsigned size, unsigned bits)
{
  #pragma unroll
  for(uint i = 0; i < size-1; i++)
    data[i] = (data[i] >> bits) | (data[i+1] << (32-bits));
  data[size-1] = data[size-1] >> bits;
}

void shlreg(uint32_t *data, unsigned size, unsigned bits)
{
  for (unsigned i = 0, ie = bits/32; i < ie; i++)
    shl32(data, size);
  
  if (bits%32)
    shl(data, size, bits%32);
}


void shrreg(uint32_t *data, unsigned size, unsigned bits)
{
  for (unsigned i = 0, ie = bits/32; i < ie; i++)
    shr32(data, size);
  if (bits%32)
    shr(data, size, bits%32);
}

uint32_t add128(uint4 *A, uint4 B)
{
  *A += B; 
  uint4 carry = -convert_uint4((*A) < B);
  
  (*A).y += carry.x; carry.y += ((*A).y < carry.x);
  (*A).z += carry.y; carry.z += ((*A).z < carry.y);
  (*A).w += carry.z;
  return carry.w + ((*A).w < carry.z); 
}


uint32_t add128Carry(uint4 *A, uint4 B, uint32_t externalCarry)
{
  *A += B;
  uint4 carry = -convert_uint4((*A) < B);
  
  (*A).x += externalCarry; carry.x += ((*A).x < externalCarry);
  (*A).y += carry.x; carry.y += ((*A).y < carry.x);
  (*A).z += carry.y; carry.z += ((*A).z < carry.y);
  (*A).w += carry.z;
  return carry.w + ((*A).w < carry.z); 
}

uint32_t add256(uint4 *a0, uint4 *a1, uint4 b0, uint4 b1)
{
  return add128Carry(a1, b1, add128(a0, b0));
}

uint32_t add384(uint4 *a0, uint4 *a1, uint4 *a2, uint4 b0, uint4 b1, uint4 b2)
{
  return add128Carry(a2, b2, add128Carry(a1, b1, add128(a0, b0)));
}

uint32_t add512(uint4 *a0, uint4 *a1, uint4 *a2, uint4 *a3, uint4 b0, uint4 b1, uint4 b2, uint4 b3)
{
  return add128Carry(a3, b3, add128Carry(a2, b2, add128Carry(a1, b1, add128(a0, b0))));
}

uint32_t sub64Borrow(uint2 *A, uint2 B, uint32_t externalBorrow)
{
  uint2 borrow = -convert_uint2((*A) < B);
  *A -= B;
  
  borrow.x += (*A).x < externalBorrow; (*A).x -= externalBorrow;
  borrow.y += (*A).y < borrow.x; (*A).y -= borrow.x;
  return borrow.y;
}

uint32_t sub96Borrow(uint4 *A, uint4 B, uint32_t externalBorrow)
{
  //   uint2 borrow = -convert_uint2((*A) < B);
  uint4 borrow = {
    (*A).x < B.x,
      (*A).y < B.y,
      (*A).z < B.z,
      0
  };
  (*A).x -= B.x;
  (*A).y -= B.y;
  (*A).z -= B.z;
  
  borrow.x += (*A).x < externalBorrow; (*A).x -= externalBorrow;
  borrow.y += (*A).y < borrow.x; (*A).y -= borrow.x;
  borrow.z += (*A).z < borrow.y; (*A).z -= borrow.y;
  
  return borrow.z;
}

uint32_t sub128(uint4 *A, uint4 B)
{
  uint4 borrow = {
    (*A).x < B.x,
      (*A).y < B.y,
      (*A).z < B.z,
      (*A).w < B.w
  };
  (*A).x -= B.x;
  (*A).y -= B.y;
  (*A).z -= B.z;
  (*A).w -= B.w;  
  
  borrow.y += (*A).y < borrow.x; (*A).y -= borrow.x;
  borrow.z += (*A).z < borrow.y; (*A).z -= borrow.y;
  borrow.w += (*A).w < borrow.z; (*A).w -= borrow.z;
  return borrow.w;
}

uint32_t sub128Borrow(uint4 *A, uint4 B, uint32_t externalBorrow)
{
  uint4 borrow = -convert_uint4((*A) < B);
  *A -= B;
  
  borrow.x += (*A).x < externalBorrow; (*A).x -= externalBorrow;
  borrow.y += (*A).y < borrow.x; (*A).y -= borrow.x;
  borrow.z += (*A).z < borrow.y; (*A).z -= borrow.y;
  borrow.w += (*A).w < borrow.z; (*A).w -= borrow.z;
  return borrow.w;
}

uint32_t sub256(uint4 *a0, uint4 *a1, uint4 b0, uint4 b1)
{
  return sub128Borrow(a1, b1, sub128(a0, b0));
}

uint32_t sub320(uint4 *a0, uint4 *a1, uint2 *a2, uint4 b0, uint4 b1, uint2 b2)
{
  return sub64Borrow(a2, b2, sub128Borrow(a1, b1, sub128(a0, b0)));
}

uint32_t sub352(uint4 *a0, uint4 *a1, uint4 *a2, uint4 b0, uint4 b1, uint4 b2)
{
  return sub96Borrow(a2, b2, sub128Borrow(a1, b1, sub128(a0, b0)));
}

uint32_t sub384(uint4 *a0, uint4 *a1, uint4 *a2, uint4 b0, uint4 b1, uint4 b2)
{
  return sub128Borrow(a2, b2, sub128Borrow(a1, b1, sub128(a0, b0)));
}

uint32_t sub448(uint4 *a0, uint4 *a1, uint4 *a2, uint2 *a3, uint4 b0, uint4 b1, uint4 b2, uint2 b3)
{
  return sub64Borrow(a3, b3, sub128Borrow(a2, b2, sub128Borrow(a1, b1, sub128(a0, b0))));
}

uint32_t invert_limb(uint32_t limb)
{
  uint32_t inv = binvert_limb_table[(limb/2) & 0x7F];
  inv = 2*inv - inv*inv*limb;
  inv = 2*inv - inv*inv*limb;
  return -inv;
}

void lshiftByLimb2(uint4 *limbs1,
                   uint4 *limbs2)
{
  (*limbs2).yzw = (*limbs2).xyz; (*limbs2).x = (*limbs1).w;
  (*limbs1).yzw = (*limbs1).xyz; (*limbs1).x = 0;
}

void rshiftByLimb2(uint4 *limbs1,
                   uint4 *limbs2)
{
  (*limbs1).xyz = (*limbs1).yzw; (*limbs1).w = (*limbs2).x;
  (*limbs2).xyz = (*limbs2).yzw; (*limbs2).w = 0;
}

void lshiftByLimb3(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3)
{
  (*limbs3).yzw = (*limbs3).xyz; (*limbs3).x = (*limbs2).w;
  (*limbs2).yzw = (*limbs2).xyz; (*limbs2).x = (*limbs1).w;
  (*limbs1).yzw = (*limbs1).xyz; (*limbs1).x = 0;
}

void rshiftByLimb3(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3)
{
  (*limbs1).xyz = (*limbs1).yzw; (*limbs1).w = (*limbs2).x;
  (*limbs2).xyz = (*limbs2).yzw; (*limbs2).w = (*limbs3).x;
  (*limbs3).xyz = (*limbs3).yzw; (*limbs3).w = 0;
}

void lshiftByLimb4(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3,
                   uint4 *limbs4)
{
  (*limbs4).yzw = (*limbs4).xyz; (*limbs4).x = (*limbs3).w;  
  (*limbs3).yzw = (*limbs3).xyz; (*limbs3).x = (*limbs2).w;
  (*limbs2).yzw = (*limbs2).xyz; (*limbs2).x = (*limbs1).w;
  (*limbs1).yzw = (*limbs1).xyz; (*limbs1).x = 0;
}

void rshiftByLimb4(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3,
                   uint4 *limbs4)
{
  (*limbs1).xyz = (*limbs1).yzw; (*limbs1).w = (*limbs2).x;
  (*limbs2).xyz = (*limbs2).yzw; (*limbs2).w = (*limbs3).x;
  (*limbs3).xyz = (*limbs3).yzw; (*limbs3).w = (*limbs4).x;
  (*limbs4).xyz = (*limbs4).yzw; (*limbs4).w = 0;
}

void lshiftByLimb5(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3,
                   uint4 *limbs4,
                   uint4 *limbs5)
{
  (*limbs5).yzw = (*limbs5).xyz; (*limbs5).x = (*limbs4).w;
  (*limbs4).yzw = (*limbs4).xyz; (*limbs4).x = (*limbs3).w;  
  (*limbs3).yzw = (*limbs3).xyz; (*limbs3).x = (*limbs2).w;
  (*limbs2).yzw = (*limbs2).xyz; (*limbs2).x = (*limbs1).w;
  (*limbs1).yzw = (*limbs1).xyz; (*limbs1).x = 0;
}

void rshiftByLimb5(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3,
                   uint4 *limbs4,
                   uint4 *limbs5)
{
  (*limbs1).xyz = (*limbs1).yzw; (*limbs1).w = (*limbs2).x;
  (*limbs2).xyz = (*limbs2).yzw; (*limbs2).w = (*limbs3).x;
  (*limbs3).xyz = (*limbs3).yzw; (*limbs3).w = (*limbs4).x;
  (*limbs4).xyz = (*limbs4).yzw; (*limbs4).w = (*limbs5).x;
  (*limbs5).xyz = (*limbs5).yzw; (*limbs5).w = 0;
}

void rshiftByLimb6(uint4 *limbs1,
                   uint4 *limbs2,
                   uint4 *limbs3,
                   uint4 *limbs4,
                   uint4 *limbs5,
                   uint4 *limbs6)
{
  (*limbs1).xyz = (*limbs1).yzw; (*limbs1).w = (*limbs2).x;
  (*limbs2).xyz = (*limbs2).yzw; (*limbs2).w = (*limbs3).x;
  (*limbs3).xyz = (*limbs3).yzw; (*limbs3).w = (*limbs4).x;
  (*limbs4).xyz = (*limbs4).yzw; (*limbs4).w = (*limbs5).x;
  (*limbs5).xyz = (*limbs5).yzw; (*limbs5).w = (*limbs6).x;
  (*limbs6).xyz = (*limbs6).yzw; (*limbs6).w = 0;
}

void lshift2(uint4 *limbs1, uint4 *limbs2, unsigned count)
{
  if (!count)
    return;  
  unsigned lowBitsCount = 32 - count;  
  
  {
    uint4 lowBits = {
      (*limbs1).w >> lowBitsCount,
      (*limbs2).x >> lowBitsCount,
      (*limbs2).y >> lowBitsCount,
      (*limbs2).z >> lowBitsCount
    };
    (*limbs2) = ((*limbs2) << count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      0,
      (*limbs1).x >> lowBitsCount,
      (*limbs1).y >> lowBitsCount,
      (*limbs1).z >> lowBitsCount
    };
    (*limbs1) = ((*limbs1) << count) | lowBits;
  }
}

void rshift2(uint4 *limbs1, uint4 *limbs2, unsigned count)
{
  if (!count)
    return;  
  unsigned lowBitsCount = 32 - count;  
  
  {
    uint4 lowBits = {
      (*limbs1).y << lowBitsCount,
      (*limbs1).z << lowBitsCount,
      (*limbs1).w << lowBitsCount,
      (*limbs2).x << lowBitsCount
    };
    (*limbs1) = ((*limbs1) >> count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      (*limbs2).y << lowBitsCount,
      (*limbs2).z << lowBitsCount,
      (*limbs2).w << lowBitsCount,
      0
    };
    (*limbs2) = ((*limbs2) >> count) | lowBits;
  }  
}

void lshift3(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, unsigned count)
{
  if (!count)
    return;  
  unsigned lowBitsCount = 32 - count;
  
  {
    uint4 lowBits = {
      (*limbs2).w >> lowBitsCount,
      (*limbs3).x >> lowBitsCount,
      (*limbs3).y >> lowBitsCount,
      (*limbs3).z >> lowBitsCount
    };
    (*limbs3) = ((*limbs3) << count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs1).w >> lowBitsCount,
      (*limbs2).x >> lowBitsCount,
      (*limbs2).y >> lowBitsCount,
      (*limbs2).z >> lowBitsCount
    };
    (*limbs2) = ((*limbs2) << count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      0,
      (*limbs1).x >> lowBitsCount,
      (*limbs1).y >> lowBitsCount,
      (*limbs1).z >> lowBitsCount
    };
    (*limbs1) = ((*limbs1) << count) | lowBits;
  }
}

void rshift3(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, unsigned count)
{
  if (!count)
    return;
  unsigned lowBitsCount = 32 - count;  
  
  {
    uint4 lowBits = {
      (*limbs1).y << lowBitsCount,
      (*limbs1).z << lowBitsCount,
      (*limbs1).w << lowBitsCount,
      (*limbs2).x << lowBitsCount
    };
    (*limbs1) = ((*limbs1) >> count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs2).y << lowBitsCount,
      (*limbs2).z << lowBitsCount,
      (*limbs2).w << lowBitsCount,
      (*limbs3).x << lowBitsCount
    };
    (*limbs2) = ((*limbs2) >> count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      (*limbs3).y << lowBitsCount,
      (*limbs3).z << lowBitsCount,
      (*limbs3).w << lowBitsCount,
      0
    };
    (*limbs3) = ((*limbs3) >> count) | lowBits;
  }  
}

void lshift4(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, uint4 *limbs4, unsigned count)
{
  if (!count)
    return;  
  unsigned lowBitsCount = 32 - count;
  
  {
    uint4 lowBits = {
      (*limbs3).w >> lowBitsCount,
      (*limbs4).x >> lowBitsCount,
      (*limbs4).y >> lowBitsCount,
      (*limbs4).z >> lowBitsCount
    };
    (*limbs4) = ((*limbs4) << count) | lowBits;
  }      
  
  {
    uint4 lowBits = {
      (*limbs2).w >> lowBitsCount,
      (*limbs3).x >> lowBitsCount,
      (*limbs3).y >> lowBitsCount,
      (*limbs3).z >> lowBitsCount
    };
    (*limbs3) = ((*limbs3) << count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs1).w >> lowBitsCount,
      (*limbs2).x >> lowBitsCount,
      (*limbs2).y >> lowBitsCount,
      (*limbs2).z >> lowBitsCount
    };
    (*limbs2) = ((*limbs2) << count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      0,
      (*limbs1).x >> lowBitsCount,
      (*limbs1).y >> lowBitsCount,
      (*limbs1).z >> lowBitsCount
    };
    (*limbs1) = ((*limbs1) << count) | lowBits;
  }
}

void lshift5(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, uint4 *limbs4, uint4 *limbs5, unsigned count)
{
  if (!count)
    return;  
  unsigned lowBitsCount = 32 - count;
  
  {
    uint4 lowBits = {
      (*limbs4).w >> lowBitsCount,
      (*limbs5).x >> lowBitsCount,
      (*limbs5).y >> lowBitsCount,
      (*limbs5).z >> lowBitsCount
    };
    (*limbs5) = ((*limbs5) << count) | lowBits;
  }      
  
  {
    uint4 lowBits = {
      (*limbs3).w >> lowBitsCount,
      (*limbs4).x >> lowBitsCount,
      (*limbs4).y >> lowBitsCount,
      (*limbs4).z >> lowBitsCount
    };
    (*limbs4) = ((*limbs4) << count) | lowBits;
  }      
  
  {
    uint4 lowBits = {
      (*limbs2).w >> lowBitsCount,
      (*limbs3).x >> lowBitsCount,
      (*limbs3).y >> lowBitsCount,
      (*limbs3).z >> lowBitsCount
    };
    (*limbs3) = ((*limbs3) << count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs1).w >> lowBitsCount,
      (*limbs2).x >> lowBitsCount,
      (*limbs2).y >> lowBitsCount,
      (*limbs2).z >> lowBitsCount
    };
    (*limbs2) = ((*limbs2) << count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      0,
      (*limbs1).x >> lowBitsCount,
      (*limbs1).y >> lowBitsCount,
      (*limbs1).z >> lowBitsCount
    };
    (*limbs1) = ((*limbs1) << count) | lowBits;
  }
}


void rshift4(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, uint4 *limbs4, unsigned count)
{
  if (!count)
    return;
  unsigned lowBitsCount = 32 - count;  
  
  {
    uint4 lowBits = {
      (*limbs1).y << lowBitsCount,
      (*limbs1).z << lowBitsCount,
      (*limbs1).w << lowBitsCount,
      (*limbs2).x << lowBitsCount
    };
    (*limbs1) = ((*limbs1) >> count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs2).y << lowBitsCount,
      (*limbs2).z << lowBitsCount,
      (*limbs2).w << lowBitsCount,
      (*limbs3).x << lowBitsCount
    };
    (*limbs2) = ((*limbs2) >> count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      (*limbs3).y << lowBitsCount,
      (*limbs3).z << lowBitsCount,
      (*limbs3).w << lowBitsCount,
      (*limbs4).x << lowBitsCount,
    };
    (*limbs3) = ((*limbs3) >> count) | lowBits;
  }
  
  {
    uint4 lowBits = {
      (*limbs4).y << lowBitsCount,
      (*limbs4).z << lowBitsCount,
      (*limbs4).w << lowBitsCount,
      0
    };
    (*limbs4) = ((*limbs4) >> count) | lowBits;
  }    
}


void rshift5(uint4 *limbs1, uint4 *limbs2, uint4 *limbs3, uint4 *limbs4, uint4 *limbs5, unsigned count)
{
  if (!count)
    return;
  unsigned lowBitsCount = 32 - count;  
  
  {
    uint4 lowBits = {
      (*limbs1).y << lowBitsCount,
      (*limbs1).z << lowBitsCount,
      (*limbs1).w << lowBitsCount,
      (*limbs2).x << lowBitsCount
    };
    (*limbs1) = ((*limbs1) >> count) | lowBits;
  }    
  
  {
    uint4 lowBits = {
      (*limbs2).y << lowBitsCount,
      (*limbs2).z << lowBitsCount,
      (*limbs2).w << lowBitsCount,
      (*limbs3).x << lowBitsCount
    };
    (*limbs2) = ((*limbs2) >> count) | lowBits;
  }  
  
  {
    uint4 lowBits = {
      (*limbs3).y << lowBitsCount,
      (*limbs3).z << lowBitsCount,
      (*limbs3).w << lowBitsCount,
      (*limbs4).x << lowBitsCount,
    };
    (*limbs3) = ((*limbs3) >> count) | lowBits;
  }
  
  {
    uint4 lowBits = {
      (*limbs4).y << lowBitsCount,
      (*limbs4).z << lowBitsCount,
      (*limbs4).w << lowBitsCount,
      (*limbs5).x << lowBitsCount,
    };
    (*limbs4) = ((*limbs4) >> count) | lowBits;
  }
  
  {
    uint4 lowBits = {
      (*limbs5).y << lowBitsCount,
      (*limbs5).z << lowBitsCount,
      (*limbs5).w << lowBitsCount,
      0
    };
    (*limbs5) = ((*limbs5) >> count) | lowBits;
  }    
}

// // Calculate (384bit){b2, b1, b0} -= (384bit)({a2, a1, a0} >> 32) * (32bit)M, returns highest product limb
void subMul1_v3(uint4 *b0, uint4 *b1, uint4 *b2, uint4 *b3,
                uint4 a0, uint4 a1, uint4 a2,
                uint32_t M)
{
  #define bringBorrow(Data, Borrow, NextBorrow) NextBorrow += (Data < Borrow); Data -= Borrow;
  
  uint4 Mv4 = {M, M, M, M};
  uint32_t clow;
  uint4 c1 = {0, 0, 0, 0};
  uint4 c2 = {0, 0, 0, 0};
  uint4 c3 = {0, 0, 0, 0};
  
  {
    uint4 a0M = a0*Mv4;
    uint4 a0Mhi = mul_hi(a0, Mv4);
    
    clow = (*b0).w < a0M.x;
    (*b0).w -= a0M.x;
    
    c1.xyz -= convert_uint3((*b1).xyz < a0M.yzw);
    (*b1).xyz -= a0M.yzw;
    
    c1 -= convert_uint4((*b1) < a0Mhi);
    (*b1) -= a0Mhi;
  }
  
  {
    uint4 a1M = a1*Mv4;
    uint4 a1Mhi = mul_hi(a1, Mv4);
    
    c1.w += ((*b1).w < a1M.x);
    (*b1).w -= a1M.x;
    
    c2.xyz -= convert_uint3((*b2).xyz < a1M.yzw);
    (*b2).xyz -= a1M.yzw;
    
    c2 -= convert_uint4((*b2) < a1Mhi);
    (*b2) -= a1Mhi;
  }
  
  {
    uint4 a2M = a2*Mv4;
    uint4 a2Mhi = mul_hi(a2, Mv4);
    
    c2.w += ((*b2).w < a2M.x);
    (*b2).w -= a2M.x;
    
    c3.xyz -= convert_uint3((*b3).xyz < a2M.yzw);
    (*b3).xyz -= a2M.yzw;
    c3 -= convert_uint4((*b3) < a2Mhi);
    (*b3) -= a2Mhi;
  }
  
  bringBorrow((*b1).x, clow, c1.x);
  bringBorrow((*b1).y, c1.x, c1.y);
  bringBorrow((*b1).z, c1.y, c1.z);
  bringBorrow((*b1).w, c1.z, c1.w);
  bringBorrow((*b2).x, c1.w, c2.x);
  bringBorrow((*b2).y, c2.x, c2.y);
  bringBorrow((*b2).z, c2.y, c2.z);
  bringBorrow((*b2).w, c2.z, c2.w);
  bringBorrow((*b3).x, c2.w, c3.x);
  bringBorrow((*b3).y, c3.x, c3.y);
  bringBorrow((*b3).z, c3.y, c3.z);
  bringBorrow((*b3).w, c3.z, c3.w);
  #undef bringBorrow
}

uint2 modulo512to384(uint4 dividendLimbs0,
                     uint4 dividendLimbs1,
                     uint4 dividendLimbs2,
                     uint4 dividendLimbs3,
                     uint4 divisorLimbs0,
                     uint4 divisorLimbs1,
                     uint4 divisorLimbs2,
                     uint4 *moduloLimbs0,
                     uint4 *moduloLimbs1,
                     uint4 *moduloLimbs2)
{
  // Detect dividend and divisor limbs count (remove trailing zero limbs)
  unsigned dividendLimbs = 16;
  unsigned divisorLimbs = 12;
  
  while (divisorLimbs && !divisorLimbs2.w) {
    lshiftByLimb3(&divisorLimbs0, &divisorLimbs1, &divisorLimbs2);
    divisorLimbs--;
  }  
  
  // Normalize dividend and divisor (high bit of divisor must be set to 1)
  unsigned normalizeShiftCount = 0;  
  uint32_t bit = 0x80000000;
  while (!(divisorLimbs2.w & bit)) {
    normalizeShiftCount++;
    bit >>= 1;  
  }    
  
  lshift4(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3, normalizeShiftCount);
  lshift3(&divisorLimbs0, &divisorLimbs1, &divisorLimbs2, normalizeShiftCount);    
  
  
  while (dividendLimbs && !dividendLimbs3.w) {
    lshiftByLimb4(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3);
    dividendLimbs--;
  }  
  
  for (unsigned i = 0; i < (dividendLimbs - divisorLimbs); i++) {
    uint32_t i32quotient;
    if (dividendLimbs3.w == divisorLimbs2.w) {
      i32quotient = 0xFFFFFFFF;
    } else {
      uint64_t i64dividend = (((uint64_t)dividendLimbs3.w) << 32) | dividendLimbs3.z;
      i32quotient = i64dividend / divisorLimbs2.w;
    }
    
    subMul1_v3(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3,
               divisorLimbs0, divisorLimbs1, divisorLimbs2,
               i32quotient);    
    uint32_t borrow = dividendLimbs3.w;    
    lshiftByLimb4(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3);
    if (borrow) {
      add384(&dividendLimbs1, &dividendLimbs2, &dividendLimbs3, divisorLimbs0, divisorLimbs1, divisorLimbs2);
      if (dividendLimbs3.w > divisorLimbs2.w)
        add384(&dividendLimbs1, &dividendLimbs2, &dividendLimbs3, divisorLimbs0, divisorLimbs1, divisorLimbs2);
    }
  }
  
  rshift4(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3, normalizeShiftCount);
  for (unsigned i = 0; i < (12-divisorLimbs); i++)
    rshiftByLimb4(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3);
  
  *moduloLimbs0 = dividendLimbs1;
  *moduloLimbs1 = dividendLimbs2;
  *moduloLimbs2 = dividendLimbs3;
  return (uint2){divisorLimbs, 32-normalizeShiftCount};
}


uint2 divq640to384(uint4 dividendLimbs0,
                   uint4 dividendLimbs1,
                   uint4 dividendLimbs2,
                   uint4 dividendLimbs3,
                   uint4 dividendLimbs4,
                   uint4 divisorLimbs0,
                   uint4 divisorLimbs1,
                   uint4 divisorLimbs2,
                   uint4 *q0,
                   uint4 *q1)
{
  // Detect dividend and divisor limbs count (remove trailing zero limbs)
  unsigned dividendLimbs = 20;
  unsigned divisorLimbs = 12;
  
  while (divisorLimbs && !divisorLimbs2.w) {
    lshiftByLimb3(&divisorLimbs0, &divisorLimbs1, &divisorLimbs2);
    divisorLimbs--;
  }  
  
  // Normalize dividend and divisor (high bit of divisor must be set to 1)
  unsigned normalizeShiftCount = 0;  
  uint32_t bit = 0x80000000;
  while (!(divisorLimbs2.w & bit)) {
    normalizeShiftCount++;
    bit >>= 1;  
  }    
  
  lshift5(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3, &dividendLimbs4, normalizeShiftCount);
  lshift3(&divisorLimbs0, &divisorLimbs1, &divisorLimbs2, normalizeShiftCount);    
  
  
  while (dividendLimbs && !dividendLimbs4.w) {
    lshiftByLimb5(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3, &dividendLimbs4);
    dividendLimbs--;
  }  
  
  for (unsigned i = 0; i < (dividendLimbs - divisorLimbs); i++) {
    uint32_t i32quotient;
    if (dividendLimbs4.w == divisorLimbs2.w) {
      i32quotient = 0xFFFFFFFF;
    } else {
      uint64_t i64dividend = (((uint64_t)dividendLimbs4.w) << 32) | dividendLimbs4.z;
      i32quotient = i64dividend / divisorLimbs2.w;
    }
    
    subMul1_v3(&dividendLimbs1, &dividendLimbs2, &dividendLimbs3, &dividendLimbs4,
               divisorLimbs0, divisorLimbs1, divisorLimbs2,
               i32quotient);    
    uint32_t borrow = dividendLimbs4.w;
    lshiftByLimb5(&dividendLimbs0, &dividendLimbs1, &dividendLimbs2, &dividendLimbs3, &dividendLimbs4);
    if (borrow) {
      i32quotient--;
      add384(&dividendLimbs2, &dividendLimbs3, &dividendLimbs4, divisorLimbs0, divisorLimbs1, divisorLimbs2);
      if (dividendLimbs4.w > divisorLimbs2.w) {
        i32quotient--;        
        add384(&dividendLimbs2, &dividendLimbs3, &dividendLimbs4, divisorLimbs0, divisorLimbs1, divisorLimbs2);
      }
    }
    
    lshiftByLimb2(q0, q1);
    (*q0).x = i32quotient;
}

return (uint2){divisorLimbs, 32-normalizeShiftCount};
}

void redcify352(unsigned shiftCount,
                uint4 *quotient,
                uint4 *limbs,
                uint32_t *result,
                uint32_t windowSize)
{
  uint4 q[2];
  q[0] = quotient[0];
  q[1] = quotient[1];
  const unsigned pow2ws = pow2[windowSize];  
  
  for (unsigned  i = 0, ie = (pow2ws-shiftCount)/32; i < ie; i++)
    rshiftByLimb2(&q[0], &q[1]);
  rshift2(&q[0], &q[1], (pow2ws-shiftCount) % 32);

  if (windowSize == 5)
    mulProductScan352to96(result, limbs, q);
  else if (windowSize == 6)
    mulProductScan352to128(result, limbs, q);
  else if (windowSize == 7)
    mulProductScan352to192(result, limbs, q);
  
  // substract 2^(384+shiftCount) - q*R
  for (unsigned i = 0; i < 11; i++)
    result[i] = ~result[i];
  result[0]++;
}

#if defined(__NVIDIA) || defined(__AMDLEGACY)
void FermatTest352(uint4 *restrict limbs,
                   uint4 *redcl)
{
  uint2 bitSize;
  //   uint4 redcl0, redcl1, redcl2;
  uint32_t inverted = invert_limb(limbs[0].x);  
  
  //   uint4 q0 = 0, q1 = 0;  
  uint4 q[2] = {0, 0};
  {
    uint4 dl4 = {0, 0, 0, 0};    
    uint4 dl3 = {0, 0, 0, 1};
    uint4 dl2 = {0, 0, 0, 0};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};
    divq640to384(dl0, dl1, dl2, dl3, dl4, limbs[0], limbs[1], limbs[2], &q[0], &q[1]);
  }
  
  
  // Retrieve of "2" in Montgomery representation
  {
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 0, 2};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};    
    bitSize = modulo512to384(dl0, dl1, dl2, dl3, limbs[0], limbs[1], limbs[2], &redcl[0], &redcl[1], &redcl[2]);
    --bitSize.y;
    if (bitSize.y == 0) {
      --bitSize.x;
      bitSize.y = 32;
    }
  }
  
  const int windowSize = 7;  
  uint32_t *data = (uint32_t*)limbs;
  int remaining = (bitSize.x-1)*32 + bitSize.y;
  
  uint32_t e[11];
  for (unsigned i = 0; i < 11; i++)
    e[i] = data[i];
  e[0]--;
  shlreg(e, 11, 352-remaining);    
  
  while (remaining > 0) {
    int size = min(remaining, windowSize);
    int index = e[10] >> (32-size);
    
    uint4 m[3];
    for (unsigned i = 0; i < size; i++)
      monSqr352(redcl, limbs, inverted);
    
    redcify352(index, q, limbs, m, windowSize);    
    monMul352(redcl, m, limbs, inverted);
    shl(e, 11, size);
    remaining -= windowSize;
  }
  
  redcHalf352(redcl, limbs, inverted);
}

#else

void FermatTest352(uint4 *restrict limbs,
                      uint4 *redcl)
{
  uint2 bitSize;
  uint32_t inverted = invert_limb(limbs[0].x);  
  
  uint4 q[2];
  q[0] = 0;
  q[1] = 0;
  
  {
    uint4 dl4 = {0, 0, 0, 0};    
    uint4 dl3 = {0, 0, 0, 1};
    uint4 dl2 = {0, 0, 0, 0};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};
    divq640to384(dl0, dl1, dl2, dl3, dl4, limbs[0], limbs[1], limbs[2], &q[0], &q[1]);
  }
  
  
  // Retrieve of "2" in Montgomery representation
  {
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 0, 2};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};    
    bitSize = modulo512to384(dl0, dl1, dl2, dl3, limbs[0], limbs[1], limbs[2], &redcl[0], &redcl[1], &redcl[2]);
    --bitSize.y;
    if (bitSize.y == 0) {
      --bitSize.x;
      bitSize.y = 32;
    }
  }
  
  const int windowSize = 7;  
  int remaining = (bitSize.x-1)*32 + bitSize.y;
  
  uint32_t data[12];
  for (unsigned i = 0; i < 11; i++)
    data[i] = ((uint32_t*)limbs)[i];  
  data[0]--;  
  
  while (remaining > 0) {
    int bitPos = max(remaining-windowSize, 0);
    int size = min(remaining, windowSize);
    
    uint64_t v64 = *(uint64_t*)(data+bitPos/32);
    v64 >>= bitPos % 32;
    uint32_t index = ((uint32_t)v64) & ((1 << size) - 1);

    uint4 m[3];
    for (unsigned i = 0; i < size; i++)
      monSqr352(redcl, limbs, inverted);

    redcify352(index, q, limbs, m, windowSize);    
    monMul352(redcl, m, limbs, inverted);
    remaining -= windowSize;
  }

  redcHalf352(redcl, limbs, inverted);
}

#endif

void redcify320(unsigned shiftCount,
                uint4 *quotient,
                uint4 *limbs,
                uint32_t *result,
                uint32_t windowSize)
{
  uint4 q[2];
  q[0] = quotient[0];
  q[1] = quotient[1];
  
  const unsigned pow2ws = pow2[windowSize];
  for (unsigned  i = 0, ie = (pow2ws-shiftCount)/32; i < ie; i++)
    rshiftByLimb2(&q[0], &q[1]);
  rshift2(&q[0], &q[1], (pow2ws-shiftCount) % 32);

  if (windowSize == 5)
    mulProductScan320to96(result, limbs, q);  
  else if (windowSize == 6)
    mulProductScan320to128(result, limbs, q);
  else if (windowSize == 7)
    mulProductScan320to192(result, limbs, q);
  
  // substract 2^(384+shiftCount) - q*R
  for (unsigned i = 0; i < 10; i++)
    result[i] = ~result[i];
  result[0]++;
}

#if defined(__NVIDIA) || defined(__AMDLEGACY)
void FermatTest320(uint4 *restrict limbs, uint4 *redcl)
{
  uint2 bitSize;
  uint32_t inverted = invert_limb(limbs[0].x);  
  
  //   uint4 q0 = 0, q1 = 0;  
  uint4 q[2] = {0, 0};
  q[0] = 0;
  q[1] = 0;
  {
    uint4 dl4 = {0, 0, 0, 0};    
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 0, 1};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};
    divq640to384(dl0, dl1, dl2, dl3, dl4, limbs[0], limbs[1], limbs[2], &q[0], &q[1]);
  }
  
  
  // Retrieve of "2" in Montgomery representation
  {
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 2, 0};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};    
    bitSize = modulo512to384(dl0, dl1, dl2, dl3, limbs[0], limbs[1], limbs[2], &redcl[0], &redcl[1], &redcl[2]);
    --bitSize.y;
    if (bitSize.y == 0) {
      --bitSize.x;
      bitSize.y = 32;
    }
  }
  
  uint32_t *data = (uint32_t*)limbs;
  int remaining = (bitSize.x-1)*32 + bitSize.y;
  
  const int windowSize = 5;
  uint32_t e[10];
  for (unsigned i = 0; i < 10; i++)
    e[i] = data[i];
  e[0]--;
  shlreg(e, 10, 320-remaining);  
  
  int counter = 0;
  while (remaining > 0) {
    int size = min(remaining, windowSize);
    int index = e[9] >> (32-size);
    
    uint4 m[3];
    for (unsigned i = 0; i < size; i++)
      monSqr320(redcl, limbs, inverted);
    
    redcify320(index, q, limbs, m, windowSize);
    monMul320(redcl, m, limbs, inverted);   
    
    shl(e, 10, size);    
    remaining -= windowSize;
  }
  
  redcHalf320(redcl, limbs, inverted);
}

#else

void FermatTest320(uint4 *restrict limbs, uint4 *redcl)
{
  uint2 bitSize;
  uint32_t inverted = invert_limb(limbs[0].x);  

  uint4 q[2];
  q[0] = 0;
  q[1] = 0;
  
  {
    uint4 dl4 = {0, 0, 0, 0};    
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 0, 1};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};
    divq640to384(dl0, dl1, dl2, dl3, dl4, limbs[0], limbs[1], limbs[2], &q[0], &q[1]);
  }
  
  
  // Retrieve of "2" in Montgomery representation
  {
    uint4 dl3 = {0, 0, 0, 0};
    uint4 dl2 = {0, 0, 2, 0};
    uint4 dl1 = {0, 0, 0, 0};
    uint4 dl0 = {0, 0, 0, 0};    
    bitSize = modulo512to384(dl0, dl1, dl2, dl3, limbs[0], limbs[1], limbs[2], &redcl[0], &redcl[1], &redcl[2]);
    --bitSize.y;
    if (bitSize.y == 0) {
      --bitSize.x;
      bitSize.y = 32;
    }
  }

  int remaining = (bitSize.x-1)*32 + bitSize.y;
  const int windowSize = 5;
  
  uint32_t data[11];
  for (unsigned i = 0; i < 10; i++)
    data[i] = ((uint32_t*)limbs)[i];  
  data[0]--;  
  
  while (remaining > 0) {
    int bitPos = max(remaining-windowSize, 0);
    int size = min(remaining, windowSize);
    
    uint64_t v64 = *(uint64_t*)(data+bitPos/32);
    v64 >>= bitPos % 32;
    uint32_t index = ((uint32_t)v64) & ((1 << size) - 1);

    uint4 m[3];
    for (unsigned i = 0; i < size; i++)
      monSqr320(redcl, limbs, inverted);
    redcify320(index, q, limbs, m, windowSize);
    monMul320(redcl, m, limbs, inverted);   
    remaining -= windowSize;
  }
  
  redcHalf320(redcl, limbs, inverted);
}

#endif

bool fermat352(const uint* p) {
  uint4 modpowl[3];
  FermatTest352((const uint4*)p, modpowl);
  
  --modpowl[0].x;
  modpowl[0] |= modpowl[1];
  modpowl[0].xy |= modpowl[0].zw;
  modpowl[0].x |= modpowl[0].y;
  modpowl[0].x |= modpowl[2].x;
  modpowl[0].x |= modpowl[2].y;  
  modpowl[0].x |= modpowl[2].z;  
  return modpowl[0].x == 0;
}

bool fermat320(const uint* p) {
  uint4 modpowl[3];
  FermatTest320((const uint4*)p, modpowl);
  
  --modpowl[0].x;
  modpowl[0] |= modpowl[1];
  modpowl[0].xy |= modpowl[0].zw;
  modpowl[0].x |= modpowl[0].y;
  modpowl[0].x |= modpowl[2].x;
  modpowl[0].x |= modpowl[2].y;  
  return modpowl[0].x == 0;
}


uint int_invert(uint a, uint nPrime)
{
    // Extended Euclidean algorithm to calculate the inverse of a in finite field defined by nPrime
    int rem0 = nPrime, rem1 = a % nPrime, rem2;
    int aux0 = 0, aux1 = 1, aux2;
    int quotient, inverse;
    
    while (1)
    {
        if (rem1 <= 1)
        {
            inverse = aux1;
            break;
        }
        
        rem2 = rem0 % rem1;
        quotient = rem0 / rem1;
        aux2 = -quotient * aux1 + aux0;
        
        if (rem2 <= 1)
        {
            inverse = aux2;
            break;
        }
        
        rem0 = rem1 % rem2;
        quotient = rem1 / rem2;
        aux0 = -quotient * aux2 + aux1;
        
        if (rem0 <= 1)
        {
            inverse = aux0;
            break;
        }
        
        rem1 = rem2 % rem0;
        quotient = rem2 / rem0;
        aux1 = -quotient * aux0 + aux2;
    }
    
    return (inverse + nPrime) % nPrime;
}

void mul384_1(uint4 l0, uint4 l1, uint4 l2, uint32_t m,
              uint4 *r0, uint4 *r1, uint4 *r2)
{
  *r0 = l0 * m;
  *r1 = l1 * m;
  *r2 = l2 * m;
  
  uint4 h0 = mul_hi(l0, m);
  uint4 h1 = mul_hi(l1, m);
  uint4 h2 = mul_hi(l2, m);
  
  add384(r0, r1, r2,
         (uint4){0, h0.x, h0.y, h0.z},
         (uint4){h0.w, h1.x, h1.y, h1.z},
         (uint4){h1.w, h2.x, h2.y, h2.z});
}

__kernel void setup_fermat( __global uint* fprimes,
              __global const fermat_t* info_all,
              __global uint* hash )
{
  
  const uint id = get_global_id(0);
  const fermat_t info = info_all[id];
  
  uint h[N];
  uint m[N];
  uint r[2*N];
  
  __global uint *H = &hash[info.hashid*N];
  uint4 h1 = {H[0], H[1], H[2], H[3]};
  uint4 h2 = {H[4], H[5], H[6], H[7]};
  uint4 h3 = {H[8], H[9], H[10], 0};

  uint line = info.origin;
  if(info.type < 2)
    line += info.chainpos;
  else
    line += info.chainpos/2;

  uint modifier = (info.type == 1 || (info.type == 2 && (info.chainpos & 1))) ? 1 : -1;
  uint4 m1, m2, m3;  
  mul384_1(h1, h2, h3, info.index, &m1, &m2, &m3);
  lshift3(&m1, &m2, &m3, line);
  m1.x += modifier;

  fprimes[id*N + 0] = m1.x;
  fprimes[id*N + 1] = m1.y;
  fprimes[id*N + 2] = m1.z;
  fprimes[id*N + 3] = m1.w;
  fprimes[id*N + 4] = m2.x;
  fprimes[id*N + 5] = m2.y;
  fprimes[id*N + 6] = m2.z;
  fprimes[id*N + 7] = m2.w;
  fprimes[id*N + 8] = m3.x;
  fprimes[id*N + 9] = m3.y;
  fprimes[id*N + 10] = m3.z;
  fprimes[id*N + 11] = 0;  
}



__attribute__((reqd_work_group_size(64, 1, 1)))
__kernel void fermat_kernel(__global uchar* result,
							__global const uint4* fprimes )
{
	
	const uint id = get_global_id(0);

  uint4 p[3];
  p[0] = fprimes[id*3+0];
  p[1] = fprimes[id*3+1];
  p[2] = fprimes[id*3+2];
	result[id] = fermat352(p);
}

__attribute__((reqd_work_group_size(64, 1, 1)))
__kernel void fermat_kernel320(__global uchar* result,
              __global const uint4* fprimes )
{
  
  const uint id = get_global_id(0);

  uint4 p[3];
  p[0] = fprimes[id*3+0];
  p[1] = fprimes[id*3+1];
  p[2] = fprimes[id*3+2];
  result[id] = fermat320(p);
}



__kernel void check_fermat(	__global fermat_t* info_out,
							__global uint* count,
							__global fermat_t* info_fin_out,
							__global uint* count_fin,
							__global const uchar* results,
							__global const fermat_t* info_in,
							uint depth )
{
	
	const uint id = get_global_id(0);
	
	if(results[id] == 1){
		
		fermat_t info = info_in[id];
		info.chainpos++;
		
		if(info.chainpos < depth){
			
			const uint i = atomic_inc(count);
			info_out[i] = info;
			
		}else{
			
			const uint i = atomic_inc(count_fin);
			info_fin_out[i] = info;
			
		}
		
	}
	
}


uint32_t mod32(uint32_t *data, unsigned size, uint32_t *modulos, uint32_t divisor)
{
  uint64_t acc = data[0];
  for (unsigned i = 1; i < size; i++)
    acc += (uint64_t)modulos[i-1] * (uint64_t)data[i];
  return acc % divisor;
}

__kernel void setup_sieve(  __global uint* offset1,
                            __global uint* offset2,
                            __global const uint* vPrimes,
                            __global uint* hash,
                            uint hashid,
                            __global uint *modulos)
{
  
  const uint id = get_global_id(0);
  const uint nPrime = vPrimes[id];
  
  uint tmp[N];
#pragma unroll
  for(int i = 0; i < N; ++i)
    tmp[i] = hash[hashid*N + i];
  
  uint localModulos[N-2];
#pragma unroll
  for (unsigned i = 0; i < N-2; i++)
    localModulos[i] = modulos[PCOUNT*i + id];
  const uint nFixedFactorMod = mod32(tmp, N-1, localModulos, nPrime);
  
  if(nFixedFactorMod == 0){
    for(uint line = 0; line < WIDTH; ++line){
      offset1[PCOUNT*line + id] = 0; //1u << 31;
      offset2[PCOUNT*line + id] = 0; //1u << 31;
    }
    return;
    
  }
  
  uint nFixedInverse = int_invert(nFixedFactorMod, nPrime);
  for(uint layer = 0; layer < WIDTH; ++layer) {
    offset1[PCOUNT*layer + id] = nFixedInverse;
    offset2[PCOUNT*layer + id] = nPrime - nFixedInverse;
    nFixedInverse = (nFixedInverse & 0x1) ?
    (nFixedInverse + nPrime) / 2 : nFixedInverse / 2;
  }    
}
