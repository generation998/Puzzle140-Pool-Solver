#ifndef WIN64
#include <unistd.h>
#endif

#include "GPUEngine.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <stdio.h>
#include <string.h>
#include "../Timer.h"
#ifdef Load
#undef Load
#endif
#include "GPUMath.h"
#include "GPUKang.h"

__global__ void comp_kangaroos(uint64_t* kangaroos, uint32_t maxFound, uint32_t* found, uint64_t dpMask) {
	int xPtr = (blockIdx.x * blockDim.x * GPU_GRP_SIZE) * KSIZE;
	ComputeKangaroos(kangaroos + xPtr, maxFound, found, dpMask);
}

static int _ConvertSMVer2Cores(int major, int minor) {
	typedef struct { int SM; int Cores; } sSMtoCores;
	sSMtoCores tab[] = {
		{0x60, 64},{0x61, 128},{0x62, 128},{0x70, 64},{0x72, 64},{0x75, 64},
		{0x80, 64},{0x86, 128},{0x89, 128},{-1, -1} };
	for (int i = 0; tab[i].SM != -1; i++) {
		if (tab[i].SM == ((major << 4) + minor))
			return tab[i].Cores;
	}
	return 128;
}

GPUEngine::GPUEngine(int gpuId, uint32_t maxFound) {
	initialised = false;
	lostWarning = false;
	dpMask = 0;
	inputKangaroo = NULL;
	inputKangarooPinned = NULL;
	outputItem = NULL;
	outputItemPinned = NULL;

	cudaError_t err = cudaSetDevice(gpuId);
	if (err != cudaSuccess) { printf("GPUEngine: %s\n", cudaGetErrorString(err)); return; }
	err = cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);
	if (err != cudaSuccess) { printf("GPUEngine: %s\n", cudaGetErrorString(err)); return; }
	err = cudaDeviceSetCacheConfig(cudaFuncCachePreferL1);
	if (err != cudaSuccess) { printf("GPUEngine: %s\n", cudaGetErrorString(err)); return; }

	cudaDeviceProp deviceProp;
	cudaGetDeviceProperties(&deviceProp, gpuId);

	int gx = 2 * deviceProp.multiProcessorCount;
	int gy = 2 * _ConvertSMVer2Cores(deviceProp.major, deviceProp.minor);
	if (gy <= 0) gy = 256;
	nbThreadPerGroup = gy;
	nbThread = gx * gy;

	this->maxFound = maxFound;
	this->outputSize = maxFound * ITEM_SIZE + 4;

	char tmp[512];
	sprintf(tmp, "GPU #%d %s (%dx%d cores) Grid(%dx%d) kangs=%d",
		gpuId, deviceProp.name, deviceProp.multiProcessorCount,
		_ConvertSMVer2Cores(deviceProp.major, deviceProp.minor),
		gx, gy, nbThread * GPU_GRP_SIZE);
	deviceName = std::string(tmp);

	kangarooSize = (uint32_t)nbThread * GPU_GRP_SIZE * KSIZE * 8;
	err = cudaMalloc((void**)&inputKangaroo, kangarooSize);
	if (err != cudaSuccess) { printf("GPUEngine: kang: %s\n", cudaGetErrorString(err)); return; }
	kangarooSizePinned = (uint32_t)nbThreadPerGroup * GPU_GRP_SIZE * KSIZE * 8;
	err = cudaHostAlloc(&inputKangarooPinned, kangarooSizePinned, cudaHostAllocWriteCombined | cudaHostAllocMapped);
	if (err != cudaSuccess) { printf("GPUEngine: pinned kang: %s\n", cudaGetErrorString(err)); return; }
	err = cudaMalloc((void**)&outputItem, outputSize);
	if (err != cudaSuccess) { printf("GPUEngine: out: %s\n", cudaGetErrorString(err)); return; }
	err = cudaHostAlloc(&outputItemPinned, outputSize, cudaHostAllocMapped);
	if (err != cudaSuccess) { printf("GPUEngine: pinned out: %s\n", cudaGetErrorString(err)); return; }

	initialised = true;
}

GPUEngine::~GPUEngine() {
	if (inputKangaroo) cudaFree(inputKangaroo);
	if (inputKangarooPinned) cudaFreeHost(inputKangarooPinned);
	if (outputItem) cudaFree(outputItem);
	if (outputItemPinned) cudaFreeHost(outputItemPinned);
}

int GPUEngine::GetNbThread() { return nbThread; }
int GPUEngine::GetNbThreadPerGroup() { return nbThreadPerGroup; }
int GPUEngine::GetNbKangaroo() { return nbThread * GPU_GRP_SIZE; }
int GPUEngine::GetGroupSize() { return GPU_GRP_SIZE; }
int GPUEngine::GetStepSize() { return NB_RUN; }

bool GPUEngine::SetJumps(const uint64_t jx[NB_JUMP][4], const uint64_t jy[NB_JUMP][4], const uint64_t jd[NB_JUMP][4]) {
	cudaError_t err;
	err = cudaMemcpyToSymbol(jPx, jx, sizeof(uint64_t) * NB_JUMP * 4);
	if (err != cudaSuccess) { printf("GPUEngine: jPx: %s\n", cudaGetErrorString(err)); return false; }
	err = cudaMemcpyToSymbol(jPy, jy, sizeof(uint64_t) * NB_JUMP * 4);
	if (err != cudaSuccess) { printf("GPUEngine: jPy: %s\n", cudaGetErrorString(err)); return false; }
	err = cudaMemcpyToSymbol(jD, jd, sizeof(uint64_t) * NB_JUMP * 4);
	if (err != cudaSuccess) { printf("GPUEngine: jD: %s\n", cudaGetErrorString(err)); return false; }
	return true;
}

void GPUEngine::SetDPMask(uint64_t mask) {
	dpMask = mask;
}

bool GPUEngine::SetKangaroos(const uint64_t* px, const uint64_t* py, const uint64_t* dist) {
	if (!initialised)
		return false;

	const int strideSize = nbThreadPerGroup * KSIZE;
	const int nbBlock = nbThread / nbThreadPerGroup;
	const int blockSize = nbThreadPerGroup * KSIZE * GPU_GRP_SIZE;
	int idx = 0;

	for (int b = 0; b < nbBlock; b++) {
		for (int g = 0; g < GPU_GRP_SIZE; g++) {
			for (int t = 0; t < nbThreadPerGroup; t++) {
				inputKangarooPinned[g * strideSize + t + 0 * nbThreadPerGroup] = px[(size_t)idx * 4 + 0];
				inputKangarooPinned[g * strideSize + t + 1 * nbThreadPerGroup] = px[(size_t)idx * 4 + 1];
				inputKangarooPinned[g * strideSize + t + 2 * nbThreadPerGroup] = px[(size_t)idx * 4 + 2];
				inputKangarooPinned[g * strideSize + t + 3 * nbThreadPerGroup] = px[(size_t)idx * 4 + 3];
				inputKangarooPinned[g * strideSize + t + 4 * nbThreadPerGroup] = py[(size_t)idx * 4 + 0];
				inputKangarooPinned[g * strideSize + t + 5 * nbThreadPerGroup] = py[(size_t)idx * 4 + 1];
				inputKangarooPinned[g * strideSize + t + 6 * nbThreadPerGroup] = py[(size_t)idx * 4 + 2];
				inputKangarooPinned[g * strideSize + t + 7 * nbThreadPerGroup] = py[(size_t)idx * 4 + 3];
				inputKangarooPinned[g * strideSize + t + 8 * nbThreadPerGroup] = dist[(size_t)idx * 4 + 0];
				inputKangarooPinned[g * strideSize + t + 9 * nbThreadPerGroup] = dist[(size_t)idx * 4 + 1];
				inputKangarooPinned[g * strideSize + t + 10 * nbThreadPerGroup] = dist[(size_t)idx * 4 + 2];
				inputKangarooPinned[g * strideSize + t + 11 * nbThreadPerGroup] = dist[(size_t)idx * 4 + 3];
				idx++;
			}
		}
		cudaMemcpy(inputKangaroo + (size_t)b * blockSize, inputKangarooPinned, kangarooSizePinned, cudaMemcpyHostToDevice);
	}

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: SetKangaroos: %s\n", cudaGetErrorString(err));
		return false;
	}
	return callKernel();
}

bool GPUEngine::GetKangaroos(uint64_t* px, uint64_t* py, uint64_t* dist) {
	if (!initialised)
		return false;

	const int strideSize = nbThreadPerGroup * KSIZE;
	const int nbBlock = nbThread / nbThreadPerGroup;
	const int blockSize = nbThreadPerGroup * KSIZE * GPU_GRP_SIZE;
	int idx = 0;

	for (int b = 0; b < nbBlock; b++) {
		cudaMemcpy(inputKangarooPinned, inputKangaroo + (size_t)b * blockSize, kangarooSizePinned, cudaMemcpyDeviceToHost);
		for (int g = 0; g < GPU_GRP_SIZE; g++) {
			for (int t = 0; t < nbThreadPerGroup; t++) {
				px[(size_t)idx * 4 + 0] = inputKangarooPinned[g * strideSize + t + 0 * nbThreadPerGroup];
				px[(size_t)idx * 4 + 1] = inputKangarooPinned[g * strideSize + t + 1 * nbThreadPerGroup];
				px[(size_t)idx * 4 + 2] = inputKangarooPinned[g * strideSize + t + 2 * nbThreadPerGroup];
				px[(size_t)idx * 4 + 3] = inputKangarooPinned[g * strideSize + t + 3 * nbThreadPerGroup];
				py[(size_t)idx * 4 + 0] = inputKangarooPinned[g * strideSize + t + 4 * nbThreadPerGroup];
				py[(size_t)idx * 4 + 1] = inputKangarooPinned[g * strideSize + t + 5 * nbThreadPerGroup];
				py[(size_t)idx * 4 + 2] = inputKangarooPinned[g * strideSize + t + 6 * nbThreadPerGroup];
				py[(size_t)idx * 4 + 3] = inputKangarooPinned[g * strideSize + t + 7 * nbThreadPerGroup];
				dist[(size_t)idx * 4 + 0] = inputKangarooPinned[g * strideSize + t + 8 * nbThreadPerGroup];
				dist[(size_t)idx * 4 + 1] = inputKangarooPinned[g * strideSize + t + 9 * nbThreadPerGroup];
				dist[(size_t)idx * 4 + 2] = inputKangarooPinned[g * strideSize + t + 10 * nbThreadPerGroup];
				dist[(size_t)idx * 4 + 3] = inputKangarooPinned[g * strideSize + t + 11 * nbThreadPerGroup];
				idx++;
			}
		}
	}

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: GetKangaroos: %s\n", cudaGetErrorString(err));
		return false;
	}
	return true;
}

bool GPUEngine::callKernel() {
	cudaMemset(outputItem, 0, 4);
	comp_kangaroos << < nbThread / nbThreadPerGroup, nbThreadPerGroup >> > (inputKangaroo, maxFound, outputItem, dpMask);
	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: Kernel: %s\n", cudaGetErrorString(err));
		return false;
	}
	return true;
}

bool GPUEngine::Launch(std::vector<KANG_DP>& dps, bool spinWait) {
	dps.clear();
	if (spinWait) {
		cudaMemcpy(outputItemPinned, outputItem, outputSize, cudaMemcpyDeviceToHost);
	} else {
		cudaEvent_t evt;
		cudaEventCreate(&evt);
		cudaMemcpyAsync(outputItemPinned, outputItem, 4, cudaMemcpyDeviceToHost, 0);
		cudaEventRecord(evt, 0);
		while (cudaEventQuery(evt) == cudaErrorNotReady)
			Timer::SleepMillis(1);
		cudaEventDestroy(evt);
	}

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: Launch: %s\n", cudaGetErrorString(err));
		return false;
	}

	uint32_t nbFound = outputItemPinned[0];
	if (nbFound > maxFound) {
		if (!lostWarning) {
			printf("\nWarning, %u DPs lost\nHint: increase -m or -dp\n", nbFound - maxFound);
			lostWarning = true;
		}
		nbFound = maxFound;
	}

	cudaMemcpy(outputItemPinned, outputItem, nbFound * ITEM_SIZE + 4, cudaMemcpyDeviceToHost);

	for (uint32_t i = 0; i < nbFound; i++) {
		uint32_t* itemPtr = outputItemPinned + (i * ITEM_SIZE32 + 1);
		KANG_DP it;
		uint64_t* x = (uint64_t*)itemPtr;
		it.x[0] = x[0]; it.x[1] = x[1]; it.x[2] = x[2]; it.x[3] = x[3];
		uint64_t* d = (uint64_t*)(itemPtr + 8);
		it.d[0] = d[0]; it.d[1] = d[1]; it.d[2] = d[2]; it.d[3] = d[3];
		it.kIdx = *((uint64_t*)(itemPtr + 16));
		dps.push_back(it);
	}

	return callKernel();
}
