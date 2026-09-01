#ifndef GPUKANGH
#define GPUKANGH

#ifndef NB_JUMP
#define NB_JUMP 32
#endif
#ifndef GPU_GRP_SIZE
#define GPU_GRP_SIZE 128
#endif
#ifndef NB_RUN
#define NB_RUN 64
#endif
#ifndef KSIZE
#define KSIZE 12
#endif
#ifndef ITEM_SIZE
#define ITEM_SIZE 72
#define ITEM_SIZE32 (ITEM_SIZE/4)
#endif

__device__ __constant__ uint64_t jD[NB_JUMP][4];
__device__ __constant__ uint64_t jPx[NB_JUMP][4];
__device__ __constant__ uint64_t jPy[NB_JUMP][4];

#define OutputDP(x,d,idx) {\
	out[pos*ITEM_SIZE32 + 1] = ((uint32_t *)(x))[0]; \
	out[pos*ITEM_SIZE32 + 2] = ((uint32_t *)(x))[1]; \
	out[pos*ITEM_SIZE32 + 3] = ((uint32_t *)(x))[2]; \
	out[pos*ITEM_SIZE32 + 4] = ((uint32_t *)(x))[3]; \
	out[pos*ITEM_SIZE32 + 5] = ((uint32_t *)(x))[4]; \
	out[pos*ITEM_SIZE32 + 6] = ((uint32_t *)(x))[5]; \
	out[pos*ITEM_SIZE32 + 7] = ((uint32_t *)(x))[6]; \
	out[pos*ITEM_SIZE32 + 8] = ((uint32_t *)(x))[7]; \
	out[pos*ITEM_SIZE32 + 9] = ((uint32_t *)(d))[0]; \
	out[pos*ITEM_SIZE32 + 10] = ((uint32_t *)(d))[1]; \
	out[pos*ITEM_SIZE32 + 11] = ((uint32_t *)(d))[2]; \
	out[pos*ITEM_SIZE32 + 12] = ((uint32_t *)(d))[3]; \
	out[pos*ITEM_SIZE32 + 13] = ((uint32_t *)(d))[4]; \
	out[pos*ITEM_SIZE32 + 14] = ((uint32_t *)(d))[5]; \
	out[pos*ITEM_SIZE32 + 15] = ((uint32_t *)(d))[6]; \
	out[pos*ITEM_SIZE32 + 16] = ((uint32_t *)(d))[7]; \
	out[pos*ITEM_SIZE32 + 17] = ((uint32_t *)(idx))[0]; \
	out[pos*ITEM_SIZE32 + 18] = ((uint32_t *)(idx))[1]; \
}

template<int DL>
__device__ __forceinline__ void AddDist(uint64_t* r, const uint64_t a[4]) {
	UADDO1(r[0], a[0]);
	if (DL == 2) {
		UADD1(r[1], a[1]);
	} else if (DL == 3) {
		UADDC1(r[1], a[1]);
		UADD1(r[2], a[2]);
	} else {
		UADDC1(r[1], a[1]);
		UADDC1(r[2], a[2]);
		UADD1(r[3], a[3]);
	}
}

template<int DL>
__device__ void LoadKangaroos(uint64_t* a, uint64_t px[GPU_GRP_SIZE][4], uint64_t py[GPU_GRP_SIZE][4], uint64_t dist[GPU_GRP_SIZE][DL]) {
	__syncthreads();
	for (int g = 0; g < GPU_GRP_SIZE; g++) {
		uint32_t stride = g * KSIZE * blockDim.x;
		px[g][0] = a[IDX + 0 * blockDim.x + stride];
		px[g][1] = a[IDX + 1 * blockDim.x + stride];
		px[g][2] = a[IDX + 2 * blockDim.x + stride];
		px[g][3] = a[IDX + 3 * blockDim.x + stride];
		py[g][0] = a[IDX + 4 * blockDim.x + stride];
		py[g][1] = a[IDX + 5 * blockDim.x + stride];
		py[g][2] = a[IDX + 6 * blockDim.x + stride];
		py[g][3] = a[IDX + 7 * blockDim.x + stride];
		dist[g][0] = a[IDX + 8 * blockDim.x + stride];
		dist[g][1] = a[IDX + 9 * blockDim.x + stride];
		if (DL >= 3)
			dist[g][2] = a[IDX + 10 * blockDim.x + stride];
		if (DL >= 4)
			dist[g][3] = a[IDX + 11 * blockDim.x + stride];
	}
}

template<int DL>
__device__ void StoreKangaroos(uint64_t* a, uint64_t px[GPU_GRP_SIZE][4], uint64_t py[GPU_GRP_SIZE][4], uint64_t dist[GPU_GRP_SIZE][DL]) {
	__syncthreads();
	for (int g = 0; g < GPU_GRP_SIZE; g++) {
		uint32_t stride = g * KSIZE * blockDim.x;
		a[IDX + 0 * blockDim.x + stride] = px[g][0];
		a[IDX + 1 * blockDim.x + stride] = px[g][1];
		a[IDX + 2 * blockDim.x + stride] = px[g][2];
		a[IDX + 3 * blockDim.x + stride] = px[g][3];
		a[IDX + 4 * blockDim.x + stride] = py[g][0];
		a[IDX + 5 * blockDim.x + stride] = py[g][1];
		a[IDX + 6 * blockDim.x + stride] = py[g][2];
		a[IDX + 7 * blockDim.x + stride] = py[g][3];
		a[IDX + 8 * blockDim.x + stride] = dist[g][0];
		a[IDX + 9 * blockDim.x + stride] = dist[g][1];
		if (DL >= 3)
			a[IDX + 10 * blockDim.x + stride] = dist[g][2];
		if (DL >= 4)
			a[IDX + 11 * blockDim.x + stride] = dist[g][3];
	}
}

__device__ __noinline__ void _ModInvGrouped(uint64_t r[GPU_GRP_SIZE][4]) {
	uint64_t subp[GPU_GRP_SIZE][4];
	uint64_t newValue[4];
	uint64_t inverse[5];

	Load256(subp[0], r[0]);
	for (uint32_t i = 1; i < GPU_GRP_SIZE; i++)
		_ModMult(subp[i], subp[i - 1], r[i]);

	Load256(inverse, subp[GPU_GRP_SIZE - 1]);
	inverse[4] = 0;
	_ModInv(inverse);

	for (uint32_t i = GPU_GRP_SIZE - 1; i > 0; i--) {
		_ModMult(newValue, subp[i - 1], inverse);
		_ModMult(inverse, r[i]);
		Load256(r[i], newValue);
	}
	Load256(r[0], inverse);
}

template<int DL>
__device__ void ComputeKangaroos(uint64_t* kangaroos, uint32_t maxFound, uint32_t* out, uint64_t dpMask) {
	uint64_t px[GPU_GRP_SIZE][4];
	uint64_t py[GPU_GRP_SIZE][4];
	uint64_t dist[GPU_GRP_SIZE][DL];
	uint64_t dx[GPU_GRP_SIZE][4];
	uint64_t dy[4];
	uint64_t rx[4];
	uint64_t ry[4];
	uint64_t _s[4];
	uint64_t _p[4];
	uint32_t jmp;
	uint64_t Jx[4], Jy[4];

	LoadKangaroos<DL>(kangaroos, px, py, dist);

	for (int run = 0; run < NB_RUN; run++) {
		__syncthreads();
		for (int g = 0; g < GPU_GRP_SIZE; g++) {
			jmp = (uint32_t)px[g][0] & (NB_JUMP - 1);
			Load256(Jx, jPx[jmp]);
			ModSub256(dx[g], px[g], Jx);
		}

		_ModInvGrouped(dx);

		__syncthreads();
		for (int g = 0; g < GPU_GRP_SIZE; g++) {
			jmp = (uint32_t)px[g][0] & (NB_JUMP - 1);
			Load256(Jx, jPx[jmp]);
			Load256(Jy, jPy[jmp]);

			ModSub256(dy, py[g], Jy);
			_ModMult(_s, dy, dx[g]);
			_ModSqr(_p, _s);

			ModSub256(rx, _p, Jx);
			ModSub256(rx, px[g]);

			ModSub256(ry, px[g], rx);
			_ModMult(ry, _s);
			ModSub256(ry, py[g]);

			Load256(px[g], rx);
			Load256(py[g], ry);
			AddDist<DL>(dist[g], jD[jmp]);

			if ((px[g][3] & dpMask) == 0) {
				uint32_t pos = atomicAdd(out, 1);
				if (pos < maxFound) {
					uint64_t kIdx = (uint64_t)IDX + (uint64_t)g * (uint64_t)blockDim.x
						+ (uint64_t)blockIdx.x * ((uint64_t)blockDim.x * GPU_GRP_SIZE);
					uint64_t d4[4] = { dist[g][0], dist[g][1], 0, 0 };
					if (DL >= 3) d4[2] = dist[g][2];
					if (DL >= 4) d4[3] = dist[g][3];
					OutputDP(px[g], d4, &kIdx);
				}
			}
		}
	}

	StoreKangaroos<DL>(kangaroos, px, py, dist);
}

#endif
