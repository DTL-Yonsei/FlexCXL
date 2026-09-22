# FlexCXL single-host simulator

This repository contains the single-host FlexCXL gem5 model.

## Build

Build the optimized gem5 binary:

```bash
scons build/X86/gem5.opt -j$(nproc) USE_SYSTEMC=n --ignore-style
```

The boot test expects the following files by default:

- Guest CXL tools: `/home/ndctl/build/cxl/cxl` and
  `/home/ndctl/build/daxctl/daxctl`

The host paths can be overridden with `DISK_IMAGE`, `KERNEL_PATH`, or the
corresponding command-line options.

## CXL boot test

The test boots Linux with `build/X86/gem5.opt`, loads the CXL drivers,
creates `region0` when necessary, converts its DAX device to system RAM,
prints the CXL/NUMA state, and exits without running a benchmark.

Run the default x8, 500 MHz link configuration:

```bash
./booting.sh
```


The guest serial output is written to `<output-dir>/system.pc.com_1.device`.
A successful test prints:

```text
[rcS] PASS: CXL DRAM is available as NUMA node 1
```

