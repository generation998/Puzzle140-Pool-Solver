#ifndef KANGAROOH
#define KANGAROOH

#include "SECP256k1.h"
#include "GPU/GPUEngine.h"
#include <string>
#include <vector>
#include <unordered_map>

struct KANG_PARAM {
	Int ksStart;
	Int ksFinish;
};

class PoolClient;
struct PoolJob;

class Kangaroo {

public:

	Kangaroo(Secp256K1* secp, const std::string& pubkeyFile, const std::string& outputFile,
		uint32_t maxFound, int dpBits, KANG_PARAM* ks, int herdMode = -1, bool generateJumps = true);

	void Search(int gpuId);
	bool CheckGPU(int gpuId);
	void SetPool(PoolClient* client);
	bool ApplyJobJumps(const PoolJob& job);

	static bool LoadPubkeys(Secp256K1* secp, const std::string& fileName,
		std::vector<Point>& pubs, std::vector<std::string>& hex, uint64_t& skippedAddr);

private:

	struct DPRec {
		uint64_t x[4];
		uint64_t d[4];
		uint32_t herd;
		uint32_t target;
	};

	void initJumps();
	void setDP(int bits);
	void chooseDP(int totalK);
	void initKangaroos(int totalK, std::vector<uint64_t>& px,
		std::vector<uint64_t>& py, std::vector<uint64_t>& dist);
	bool processDP(const KANG_DP& it);
	bool tryKey(Int& k, uint32_t target);
	bool recoverFromPair(const DPRec& a, const DPRec& b);
	void outputFound(uint32_t target, Int& k);
	void printStats(uint64_t jumps, double t0);

	Secp256K1* secp;
	KANG_PARAM* ks;
	Int rangeSize;
	int rangeBits;
	int dpBits;
	int userDP;
	uint64_t dpMask;
	uint32_t maxFound;
	std::string outputFile;

	std::vector<Point> pubs;
	std::vector<std::string> pubHex;
	std::vector<uint8_t> solved;
	uint32_t nbSolved;

	uint64_t jx[NB_JUMP][4];
	uint64_t jy[NB_JUMP][4];
	uint64_t jd[NB_JUMP][4];

	std::unordered_map<uint64_t, std::vector<DPRec>> table;
	uint64_t nbDP;
	uint64_t nbCollide;
	uint64_t nbSameHerd;
	int herdMode;
	PoolClient* pool;
};

#endif
