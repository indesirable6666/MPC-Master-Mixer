# Host DSP benchmark

## Purpose and limits

The host-only benchmark in `tools/audio-engine-benchmark.cpp` measures the
average processing time of the portable DSP engine for several block sizes.
GitHub Actions runs it as a diagnostic step after compiling the host tests.

These numbers describe the CI virtual machine only. They are **not** an MPC
Live II performance result, a hard real-time guarantee, or evidence that the
audio engine can meet deadlines on the target. The benchmark reports an
average; it does not measure worst-case latency, scheduling jitter, xruns,
cache behavior on ARMv7, or device/backend overhead.

## Workload

- 16 mono input tracks, 48 kHz float samples
- Six-band EQ configured on every track
- Two enabled one-pole inserts per track (high-pass and low-pass)
- Four active Aux send buses
- 200 warm-up blocks followed by 1,500 measured blocks per block size
- Block sizes: 64, 128, 256 and 512 frames
- Optimized host build (`-O3 -DNDEBUG`)

The reported real-time budget is the duration represented by one block at
48 kHz:

`block budget (µs) = 1,000,000 × frame count / 48,000`

The reported load is average processing time divided by that block budget.
A value below 100% on this host means the average benchmark processing time
was shorter than the corresponding block duration on this particular run;
it says nothing about deadline misses or target-device behavior.

## Recorded CI baseline

Run: [Audio engine tests #45192459353](https://github.com/indesirable6666/MPC-Master-Mixer/actions/runs/45192459353)  
Commit: `369cf21079df716cea99921f308fc2e09e296e75`  
Date: 2026-11-23  
Runner: Ubuntu 22.04, x86_64 virtual machine, 4 vCPUs, AMD EPYC 7763

| Block frames | Average time per block | 48 kHz block budget | Average budget load | Throughput |
|---:|---:|---:|---:|---:|
| 64 | 53.37 µs | 1,333.33 µs | 4.00% | 1,199,235 frames/s |
| 128 | 106.69 µs | 2,666.67 µs | 4.00% | 1,199,739 frames/s |
| 256 | 222.23 µs | 5,333.33 µs | 4.17% | 1,151,971 frames/s |
| 512 | 445.73 µs | 10,666.67 µs | 4.18% | 1,148,672 frames/s |

This is one CI observation, not a stable cross-machine benchmark. Compare
future results only when the runner class, compiler flags, workload and
benchmark revision are known. Do not use these figures to choose an MPC audio
buffer size or claim ARMv7 headroom.

## Reproduce locally

Configure and build the optional benchmark:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --target audio_engine_benchmark --parallel
./build/host/audio_engine_benchmark
```

The executable is diagnostic. The C++ host tests remain the correctness
checks; benchmark timing is deliberately not a CI pass/fail threshold.
