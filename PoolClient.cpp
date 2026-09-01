#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define pool_sleep_ms(ms) Sleep(ms)
#define pool_closesocket closesocket
static SOCKET asSock(uintptr_t s) { return (SOCKET)s; }
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#define INVALID_SOCKET ((uintptr_t)-1)
#define SOCKET int
#define pool_sleep_ms(ms) usleep((unsigned)(ms) * 1000u)
#define pool_closesocket close
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
static int asSock(uintptr_t s) { return (int)s; }
#endif

#include "PoolClient.h"
#include "Timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool parseKV(const std::string& s, const char* key, std::string& val) {
	std::string pat = std::string(key) + "=";
	size_t p = s.find(pat);
	if (p == std::string::npos)
		return false;
	p += pat.size();
	size_t e = s.find(' ', p);
	val = (e == std::string::npos) ? s.substr(p) : s.substr(p, e - p);
	return true;
}

PoolClient::PoolClient() {
	sock = (uintptr_t)INVALID_SOCKET;
	wsa = false;
	solved = false;
	online = false;
	runPump = false;
	queued = 0;
	lastHits = 0;
	lastTotal = 0;
	port = 17403;
	herd = -1;
	spool = NULL;
	acked = 0;
	written = 0;
	jumpsNote = 0;
	lastFailPrint = 0;
}

PoolClient::~PoolClient() {
	StopPump();
	if (spool) {
		fflush(spool);
		fclose(spool);
		spool = NULL;
	}
	closeSock();
#ifdef _WIN32
	if (wsa)
		WSACleanup();
#endif
}

void PoolClient::Drain(double seconds) {
	double t0 = Timer::get_tick();
	while (queued.load() > 0 && !solved.load() && (Timer::get_tick() - t0) < seconds)
		pool_sleep_ms(50);
}

void PoolClient::StopPump() {
	runPump = false;
	if (pumpTh.joinable())
		pumpTh.join();
}

void PoolClient::closeSock() {
#ifdef _WIN32
	if (asSock(sock) != INVALID_SOCKET) {
		pool_closesocket(asSock(sock));
		sock = (uintptr_t)INVALID_SOCKET;
	}
#else
	if ((int)sock >= 0) {
		pool_closesocket(asSock(sock));
		sock = (uintptr_t)INVALID_SOCKET;
	}
#endif
	online = false;
	recvBuf.clear();
}

bool PoolClient::ensureWsa() {
#ifdef _WIN32
	if (wsa)
		return true;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		fprintf(stderr, "[pool] WSAStartup failed\n");
		return false;
	}
	wsa = true;
#endif
	return true;
}

void PoolClient::applySockOpts() {
	int nbuf = 1 << 20;
	setsockopt(asSock(sock), SOL_SOCKET, SO_SNDBUF, (char*)&nbuf, sizeof(nbuf));
	setsockopt(asSock(sock), SOL_SOCKET, SO_RCVBUF, (char*)&nbuf, sizeof(nbuf));
	int one = 1;
	setsockopt(asSock(sock), IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
	int nodelayLinger = 1;
	linger lin;
	lin.l_onoff = 0;
	lin.l_linger = 0;
	setsockopt(asSock(sock), SOL_SOCKET, SO_LINGER, (char*)&lin, sizeof(lin));
	(void)nodelayLinger;
}

bool PoolClient::Connect(const std::string& hostIn, int portIn) {
	host = hostIn;
	port = portIn;
	if (!ensureWsa())
		return false;
	closeSock();
	sock = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (asSock(sock) == INVALID_SOCKET) {
		fprintf(stderr, "[pool] socket failed\n");
		return false;
	}
	applySockOpts();
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* res = NULL;
		if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || !res)
			return false;
		addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
		freeaddrinfo(res);
	}
	if (connect(asSock(sock), (sockaddr*)&addr, sizeof(addr)) != 0) {
		closeSock();
		return false;
	}
#ifdef _WIN32
	DWORD timeout = 20000;
	setsockopt(asSock(sock), SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	setsockopt(asSock(sock), SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#else
	struct timeval tv;
	tv.tv_sec = 20;
	tv.tv_usec = 0;
	setsockopt(asSock(sock), SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
	setsockopt(asSock(sock), SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
#endif
	online = true;
	return true;
}

bool PoolClient::sendLine(const std::string& s) {
	std::string line = s + "\n";
	const char* p = line.c_str();
	int left = (int)line.size();
	while (left > 0) {
		int n = send(asSock(sock), p, left, 0);
		if (n <= 0)
			return false;
		p += n;
		left -= n;
	}
	return true;
}

bool PoolClient::recvLine(std::string& out) {
	out.clear();
	for (;;) {
		size_t nl = recvBuf.find('\n');
		if (nl != std::string::npos) {
			out = recvBuf.substr(0, nl);
			if (!out.empty() && out[out.size() - 1] == '\r')
				out.pop_back();
			recvBuf.erase(0, nl + 1);
			return true;
		}
		char tmp[4096];
		int n = recv(asSock(sock), tmp, sizeof(tmp), 0);
		if (n <= 0)
			return false;
		recvBuf.append(tmp, (size_t)n);
		if (recvBuf.size() > (1u << 20))
			return false;
	}
}

bool PoolClient::Hello(const std::string& tokenIn, const std::string& nameIn, PoolJob& job, int preferHerd, bool verbose) {
	token = tokenIn;
	name = nameIn;
	char line0[256];
	if (preferHerd >= 0)
		snprintf(line0, sizeof(line0), "HELLO token=%s name=%s herd=%d",
			token.c_str(), name.c_str(), preferHerd);
	else
		snprintf(line0, sizeof(line0), "HELLO token=%s name=%s", token.c_str(), name.c_str());
	if (!sendLine(line0))
		return false;
	std::string line;
	if (!recvLine(line))
		return false;
	if (line.rfind("ERR", 0) == 0) {
		fprintf(stderr, "[pool] %s\n", line.c_str());
		return false;
	}
	if (line.rfind("JOB ", 0) != 0) {
		fprintf(stderr, "[pool] expected JOB, got %s\n", line.c_str());
		return false;
	}
	std::string v;
	if (!parseKV(line, "herd", v))
		return false;
	job.herd = atoi(v.c_str());
	if (!parseKV(line, "dp", v))
		return false;
	job.dpBits = atoi(v.c_str());
	if (!parseKV(line, "start", job.start) || !parseKV(line, "end", job.end)
		|| !parseKV(line, "pubkey", job.pubkey)) {
		fprintf(stderr, "[pool] incomplete JOB\n");
		return false;
	}
	int got = 0;
	for (;;) {
		if (!recvLine(line))
			return false;
		if (line == "ENDJOB")
			break;
		if (line.rfind("JUMP ", 0) != 0)
			continue;
		int idx = 0;
		if (sscanf(line.c_str(), "JUMP %d", &idx) != 1 || idx < 0 || idx >= POOL_NB_JUMP)
			continue;
		if (!parseKV(line, "d", job.jumps[idx].d)
			|| !parseKV(line, "x", job.jumps[idx].x)
			|| !parseKV(line, "y", job.jumps[idx].y))
			return false;
		got++;
	}
	if (got < POOL_NB_JUMP) {
		fprintf(stderr, "[pool] jump table incomplete (%d/%d)\n", got, POOL_NB_JUMP);
		return false;
	}
	herd = job.herd;
	if (verbose) {
		fprintf(stdout, "[pool] job herd=%s dp=%d start=%s\n",
			job.herd == 0 ? "tame" : "wild", job.dpBits, job.start.c_str());
		fprintf(stdout, "[pool] GPU keeps walking if the server drops; DPs spool to disk until ACK\n");
		fflush(stdout);
	}
	return true;
}

bool PoolClient::SendDPs(const std::vector<PoolDP>& dps) {
	if (dps.empty())
		return true;
	char buf[256];
	snprintf(buf, sizeof(buf), "DPS %u", (unsigned)dps.size());
	if (!sendLine(buf))
		return false;
	for (size_t i = 0; i < dps.size(); i++) {
		snprintf(buf, sizeof(buf),
			"%llx %llx %llx %llx %llx %llx %llx %llx %u",
			(unsigned long long)dps[i].x[0],
			(unsigned long long)dps[i].x[1],
			(unsigned long long)dps[i].x[2],
			(unsigned long long)dps[i].x[3],
			(unsigned long long)dps[i].d[0],
			(unsigned long long)dps[i].d[1],
			(unsigned long long)dps[i].d[2],
			(unsigned long long)dps[i].d[3],
			dps[i].target);
		if (!sendLine(buf))
			return false;
	}
	std::string ack;
	if (!recvLine(ack))
		return false;
	if (ack.rfind("ERR", 0) == 0)
		return false;
	std::string v;
	if (parseKV(ack, "hits", v))
		lastHits = strtoull(v.c_str(), NULL, 10);
	if (parseKV(ack, "total", v))
		lastTotal = strtoull(v.c_str(), NULL, 10);
	if (parseKV(ack, "solved", v) && v != "0") {
		solved = true;
		fprintf(stdout, "\n[pool] Server reports SOLVED. The private key is only on the server.\n");
		fflush(stdout);
	}
	return true;
}

bool PoolClient::SendStats(uint64_t jumps) {
	char buf[64];
	snprintf(buf, sizeof(buf), "STATS jumps=%llu", (unsigned long long)jumps);
	if (!sendLine(buf))
		return false;
	std::string ack;
	return recvLine(ack);
}

void PoolClient::loadSpool() {
	acked = 0;
	FILE* idx = fopen(ackPath.c_str(), "rb");
	if (idx) {
		if (fread(&acked, sizeof(acked), 1, idx) != 1)
			acked = 0;
		fclose(idx);
	}
	FILE* in = fopen(spoolPath.c_str(), "rb");
	if (!in)
		return;
	PoolDP rec;
	uint64_t n = 0;
	while (fread(&rec, sizeof(rec), 1, in) == 1) {
		if (n >= acked)
			q.push_back(rec);
		n++;
	}
	written = n;
	fclose(in);
	queued = (uint64_t)q.size();
	if (!q.empty())
		fprintf(stdout, "[pool] reloaded %llu unacked DPs from %s\n",
			(unsigned long long)q.size(), spoolPath.c_str());
}

void PoolClient::appendSpool(const PoolDP& dp) {
	if (!spool)
		return;
	fwrite(&dp, sizeof(dp), 1, spool);
	written++;
	if ((written & 31ULL) == 0)
		fflush(spool);
}

void PoolClient::persistAcked() {
	FILE* idx = fopen(ackPath.c_str(), "wb");
	if (!idx)
		return;
	fwrite(&acked, sizeof(acked), 1, idx);
	fclose(idx);
}

void PoolClient::Enqueue(const std::vector<PoolDP>& dps) {
	if (dps.empty())
		return;
	std::lock_guard<std::mutex> g(mu);
	for (size_t i = 0; i < dps.size(); i++) {
		q.push_back(dps[i]);
		appendSpool(dps[i]);
	}
	queued = (uint64_t)q.size();
}

void PoolClient::NoteJumps(uint64_t jumps) {
	jumpsNote = jumps;
}

bool PoolClient::reconnect() {
	PoolJob job;
	if (!Connect(host, port))
		return false;
	if (!Hello(token, name, job, herd, false)) {
		closeSock();
		return false;
	}
	if (herd >= 0 && job.herd != herd)
		fprintf(stderr, "[pool] warning: server assigned herd %d, staying tagged as %d\n",
			job.herd, herd);
	return true;
}

void PoolClient::pump() {
	double lastStats = Timer::get_tick();
	double lastFlush = lastStats;
	while (runPump && !solved.load()) {
		if (!online.load()) {
			if (!reconnect()) {
				double now = Timer::get_tick();
				if (now - lastFailPrint >= 10.0) {
					fprintf(stderr, "[pool] server offline — GPU still running, DPs in %s\n",
						spoolPath.c_str());
					lastFailPrint = now;
				}
				pool_sleep_ms(1000);
				continue;
			}
			fprintf(stdout, "\n[pool] reconnected, flushing spool\n");
			fflush(stdout);
		}

		std::vector<PoolDP> batch;
		uint64_t jcopy = 0;
		{
			std::lock_guard<std::mutex> g(mu);
			size_t n = q.size();
			if (n > 256)
				n = 256;
			for (size_t i = 0; i < n; i++)
				batch.push_back(q[i]);
			jcopy = jumpsNote;
			if ((Timer::get_tick() - lastFlush) > 1.0 && spool) {
				fflush(spool);
				lastFlush = Timer::get_tick();
			}
		}

		if (!batch.empty()) {
			if (!SendDPs(batch)) {
				closeSock();
				continue;
			}
			{
				std::lock_guard<std::mutex> g(mu);
				for (size_t i = 0; i < batch.size() && !q.empty(); i++)
					q.pop_front();
				acked += (uint64_t)batch.size();
				queued = (uint64_t)q.size();
			}
			persistAcked();
		} else {
			double now = Timer::get_tick();
			if (now - lastStats >= 5.0) {
				if (!SendStats(jcopy))
					closeSock();
				lastStats = now;
			} else {
				pool_sleep_ms(20);
			}
		}
	}
	if (spool)
		fflush(spool);
}

void PoolClient::StartPump(const std::string& hostIn, int portIn, const std::string& tokenIn,
	const std::string& nameIn, int herdIn, const std::string& spoolPathIn) {
	host = hostIn;
	port = portIn;
	token = tokenIn;
	name = nameIn;
	herd = herdIn;
	spoolPath = spoolPathIn;
	ackPath = spoolPath + ".ack";
	{
		std::lock_guard<std::mutex> g(mu);
		loadSpool();
		spool = fopen(spoolPath.c_str(), "ab");
	}
	runPump = true;
	pumpTh = std::thread(&PoolClient::pump, this);
}
