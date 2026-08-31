#ifndef POOLCLIENTH
#define POOLCLIENTH

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <stdint.h>

#define POOL_NB_JUMP 32

struct PoolJump {
	std::string d;
	std::string x;
	std::string y;
};

struct PoolJob {
	int herd;
	int dpBits;
	std::string start;
	std::string end;
	std::string pubkey;
	PoolJump jumps[POOL_NB_JUMP];
};

struct PoolDP {
	uint64_t x[4];
	uint64_t d[2];
	uint32_t target;
};

class PoolClient {
public:
	PoolClient();
	~PoolClient();

	bool Connect(const std::string& host, int port);
	bool Hello(const std::string& token, const std::string& name, PoolJob& job, int preferHerd = -1, bool verbose = true);
	void StartPump(const std::string& host, int port, const std::string& token,
		const std::string& name, int herd, const std::string& spoolPath);
	void Enqueue(const std::vector<PoolDP>& dps);
	void NoteJumps(uint64_t jumps);
	void StopPump();
	void Drain(double seconds);
	bool SendDPs(const std::vector<PoolDP>& dps);
	bool SendStats(uint64_t jumps);
	bool Solved() const { return solved.load(); }
	uint64_t LastHits() const { return lastHits; }
	uint64_t LastTotal() const { return lastTotal; }
	uint64_t Queued() const { return queued.load(); }
	bool Online() const { return online.load(); }

private:
	bool sendLine(const std::string& s);
	bool recvLine(std::string& out);
	void closeSock();
	bool ensureWsa();
	void applySockOpts();
	bool reconnect();
	void pump();
	void loadSpool();
	void appendSpool(const PoolDP& dp);
	void persistAcked();

	uintptr_t sock;
	bool wsa;
	std::atomic<bool> solved;
	std::atomic<bool> online;
	std::atomic<bool> runPump;
	std::atomic<uint64_t> queued;
	uint64_t lastHits;
	uint64_t lastTotal;
	std::string recvBuf;

	std::string host;
	int port;
	std::string token;
	std::string name;
	int herd;
	std::string spoolPath;
	std::string ackPath;

	std::mutex mu;
	std::deque<PoolDP> q;
	uint64_t jumpsNote;
	std::thread pumpTh;
	FILE* spool;
	uint64_t acked;
	uint64_t written;
	double lastFailPrint;
};

#endif
