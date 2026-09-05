# Same-frame bring-up

The released bridge remains asynchronous. This probe is a prerequisite, not
a synchronization fix or a model/game test.

The pinned host reports that inline mode requires zero-copy interop and
falls back to asynchronous execution without it. Our three external-memory
HIP entry points currently return unsupported. Changing `Inline=1` alone
therefore does not fix the stale residual.

`interop.cpp` matches Vulkan and HIP adapters by PCI address, exports dedicated
Vulkan buffer memory as an opaque FD, and imports it into HIP. In 16 rounds:

1. Vulkan fills the buffer with a changing pattern and releases ownership.
2. After a bounded Vulkan fence wait, HIP reads and verifies all 1,024 words.
3. HIP writes a different pattern and synchronizes.
4. Vulkan reacquires ownership, copies to a separate readback buffer, waits
   for completion, and verifies all 1,024 words.

Passed on RX 9070 XT / RADV GFX1201 / HIP 6.4.1 on 2026-09-05. This validates
native allocation sharing and explicitly serialized visibility, not concurrent
GPU flag polling, cross-process handle transport, or in-game frame alignment.

Build with Linux C++, Vulkan headers/loader, and HIP headers/libraries:

```sh
make build/interop-probe ROCM_ROOT=/path/to/rocm
LD_LIBRARY_PATH=/path/to/runtime/lib timeout -k 5 30 build/interop-probe
```

Next gates before enabling same-frame mode:

- Resolve Proton's D3D12 shared-resource handle to its underlying Linux GPU
  allocation without pretending a Windows handle is a Linux FD.
- Transfer that FD to the backend through authenticated Unix-domain IPC
  (SCM_RIGHTS); numeric FDs cannot be sent over the current TCP protocol.
- Implement external-memory import, offset/size-checked mapping, and cleanup.
- Verify cross-API flag visibility and bounded waits used by the inline host.
- Test the complete path in an isolated Proton probe, then DS2, checking
  source/output frame correspondence and frame times. Keep asynchronous mode
  available as the fallback; do not silently label it synchronized.

The alpha release and the working game installation are unchanged by this test.
