#include "Timer.h"
#include "Kangaroo.h"
#include "PoolClient.h"
#include "SECP256k1.h"
#include <string.h>
#include <stdio.h>
#include <stdexcept>
#include <iostream>
#include <cctype>
#include <vector>

#define RELEASE "2.2-kangaroo JLP-GPU multi-pubkey"

using namespace std;

void printUsage() {
	printf("VanitySearchKang [-v] [-gpuId N] [-pubkeys file] [-o output] [-start HEX] [-end HEX] [-range N] [-dp N] [-m N]\n\n");
	printf(" Pollard kangaroo (multi-pubkey). GPU does EC jumps only  no HASH160.\n");
	printf(" Each pubkey must have its private key inside [start, end].\n");
	printf(" Does not search Bitcoin addresses (1/3/bc1). Use the fast/No3 folders for those.\n\n");
	printf(" -pubkeys file  One compressed (02/03+32B) or uncompressed (04+64B) hex key per line\n");
	printf(" -start HEX     Range start (default 1)\n");
	printf(" -end HEX       Inclusive end. If set, -range is ignored\n");
	printf(" -range N       End = start + 2^N - 1\n");
	printf(" -dp N          Distinguished-point bits (auto from range if omitted)\n");
	printf(" -m N           DP buffer per kernel, default 262144 (multiple of 65536)\n");
	printf(" -o file        Append found keys (default found.txt). Ignored in -pool mode.\n");
	printf(" -gpuId N       GPU index, default 0\n");
	printf(" -pool host[:port]  Join a DP pool. This GPU never reconstructs or prints the key.\n");
	printf(" -token STR     Optional. Only if the server set a token in config.json\n");
	printf(" -worker NAME   Name shown on the server status page\n");
	printf(" -v             Version\n");
	exit(-1);
}

int getInt(const char* name, char* v) {
	try {
		return std::stoi(string(v));
	} catch (std::invalid_argument&) {
		fprintf(stderr, "[ERROR] Invalid %s argument, number expected\n", name);
		exit(-1);
	}
}

void parseHexPriv(const string& text, Int& out, const char* name) {
	string item = text;
	if (item.size() >= 2 && item[0] == '0' && (item[1] == 'x' || item[1] == 'X'))
		item = item.substr(2);
	if (item.empty() || item.length() > 64) {
		fprintf(stderr, "[ERROR] %s: invalid privkey (1-64 hex characters)\n", name);
		exit(-1);
	}
	for (size_t i = 0; i < item.length(); i++) {
		if (!isxdigit((unsigned char)item[i])) {
			fprintf(stderr, "[ERROR] %s: invalid hex digit\n", name);
			exit(-1);
		}
	}
	std::vector<char> buf(item.begin(), item.end());
	buf.push_back(0);
	out.SetBase16(&buf[0]);
}

void checkKeySpace(KANG_PARAM* bc, Int& maxKey) {
	if (bc->ksStart.IsGreater(&maxKey) || bc->ksFinish.IsGreater(&maxKey)) {
		fprintf(stderr, "[ERROR] START/END IsGreater %s \n", maxKey.GetBase16().c_str());
		exit(-1);
	}
	if (bc->ksFinish.IsLowerOrEqual(&bc->ksStart)) {
		fprintf(stderr, "[ERROR] END IsLowerOrEqual START \n");
		exit(-1);
	}
}

int main(int argc, char** argv) {
	Timer::Init();

	Secp256K1* secp = new Secp256K1();
	secp->Init();

	int gpuId = 0;
	string pubkeyFile = "";
	string outputFile = "found.txt";
	uint32_t maxFound = 65536 * 4;
	int range = 32;
	bool hasRange = false;
	bool hasEnd = false;
	int dpBits = -1;
	bool doCheck = false;
	string start = "1";
	string endHex = "";
	string poolSpec = "";
	string poolToken = "";
	string workerName = "gpu";

	KANG_PARAM ks;
	Int maxKey;
	maxKey.SetBase16((char*)"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");
	ks.ksStart.SetInt32(1);
	ks.ksFinish.Set(&maxKey);

	int a = 1;
	while (a < argc) {
		if (strcmp(argv[a], "-gpuId") == 0) {
			gpuId = getInt("gpuId", argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-v") == 0) {
			printf("%s\n", RELEASE);
			return 0;
		} else if (strcmp(argv[a], "-o") == 0) {
			outputFile = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-start") == 0) {
			start = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-end") == 0) {
			endHex = string(argv[++a]);
			hasEnd = true;
			a++;
		} else if (strcmp(argv[a], "-range") == 0) {
			range = getInt("range", argv[++a]);
			hasRange = true;
			a++;
		} else if (strcmp(argv[a], "-dp") == 0) {
			dpBits = getInt("dp", argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-m") == 0) {
			maxFound = (uint32_t)getInt("maxFound", argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-check") == 0) {
			doCheck = true;
			a++;
		} else if (strcmp(argv[a], "-pubkeys") == 0) {
			pubkeyFile = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-pool") == 0) {
			poolSpec = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-token") == 0) {
			poolToken = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-worker") == 0) {
			workerName = string(argv[++a]);
			a++;
		} else if (strcmp(argv[a], "-h") == 0 || strcmp(argv[a], "-help") == 0) {
			printUsage();
		} else {
			printf("Unexpected %s argument\n", argv[a]);
			printUsage();
		}
	}

	fprintf(stdout, "VanitySearch-Bitcrack-Kangaroo v" RELEASE "\n");

	if (!poolSpec.empty()) {
		string host = poolSpec;
		int port = 17403;
		size_t colon = poolSpec.rfind(':');
		if (colon != string::npos) {
			host = poolSpec.substr(0, colon);
			string portStr = poolSpec.substr(colon + 1);
			port = getInt("pool port", (char*)portStr.c_str());
		}
		PoolClient client;
		if (!client.Connect(host, port)) {
			fprintf(stderr, "[pool] connect %s:%d failed  start the server first\n", host.c_str(), port);
			return 1;
		}
		PoolJob job;
		if (!client.Hello(poolToken, workerName, job))
			return 1;
		parseHexPriv(job.start, ks.ksStart, "start");
		parseHexPriv(job.end, ks.ksFinish, "end");
		checkKeySpace(&ks, maxKey);
		Kangaroo kang(secp, job.pubkey, outputFile, maxFound, job.dpBits, &ks, job.herd, false);
		if (!kang.ApplyJobJumps(job))
			return 1;
		char spool[256];
		snprintf(spool, sizeof(spool), "pool_spool_gpu%d.bin", gpuId);
		client.StartPump(host, port, poolToken, workerName, job.herd, spool);
		kang.SetPool(&client);
		kang.Search(gpuId);
		client.Drain(30.0);
		client.StopPump();
		return 0;
	}

	if (pubkeyFile.empty()) {
		fprintf(stderr, "[ERROR] -pubkeys file is required\n");
		printUsage();
	}

	parseHexPriv(start, ks.ksStart, "start");
	if (hasEnd) {
		if (hasRange)
			fprintf(stdout, "[WARNING] -end is set, -range is ignored\n");
		parseHexPriv(endHex, ks.ksFinish, "end");
	} else {
		if (!hasRange)
			range = 32;
		if (range > 255)
			range = 255;
		Int Range;
		Range.SetInt32(1);
		for (int i = 0; i < range; i++)
			Range.Mult(2);
		Range.SubOne();
		ks.ksFinish.Set(&ks.ksStart);
		ks.ksFinish.Add(&Range);
	}
	checkKeySpace(&ks, maxKey);

	Kangaroo kang(secp, pubkeyFile, outputFile, maxFound, dpBits, &ks);
	if (doCheck)
		return kang.CheckGPU(gpuId) ? 0 : 1;
	kang.Search(gpuId);
	return 0;
}
