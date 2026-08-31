#include "Kangaroo.h"
#include "Timer.h"
#include "Random.h"
#include "PoolClient.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#ifndef _WIN32
#include <strings.h>
#endif
#include <fstream>
#include <cmath>
#include <ctime>

static void setHex(Int& i, const std::string& s) {
	std::vector<char> buf(s.begin(), s.end());
	buf.push_back(0);
	i.SetBase16(&buf[0]);
}

static void intFrom128(Int& i, const uint64_t d[2]) {
	i.SetInt32(0);
	i.bits64[0] = d[0];
	i.bits64[1] = d[1];
	i.bits64[2] = 0;
	i.bits64[3] = 0;
	i.bits64[4] = 0;
}

static bool xEqual(const uint64_t a[4], const uint64_t b[4]) {
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static uint64_t xKey(const uint64_t x[4]) {
	return x[0] ^ (x[1] * 0x9E3779B97F4A7C15ULL);
}

static bool isHex(const std::string& s) {
	if (s.empty())
		return false;
	for (size_t i = 0; i < s.size(); i++) {
		if (!isxdigit((unsigned char)s[i]))
			return false;
	}
	return true;
}

bool Kangaroo::LoadPubkeys(Secp256K1* secp, const std::string& fileName,
	std::vector<Point>& pubs, std::vector<std::string>& hex, uint64_t& skippedAddr) {

	skippedAddr = 0;
	std::string asHex = fileName;
	if (asHex.size() >= 2 && asHex[0] == '0' && (asHex[1] == 'x' || asHex[1] == 'X'))
		asHex = asHex.substr(2);
	if (isHex(asHex) && (asHex.size() == 66 || asHex.size() == 130)) {
		bool compressed = false;
		Point q = secp->ParsePublicKeyHex(asHex, compressed);
		pubs.push_back(q);
		hex.push_back(secp->GetPublicKeyHex(true, q));
		fprintf(stdout, "Loaded 1 public key (inline hex)\n");
		return true;
	}
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (!fp) {
		fprintf(stderr, "[ERROR] cannot open %s\n", fileName.c_str());
		return false;
	}
	fseek(fp, 0L, SEEK_END);
	long long sz = (long long)ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	if (sz > 1000)
		fprintf(stdout, "Loading pubkeys %s (%.2f MB)...\n", fileName.c_str(), sz / 1048576.0);

	char buf[512];
	uint64_t nLine = 0;
	bool first = true;
	while (fgets(buf, sizeof(buf), fp)) {
		std::string line(buf);
		if (first && line.size() >= 3 &&
			(unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
			line = line.substr(3);
		first = false;
		while (!line.empty() && isspace((unsigned char)line.back()))
			line.pop_back();
		size_t sp = 0;
		while (sp < line.size() && isspace((unsigned char)line[sp]))
			sp++;
		if (sp)
			line = line.substr(sp);
		if (line.empty() || line[0] == '#')
			continue;
		nLine++;

		if (line[0] == '1' || line[0] == '3' || line[0] == 'b' || line[0] == 'B') {
			skippedAddr++;
			continue;
		}
		if (line.size() >= 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X'))
			line = line.substr(2);
		if (!isHex(line)) {
			skippedAddr++;
			continue;
		}
		if (line.size() != 66 && line.size() != 130) {
			skippedAddr++;
			continue;
		}
		unsigned int pfx = 0;
		sscanf(line.substr(0, 2).c_str(), "%02x", &pfx);
		if (pfx != 2 && pfx != 3 && pfx != 4) {
			skippedAddr++;
			continue;
		}

		bool compressed = false;
		Point q = secp->ParsePublicKeyHex(line, compressed);
		pubs.push_back(q);
		hex.push_back(secp->GetPublicKeyHex(true, q));
	}
	fclose(fp);
	static const char* known[][3] = {
		{ "03231a67e424caf7", "62", "2000000000000000-3FFFFFFFFFFFFFFF" },
		{ "0348e843dc5b1bd2", "60", "800000000000000-FFFFFFFFFFFFFFF" },
		{ "0241267d2d7ee1a8", "59", "400000000000000-7FFFFFFFFFFFFFF" },
		{ "0441267d2d7ee1a8", "59", "400000000000000-7FFFFFFFFFFFFFF" },
		{ "0311569442e87032", "58", "200000000000000-3FFFFFFFFFFFFFF" },
		{ "0411569442e87032", "58", "200000000000000-3FFFFFFFFFFFFFF" },
		{ "02a521a07e98f78b", "57", "100000000000000-1FFFFFFFFFFFFFF" },
		{ "033f2db2074e3217", "56", "80000000000000-FFFFFFFFFFFFFF" },
		{ "043f2db2074e3217", "56", "80000000000000-FFFFFFFFFFFFFF" },
		{ NULL, NULL, NULL }
	};
	for (size_t i = 0; i < hex.size(); i++) {
		std::string h = hex[i];
		for (int k = 0; known[k][0]; k++) {
#ifdef _WIN32
			if (_strnicmp(h.c_str(), known[k][0], 16) == 0) {
#else
			if (strncasecmp(h.c_str(), known[k][0], 16) == 0) {
#endif
				fprintf(stdout, "  key %u looks like BTC puzzle #%s  native range %s\n",
					(uint32_t)i, known[k][1], known[k][2]);
				break;
			}
		}
	}

	fprintf(stdout, "Loaded %u public keys", (uint32_t)pubs.size());
	if (skippedAddr)
		fprintf(stdout, ", skipped %llu address/invalid lines", (unsigned long long)skippedAddr);
	fprintf(stdout, "\n");
	return !pubs.empty();
}

Kangaroo::Kangaroo(Secp256K1* secp, const std::string& pubkeyFile, const std::string& outputFile,
	uint32_t maxFound, int dpBits, KANG_PARAM* ks, int herdMode, bool generateJumps) {

	this->secp = secp;
	this->ks = ks;
	this->maxFound = maxFound;
	this->outputFile = outputFile;
	this->nbSolved = 0;
	this->nbDP = 0;
	this->nbCollide = 0;
	this->nbSameHerd = 0;
	this->userDP = dpBits;
	this->herdMode = herdMode;
	this->pool = NULL;

	rseed((unsigned long)time(NULL) ^ (unsigned long)Timer::get_tick());

	uint64_t skipped = 0;
	if (!LoadPubkeys(secp, pubkeyFile, pubs, pubHex, skipped)) {
		fprintf(stderr, "[ERROR] No public keys loaded. Use hex 02/03/04 keys, not Bitcoin addresses.\n");
		exit(-1);
	}
	solved.assign(pubs.size(), 0);

	rangeSize.Set(&ks->ksFinish);
	rangeSize.Sub(&ks->ksStart);
	rangeSize.AddOne();
	rangeBits = rangeSize.GetBitLength();
	if (rangeBits < 8)
		rangeBits = 8;
	if (rangeBits > 125)
		fprintf(stdout, "[WARNING] Range is 2^%d; GPU distance is 128-bit (JLP limit ~125 bits).\n", rangeBits);

	if (generateJumps)
		initJumps();
}

void Kangaroo::SetPool(PoolClient* client) {
	pool = client;
}

bool Kangaroo::ApplyJobJumps(const PoolJob& job) {
	for (int i = 0; i < NB_JUMP; i++) {
		Int d, x, y;
		setHex(d, job.jumps[i].d);
		setHex(x, job.jumps[i].x);
		setHex(y, job.jumps[i].y);
		for (int k = 0; k < 4; k++) {
			jx[i][k] = x.bits64[k];
			jy[i][k] = y.bits64[k];
		}
		jd[i][0] = d.bits64[0];
		jd[i][1] = d.bits64[1];
	}
	fprintf(stdout, "Jump table loaded from pool server (%d jumps)\n", NB_JUMP);
	return true;
}

void Kangaroo::setDP(int bits) {
	dpBits = bits;
	if (dpBits < 0) dpBits = 0;
	if (dpBits > 64) dpBits = 64;
	if (dpBits == 0)
		dpMask = 0;
	else {
		dpMask = (1ULL << (64 - dpBits)) - 1ULL;
		dpMask = ~dpMask;
	}
}

void Kangaroo::chooseDP(int totalK) {
	int suggested = (int)((double)rangeBits / 2.0 - log2((double)(std::max)(1, totalK)));
	if (suggested < 0) suggested = 0;
	double perKernel = (double)totalK * (double)NB_RUN / pow(2.0, (double)suggested);
	while (perKernel > (double)maxFound * 0.4 && suggested < 40) {
		suggested++;
		perKernel *= 0.5;
	}
	if (userDP >= 0)
		suggested = userDP;
	setDP(suggested);
	fprintf(stdout, "DP size: %d [0x%016llX] (high bits of x)\n", dpBits, (unsigned long long)dpMask);
}

void Kangaroo::initJumps() {
	int jumpBit = rangeBits / 2 + 1;
	if (jumpBit > 128) jumpBit = 128;
	if (jumpBit < 2) jumpBit = 2;

	rseed(0x600DCAFE);
	double maxAvg = pow(2.0, (double)jumpBit - 0.95);
	double minAvg = pow(2.0, (double)jumpBit - 1.05);
	double distAvg = 0;
	bool ok = false;
	int maxRetry = 100;

	while (!ok && maxRetry > 0) {
		Int totalDist;
		totalDist.SetInt32(0);
		for (int i = 0; i < NB_JUMP; i++) {
			Int d;
			d.Rand(jumpBit);
			if (d.IsZero())
				d.SetInt32(1);
			totalDist.Add(&d);
			jd[i][0] = d.bits64[0];
			jd[i][1] = d.bits64[1];
			Point p = secp->ComputePublicKey(&d);
			for (int k = 0; k < 4; k++) {
				jx[i][k] = p.x.bits64[k];
				jy[i][k] = p.y.bits64[k];
			}
		}
		distAvg = totalDist.ToDouble() / (double)NB_JUMP;
		ok = distAvg > minAvg && distAvg < maxAvg;
		maxRetry--;
	}

	rseed((unsigned long)time(NULL) ^ (unsigned long)Timer::get_tick());
	fprintf(stdout, "Jump Avg distance: 2^%.2f (%d-bit draws)\n", log2(distAvg), jumpBit);
}

void Kangaroo::initKangaroos(int totalK, std::vector<uint64_t>& px,
	std::vector<uint64_t>& py, std::vector<uint64_t>& dist) {

	px.assign((size_t)totalK * 4, 0);
	py.assign((size_t)totalK * 4, 0);
	dist.assign((size_t)totalK * 2, 0);

	const uint32_t M = (uint32_t)pubs.size();
	if (herdMode == (int)KANG_TAME)
		fprintf(stdout, "Initializing %d kangaroos (TAME only — pool worker)...\n", totalK);
	else if (herdMode == (int)KANG_WILD1)
		fprintf(stdout, "Initializing %d kangaroos (WILD only — pool worker)...\n", totalK);
	else
		fprintf(stdout, "Initializing %d kangaroos (50/50 tame/wild, JLP 128/thread)...\n", totalK);
	fflush(stdout);

	Point twalk;
	Int td;
	int tameLeft = 0;
	std::vector<Point> wwalk(M);
	std::vector<Int> wd(M);
	std::vector<int> wildLeft(M, 0);
	const int herdLen = 128;

	for (int idx = 0; idx < totalK; idx++) {
		Point p;
		Int dval;
		bool isTame;
		if (herdMode == (int)KANG_TAME)
			isTame = true;
		else if (herdMode == (int)KANG_WILD1)
			isTame = false;
		else
			isTame = ((idx % 2) == (int)KANG_TAME);
		if (isTame) {
			if (tameLeft == 0) {
				td.Rand(&rangeSize);
				if (td.IsZero())
					td.SetInt32(1);
				Int tpriv;
				tpriv.Set(&ks->ksStart);
				tpriv.Add(&td);
				twalk = secp->ComputePublicKey(&tpriv);
				tameLeft = herdLen;
			}
			p = twalk;
			dval.Set(&td);
			twalk = secp->AddDirect(twalk, secp->G);
			td.AddOne();
			tameLeft--;
		} else {
			uint32_t t = (herdMode >= 0)
				? (uint32_t)((uint64_t)idx % M)
				: (uint32_t)(((uint64_t)idx / 2) % M);
			if (wildLeft[t] == 0) {
				wd[t].Rand(&rangeSize);
				if (wd[t].IsZero())
					wd[t].SetInt32(1);
				Point extra = secp->ComputePublicKey(&wd[t]);
				wwalk[t] = secp->AddDirect(pubs[t], extra);
				wildLeft[t] = herdLen;
			}
			p = wwalk[t];
			dval.Set(&wd[t]);
			wwalk[t] = secp->AddDirect(wwalk[t], secp->G);
			wd[t].AddOne();
			wildLeft[t]--;
		}
		px[(size_t)idx * 4 + 0] = p.x.bits64[0];
		px[(size_t)idx * 4 + 1] = p.x.bits64[1];
		px[(size_t)idx * 4 + 2] = p.x.bits64[2];
		px[(size_t)idx * 4 + 3] = p.x.bits64[3];
		py[(size_t)idx * 4 + 0] = p.y.bits64[0];
		py[(size_t)idx * 4 + 1] = p.y.bits64[1];
		py[(size_t)idx * 4 + 2] = p.y.bits64[2];
		py[(size_t)idx * 4 + 3] = p.y.bits64[3];
		dist[(size_t)idx * 2 + 0] = dval.bits64[0];
		dist[(size_t)idx * 2 + 1] = dval.bits64[1];
		if ((idx & 0x3FFFF) == 0) {
			fprintf(stdout, "  init %d / %d\r", idx, totalK);
			fflush(stdout);
		}
	}
	fprintf(stdout, "  init %d / %d          \n", totalK, totalK);

	Int chk;
	if (herdMode == (int)KANG_WILD1) {
		intFrom128(chk, &dist[0]);
		Point extra = secp->ComputePublicKey(&chk);
		Point Wchk = secp->AddDirect(pubs[0], extra);
		bool wildOk = Wchk.x.bits64[0] == px[0] && Wchk.x.bits64[1] == px[1]
			&& Wchk.x.bits64[2] == px[2] && Wchk.x.bits64[3] == px[3];
		fprintf(stdout, "Init wild[0] %s\n", wildOk ? "OK" : "MISMATCH vs Q+d*G");
	} else {
		intFrom128(chk, &dist[0]);
		chk.Add(&ks->ksStart);
		Point Pchk = secp->ComputePublicKey(&chk);
		bool tameOk = Pchk.x.bits64[0] == px[0] && Pchk.x.bits64[1] == px[1]
			&& Pchk.x.bits64[2] == px[2] && Pchk.x.bits64[3] == px[3];
		fprintf(stdout, "Init tame[0] %s\n", tameOk ? "OK" : "MISMATCH vs (start+d)*G");
		if (herdMode < 0 && totalK > 1) {
			intFrom128(chk, &dist[2]);
			Point extra = secp->ComputePublicKey(&chk);
			Point Wchk = secp->AddDirect(pubs[0], extra);
			bool wildOk = Wchk.x.bits64[0] == px[4] && Wchk.x.bits64[1] == px[5]
				&& Wchk.x.bits64[2] == px[6] && Wchk.x.bits64[3] == px[7];
			fprintf(stdout, "Init wild[1] %s\n", wildOk ? "OK" : "MISMATCH vs Q+d*G");
		}
	}
}

bool Kangaroo::tryKey(Int& k, uint32_t target) {
	if (target >= pubs.size() || solved[target])
		return false;
	if (k.IsNegative())
		k.Add(&secp->order);
	k.Mod(&secp->order);
	if (k.IsZero())
		return false;
	if (k.IsLower(&ks->ksStart) || k.IsGreater(&ks->ksFinish))
		return false;
	Point p = secp->ComputePublicKey(&k);
	if (!p.x.IsEqual(&pubs[target].x) || !p.y.IsEqual(&pubs[target].y))
		return false;
	outputFound(target, k);
	return true;
}

void Kangaroo::outputFound(uint32_t target, Int& k) {
	if (pool) {
		fprintf(stdout, "\n[pool] Local recovery blocked. Key stays on the server.\n");
		fflush(stdout);
		return;
	}
	solved[target] = 1;
	nbSolved++;
	std::string privHex = k.GetBase16();
	std::string wif = secp->GetPrivAddress(true, k);
	std::string pub = pubHex[target];
	fprintf(stdout, "\nFOUND [%u/%u] pub=%s\n  priv=%s\n  WIF=%s\n",
		nbSolved, (uint32_t)pubs.size(), pub.c_str(), privHex.c_str(), wif.c_str());
	fflush(stdout);
	if (!outputFile.empty()) {
		FILE* f = fopen(outputFile.c_str(), "a");
		if (f) {
			fprintf(f, "Pub: %s\nPriv: %s\nWIF: %s\n\n", pub.c_str(), privHex.c_str(), wif.c_str());
			fclose(f);
		}
	}
}

bool Kangaroo::recoverFromPair(const DPRec& a, const DPRec& b) {
	const DPRec* tame = NULL;
	const DPRec* wild = NULL;
	if ((a.herd == KANG_TAME) && (b.herd != KANG_TAME)) {
		tame = &a; wild = &b;
	} else if ((b.herd == KANG_TAME) && (a.herd != KANG_TAME)) {
		tame = &b; wild = &a;
	} else {
		return false;
	}

	Int dt, dw;
	intFrom128(dt, tame->d);
	intFrom128(dw, wild->d);
	uint32_t target = wild->target;

	Int cands[4];
	cands[0].Set(&ks->ksStart); cands[0].Add(&dt); cands[0].Sub(&dw);
	cands[1].Set(&ks->ksStart); cands[1].Add(&dw); cands[1].Sub(&dt);
	cands[2].Set(&dt); cands[2].Sub(&dw);
	cands[3].Set(&dw); cands[3].Sub(&dt);

	for (int i = 0; i < 4; i++) {
		if (tryKey(cands[i], target))
			return true;
	}
	return false;
}

bool Kangaroo::processDP(const KANG_DP& it) {
	nbDP++;
	DPRec rec;
	memcpy(rec.x, it.x, sizeof(rec.x));
	memcpy(rec.d, it.d, sizeof(rec.d));
	if (herdMode >= 0) {
		rec.herd = (uint32_t)herdMode;
		rec.target = (uint32_t)(it.kIdx % pubs.size());
	} else {
		rec.herd = (uint32_t)(it.kIdx % 2);
		rec.target = (uint32_t)((it.kIdx / 2) % pubs.size());
	}

	uint64_t key = xKey(it.x);
	std::vector<DPRec>& bucket = table[key];
	for (size_t i = 0; i < bucket.size(); i++) {
		if (!xEqual(bucket[i].x, rec.x))
			continue;
		if (bucket[i].herd == rec.herd) {
			nbSameHerd++;
			continue;
		}
		nbCollide++;
		if (recoverFromPair(bucket[i], rec))
			return true;
	}
	if (bucket.size() < 8)
		bucket.push_back(rec);
	return false;
}

void Kangaroo::printStats(uint64_t jumps, double t0) {
	double t = Timer::get_tick() - t0;
	if (t < 0.001) t = 0.001;
	double rate = jumps / t / 1e6;
	double W = rangeSize.ToDouble();
	double expect = 2.0 * std::sqrt((std::max)(1.0, W / (double)pubs.size()));
	double eta = (rate > 0.0) ? (expect / (rate * 1e6)) : 0.0;
	if (pool) {
		fprintf(stdout, "\r%.1f MJ/s - %.3f GJumps - DP:%llu queued:%llu srv:%llu %s - RUN: %.1fs   ",
			rate, jumps / 1e9, (unsigned long long)nbDP,
			(unsigned long long)pool->Queued(), (unsigned long long)pool->LastTotal(),
			pool->Online() ? "up" : "OFFLINE-spool", t);
	} else {
		fprintf(stdout, "\r%.1f MJ/s - %.3f GJumps - DP:%llu hit:%llu same:%llu Found:%u/%u - RUN: %.1fs | ~%.0fs   ",
			rate, jumps / 1e9, (unsigned long long)nbDP, (unsigned long long)nbCollide,
			(unsigned long long)nbSameHerd, nbSolved, (uint32_t)pubs.size(), t, eta);
	}
	fflush(stdout);
}

void Kangaroo::Search(int gpuId) {
	GPUEngine g(gpuId, maxFound);
	int totalK = g.GetNbKangaroo();
	int step = g.GetStepSize();
	chooseDP(totalK);

	fprintf(stdout, "GPU: %s\n", g.deviceName.c_str());
	fprintf(stdout, "Kangaroo: %u pubkeys, range 2^%d, %d kangaroos, dp=%d, JLP GPU (grp=%d run=%d)\n",
		(uint32_t)pubs.size(), rangeBits, totalK, dpBits, g.GetGroupSize(), step);
	fprintf(stdout, "Expected jumps to first hit ~ 2*sqrt(range/M) = %.3e\n",
		2.0 * std::sqrt((std::max)(1.0, rangeSize.ToDouble() / (double)pubs.size())));
	fprintf(stdout, "[keyspace] start=%s\n", ks->ksStart.GetBase16().c_str());
	fprintf(stdout, "[keyspace]   end=%s\n", ks->ksFinish.GetBase16().c_str());
	fflush(stdout);

	if (!g.SetJumps(jx, jy, jd))
		return;
	g.SetDPMask(dpMask);

	std::vector<uint64_t> px, py, distv;
	initKangaroos(totalK, px, py, distv);
	if (!g.SetKangaroos(px.data(), py.data(), distv.data()))
		return;

	fprintf(stdout, "GPU kangaroo started (%d grouped jumps/kangaroo/launch)\n", step);
	fflush(stdout);

	uint64_t jumps = 0;
	double t0 = Timer::get_tick();
	double tprint = t0;
	std::vector<KANG_DP> dps;

	while (nbSolved < pubs.size()) {
		if (pool && pool->Solved())
			break;
		if (!g.Launch(dps, true))
			break;
		jumps += (uint64_t)totalK * (uint64_t)step;
		if (pool) {
			std::vector<PoolDP> batch;
			batch.reserve(dps.size());
			for (size_t i = 0; i < dps.size(); i++) {
				PoolDP rec;
				memcpy(rec.x, dps[i].x, sizeof(rec.x));
				memcpy(rec.d, dps[i].d, sizeof(rec.d));
				rec.target = (herdMode >= 0)
					? (uint32_t)(dps[i].kIdx % pubs.size())
					: (uint32_t)((dps[i].kIdx / 2) % pubs.size());
				batch.push_back(rec);
				nbDP++;
			}
			pool->Enqueue(batch);
			pool->NoteJumps(jumps);
			if (pool->Solved())
				break;
		} else {
			for (size_t i = 0; i < dps.size(); i++) {
				processDP(dps[i]);
				if (nbSolved >= pubs.size())
					break;
			}
		}
		double now = Timer::get_tick();
		if (now - tprint >= 1.0) {
			printStats(jumps, t0);
			tprint = now;
		}
	}
	printStats(jumps, t0);
	if (pool)
		fprintf(stdout, "\nDone. %s\n", pool->Solved() ? "Server solved (key not shown here)." : "Disconnected or stopped.");
	else
		fprintf(stdout, "\nDone. Solved %u / %u\n", nbSolved, (uint32_t)pubs.size());
}

bool Kangaroo::CheckGPU(int gpuId) {
	GPUEngine g(gpuId, maxFound);
	int totalK = g.GetNbKangaroo();
	int grp = g.GetGroupSize();
	const int tPer = g.GetNbThreadPerGroup();
	chooseDP(totalK);

	fprintf(stdout, "GPU check: %s\n", g.deviceName.c_str());
	if (!g.SetJumps(jx, jy, jd))
		return false;
	g.SetDPMask(dpMask);

	std::vector<uint64_t> px, py, distv;
	initKangaroos(totalK, px, py, distv);
	std::vector<uint64_t> cpx = px, cpy = py, cd = distv;

	if (!g.SetKangaroos(px.data(), py.data(), distv.data()))
		return false;
	if (!g.GetKangaroos(px.data(), py.data(), distv.data()))
		return false;
	int faults = 0;
	Int one;
	one.SetInt32(1);

	for (int gi = 0; gi < grp; gi++) {
		int idx = gi * tPer;
		Int X, Y, D;
		X.SetInt32(0); Y.SetInt32(0); D.SetInt32(0);
		X.bits64[0] = cpx[(size_t)idx * 4 + 0];
		X.bits64[1] = cpx[(size_t)idx * 4 + 1];
		X.bits64[2] = cpx[(size_t)idx * 4 + 2];
		X.bits64[3] = cpx[(size_t)idx * 4 + 3];
		Y.bits64[0] = cpy[(size_t)idx * 4 + 0];
		Y.bits64[1] = cpy[(size_t)idx * 4 + 1];
		Y.bits64[2] = cpy[(size_t)idx * 4 + 2];
		Y.bits64[3] = cpy[(size_t)idx * 4 + 3];
		D.bits64[0] = cd[(size_t)idx * 2 + 0];
		D.bits64[1] = cd[(size_t)idx * 2 + 1];
		Point P(&X, &Y, &one);

		for (int run = 0; run < NB_RUN; run++) {
			uint32_t jmp = (uint32_t)P.x.bits64[0] & (NB_JUMP - 1);
			Int Jx, Jy;
			Jx.SetInt32(0); Jy.SetInt32(0);
			for (int k = 0; k < 4; k++) {
				Jx.bits64[k] = jx[jmp][k];
				Jy.bits64[k] = jy[jmp][k];
			}
			Point J(&Jx, &Jy, &one);
			P = secp->AddDirect(P, J);
			D.bits64[0] += jd[jmp][0];
			uint64_t c = (D.bits64[0] < jd[jmp][0]) ? 1 : 0;
			D.bits64[1] += jd[jmp][1] + c;
		}

		bool ok = P.x.bits64[0] == px[(size_t)idx * 4 + 0]
			&& P.x.bits64[1] == px[(size_t)idx * 4 + 1]
			&& P.x.bits64[2] == px[(size_t)idx * 4 + 2]
			&& P.x.bits64[3] == px[(size_t)idx * 4 + 3]
			&& D.bits64[0] == distv[(size_t)idx * 2 + 0]
			&& D.bits64[1] == distv[(size_t)idx * 2 + 1];
		if (!ok) {
			if (faults < 3) {
				fprintf(stdout, "MISMATCH idx=%d\n", idx);
				fprintf(stdout, "  CPU x=%s\n", P.x.GetBase16().c_str());
				fprintf(stdout, "  GPU x=%016llx%016llx%016llx%016llx\n",
					(unsigned long long)px[(size_t)idx * 4 + 3],
					(unsigned long long)px[(size_t)idx * 4 + 2],
					(unsigned long long)px[(size_t)idx * 4 + 1],
					(unsigned long long)px[(size_t)idx * 4 + 0]);
			}
			faults++;
		}
	}

	fprintf(stdout, "CPU/GPU compare: %d / %d faults (thread-0 group)\n", faults, grp);
	return faults == 0;
}
