# Bitcoin Puzzle 140 — GPU pool worker

Join the public kangaroo distinguished-point hunt for [Bitcoin Puzzle 140](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx).

This is interval ECDLP on the published puzzle-140 pubkey.

## Windows (ready binary)

Needs an NVIDIA GPU, a current Game Ready/Studio driver, and the [VC++ x64 redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

```powershell
git clone https://github.com/generation998/Puzzle140-Pool-Worker.git
cd Puzzle140-Pool-Worker
.\JoinWorker.bat
```

`JoinWorker.bat` starts one process per NVIDIA GPU. Pin a single card with `.\JoinWorker.bat 72.62.76.118 1`.

## Linux / Vast.ai (build on the instance)

The `.exe` does not run on Linux. Build the CUDA worker **on the GPU box** (needs `nvcc` and `g++`). Use a CUDA 12.x devel image.

If GitHub works:

```bash
git clone https://github.com/generation998/Puzzle140-Pool-Worker.git
cd Puzzle140-Pool-Worker
chmod +x JoinWorker.sh
./JoinWorker.sh
```

`JoinWorker.sh` runs `make`, then starts one process per NVIDIA GPU (`nvidia-smi` or `CUDA_VISIBLE_DEVICES`). Pin a single card with `./JoinWorker.sh 72.62.76.118 1`. Limit a subset with `GPUS=0,3 ./JoinWorker.sh`.

If `git`/`wget` to GitHub hangs (common on some Vast hosts), copy this folder from your PC:

```powershell
scp -P YOUR_SSH_PORT -r \Puzzle140-Pool-Worker root@VAST_HOST:/workspace/
```

Then on the instance:

```bash
cd /workspace/Puzzle140-Pool-Worker
chmod +x JoinWorker.sh
./JoinWorker.sh
```

## How the pool works

Every GPU walks the same puzzle-140 range. Each client is tame or wild only. Distinguished points go to the server. A match is recovered only on the server. Two clients are enough for both herds.

Expected work is about `2^70.5` jumps. More GPUs raise the combined rate. They do not split the range.

## License

AGPLv3, derived from [JeanLucPons/VanitySearch](https://github.com/JeanLucPons/VanitySearch).
