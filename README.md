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

## Clean Linux shared-runtime check

This branch can validate the new source-only `libdlssnr-native.so` path without
downloading or launching the existing HIP implementation:

```sh
make native-probe
./nr-run \
  --model /path/to/nvngx_dlssnr.dll \
  --native-library /path/to/libdlssnr-native.so \
  --native-check
```

The check dynamically loads ABI v1, opens the raw user-provided DLL directly,
verifies its model inventory, creates the Vulkan runtime, binds the model, and
prints the selected GPU. It does **not** claim Stage-1 inference is wired yet.

For an integration smoke test, the launcher can keep the Linux runtime alive
while starting the Proton game:

```sh
make native-host
./nr-run \
  --model /path/to/nvngx_dlssnr.dll \
  --native-library /path/to/libdlssnr-native.so \
  --native-session -- %command%
```

`--native-session` starts an authenticated loopback host, exports its
port/token into the game environment, then cleans the host up when the game
exits. A compatible `dlss5-linux-bridge` native-control build must already be
installed in the game/prefix for the Windows NGX proxy to connect. This path
does not download the HIP runtime or stage its Windows HIP shim.

The current native-host capability mask is zero: this validates model ownership,
Linux Vulkan runtime creation, and the Proton control plane only. Native frame
evaluation/resource sharing is not implemented yet, so the proxy retains the
standard game output.

The clean shared-runtime path is currently pinned to the independently
reconstructed portable profile SHA-256
`6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927f`.
That is intentionally separate from the existing HIP path below, which uses a
different tested 310.8 DLL build. The two are not treated as interchangeable
until their normalized tensor sets and graph compatibility are verified.

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
