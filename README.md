# dlssnr-proton

Experimental DLSS Neural Rendering on RDNA4 through Proton. Tested with an
RX 9070 XT, SteamOS 3.8, and Death Stranding 2 using FSR.

Extract the release and set the game's Steam launch options:

```sh
"/path/to/nr-run" --model "/path/to/nvngx_dlssnr.dll" %command%
```

Enable FSR in-game. **End** opens the renderer overlay. This version uses
asynchronous CPU staging: neural updates take about 55–65 ms and visibly lag
moving gameplay. It is not synchronized 60 FPS neural rendering.

Required model: **NVIDIA DLSSNR 310.8.0.0**, tested filename
`nvngx_dlssnr.approx-fp16-sm_75-sm_86-sm_89-sm_120.dll`, SHA-256:

```text
dcc0dc2414aedec4a8e084647070383be068554042587180c20c784d4772d36f
```

First launch downloads the pinned [AMD runtime](https://github.com/danielblnc/DLSS-NR-on-AMD/releases/tag/v0.2.10)
and user-space HIP libraries to the user cache. No root or system ROCm install
is needed. Linux x86-64, Python 3.9+, GNU tar/zstd, glibc 2.38+ and the normal
AMD Vulkan/KFD driver stack are required. Other distributions are unverified.
Existing mod files are preserved; conflicting installations are refused.

Build: `make ROCM_ROOT=/path/to/rocm` with a Linux C++ compiler, HIP headers,
and MinGW-w64. `make test` runs the source tests; `make release` packages the
launcher and bridge binaries. The NVIDIA DLL, AMD runtime, kernels, and
model weights are not distributed here. GPLv3.
