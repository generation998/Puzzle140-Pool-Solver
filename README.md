# Bitcoin Puzzle 140 — GPU pool worker

Join the public kangaroo **distinguished-point** hunt for [Bitcoin Puzzle 140](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx).


This is interval ECDLP on the published puzzle-140 pubkey. It is not a wallet cracker and does not search Bitcoin addresses.

## What you need

- Windows 10/11 64-bit
- An **NVIDIA GPU**
- A current [NVIDIA Game Ready / Studio driver](https://www.nvidia.com/Download/index.aspx)
- [Visual C++ Redistributable x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

You do **not** need Python, Visual Studio, or the CUDA Toolkit.

## How to join

1. Download this repository (Code → Download ZIP) and unzip it.
2. Open PowerShell in that folder.
3. Run:

```powershell
.\JoinWorker.bat
```

That connects to `72.62.76.118:17403` on GPU `0`.

Second GPU on the same PC:

```powershell
.\JoinWorker.bat 72.62.76.118 1
```

Leave the window open. You should see `[pool] job herd=tame` or `wild` and a jump rate (MJ/s). `OFFLINE-spool` means the server blipped; the GPU keeps walking and uploads when it is back.

Press Ctrl+C then **Y** to stop.

## How the pool works

Every GPU walks the **same** puzzle-140 range with the same jump table. Each client is assigned **tame** or **wild** only. Distinguished points go to the server. A match is recovered **only on the server**.

You need both herds in the pool (any two clients is enough). One machine still helps.

Expected work is about `2^70.5` jumps. One GPU will not finish this in a lifetime. More GPUs sharing this table raise the combined rate. They do not split the range.

## Files

| File | Role |
|---|---|
| `JoinWorker.bat` | Start the worker |
| `tools\VanitySearchKang3.exe` | CUDA kangaroo client |

Do not copy anything from a server `secrets` or `work` folder onto your PC.

## License

AGPLv3. Binary is derived from [JeanLucPons/VanitySearch](https://github.com/JeanLucPons/VanitySearch) with a pool client. Corresponding source lives in the `VanitySearch-Bitcrack-kangaroo` tree used to build `VanitySearchKang3.exe`.
