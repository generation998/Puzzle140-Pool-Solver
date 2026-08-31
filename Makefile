# Linux CUDA build: pool kangaroo worker
# On Vast:  make -j$(nproc) && ./JoinWorker.sh

SRC_CPP = Base58.cpp IntGroup.cpp main.cpp Random.cpp \
      Timer.cpp Int.cpp IntMod.cpp Point.cpp SECP256K1.cpp \
      Kangaroo.cpp PoolClient.cpp \
      hash/ripemd160.cpp hash/sha256.cpp hash/sha512.cpp \
      hash/ripemd160_sse.cpp hash/sha256_sse.cpp \
      Bech32.cpp Wildcard.cpp

OBJDIR = obj

OBJS = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Kangaroo.o PoolClient.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o \
        GPU/GPUEngine.o Bech32.o Wildcard.o)

CXX        ?= g++
NVCC       ?= nvcc
CUDA       ?= $(shell dirname $(shell dirname $(shell command -v nvcc 2>/dev/null || echo /usr/local/cuda/bin/nvcc)))

CXXFLAGS   = -mssse3 -msse4.1 -Wno-write-strings -O2 -std=c++14 -I. -I$(CUDA)/include
LFLAGS     = -lpthread -L$(CUDA)/lib64 -lcudart
NVCCFLAGS  = -maxrregcount=0 --compile --compiler-options -fPIC -m64 -O2 -I$(CUDA)/include \
             -gencode=arch=compute_60,code=sm_60 \
             -gencode=arch=compute_61,code=sm_61 \
             -gencode=arch=compute_75,code=sm_75 \
             -gencode=arch=compute_80,code=sm_80 \
             -gencode=arch=compute_86,code=sm_86 \
             -gencode=arch=compute_89,code=sm_89 \
             -gencode=arch=compute_90,code=sm_90 \
             -gencode=arch=compute_89,code=compute_89

all: VanitySearchKang

$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) $(NVCCFLAGS) -o $@ -c GPU/GPUEngine.cu

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

VanitySearchKang: $(OBJS)
	$(CXX) $(OBJS) $(LFLAGS) -o VanitySearchKang

$(OBJS): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/hash

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	mkdir -p $(OBJDIR)/GPU

$(OBJDIR)/hash: $(OBJDIR)
	mkdir -p $(OBJDIR)/hash

clean:
	rm -rf $(OBJDIR) VanitySearchKang
