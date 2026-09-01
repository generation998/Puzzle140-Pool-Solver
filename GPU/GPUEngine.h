#ifndef GPUENGINEH
#define GPUENGINEH

#include <vector>
#include <string>
#include <stdint.h>

#define GPU_GRP_SIZE 128
#define NB_JUMP 32
#define NB_RUN 64
#define KSIZE 12
#define ITEM_SIZE 72
#define ITEM_SIZE32 (ITEM_SIZE/4)

#define KANG_TAME  0u
#define KANG_WILD1 1u

struct KANG_DP {
	uint64_t x[4];
	uint64_t d[4];
	uint64_t kIdx;
};

class GPUEngine {

public:

	GPUEngine(int gpuId, uint32_t maxFound);
	~GPUEngine();

	bool SetJumps(const uint64_t jx[NB_JUMP][4], const uint64_t jy[NB_JUMP][4], const uint64_t jd[NB_JUMP][4]);
	void SetDPMask(uint64_t mask);
	bool SetKangaroos(const uint64_t* px, const uint64_t* py, const uint64_t* dist);
	bool GetKangaroos(uint64_t* px, uint64_t* py, uint64_t* dist);
	bool Launch(std::vector<KANG_DP>& dps, bool spinWait = true);

	int GetNbThread();
	int GetNbThreadPerGroup();
	int GetNbKangaroo();
	int GetGroupSize();
	int GetStepSize();
	std::string deviceName;

private:

	bool callKernel();

	int nbThread;
	int nbThreadPerGroup;
	uint32_t maxFound;
	uint32_t outputSize;
	uint64_t dpMask;
	bool lostWarning;
	bool initialised;

	uint64_t* inputKangaroo;
	uint64_t* inputKangarooPinned;
	uint32_t* outputItem;
	uint32_t* outputItemPinned;
	uint32_t kangarooSize;
	uint32_t kangarooSizePinned;
};

#endif
