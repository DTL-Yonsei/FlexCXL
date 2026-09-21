# Copyright (c) 2010-2013, 2016, 2019-2020 ARM Limited
# Copyright (c) 2020 Barkhausen Institut
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2012-2014 Mark D. Hill and David A. Wood
# Copyright (c) 2009-2011 Advanced Micro Devices, Inc.
# Copyright (c) 2006-2007 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import os
import sys
from m5.util.dot_writer import do_dot
import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, convert, fatal, warn
from m5.util.fdthelper import *
from gem5.isas import ISA
from gem5.runtime import get_runtime_isa
from m5.util.dot_writer import do_dot

addToPath("../../")

from ruby import Ruby

from x86_FS_config import *
from common.SysPaths import *
from common.Benchmarks import *
from common import Simulation
from common import CacheConfig
from common import CpuConfig
from common import MemConfig
from common import ObjectList
from common.Caches import *
from common.CxlPartition import partition_cxl_memory
from common import Options


def cmd_line_template():
    if args.command_line and args.command_line_file:
        print(
            "Error: --command-line and --command-line-file are "
            "mutually exclusive"
        )
        sys.exit(1)
    if args.command_line:
        return args.command_line
    if args.command_line_file:
        return open(args.command_line_file).read().strip()
    return None


def parse_ratio_weights(ratio_csv):
    parts = [p.strip() for p in ratio_csv.split(",")]
    if not parts or any(p == "" for p in parts):
        fatal("Invalid --host-cxl-ratios format: '%s'", ratio_csv)

    weights = []
    for p in parts:
        try:
            val = float(p)
        except ValueError:
            fatal("Invalid ratio value '%s' in --host-cxl-ratios", p)
        if val < 0:
            fatal("--host-cxl-ratios values must be >= 0")
        weights.append(val)
    return weights


def parse_priority_ratio_weights(ratio_csv):
    parts = [p.strip() for p in ratio_csv.split(",")]
    if not parts or any(p == "" for p in parts):
        fatal("Invalid --priority-host-ratios format: '%s'", ratio_csv)

    weights = []
    for p in parts:
        try:
            val = int(p)
        except ValueError:
            fatal("Invalid value '%s' in --priority-host-ratios", p)
        if val < 0:
            fatal("--priority-host-ratios values must be >= 0")
        weights.append(val)
    return weights


def normalize_weights_to_sum(weights, target_sum=10):
    total = sum(weights)
    if total <= 0:
        fatal("--host-cxl-ratios sum must be > 0")

    scaled = [w * target_sum / total for w in weights]
    ratios = [int(v) for v in scaled]
    remain = target_sum - sum(ratios)
    frac = [scaled[i] - ratios[i] for i in range(len(weights))]
    order = sorted(
        range(len(weights)),
        key=lambda i: (frac[i], weights[i], -i),
        reverse=True,
    )
    for i in range(remain):
        ratios[order[i]] += 1
    return ratios


def ensure_nonzero_ratios(ratios):
    ratios = list(ratios)
    if len(ratios) <= 1:
        return ratios

    for idx, val in enumerate(ratios):
        if val < 0:
            fatal("Internal error: negative ratio at index %d", idx)

    for idx, val in enumerate(ratios):
        if val != 0:
            continue
        donor = max(range(len(ratios)), key=lambda i: ratios[i])
        if ratios[donor] <= 1:
            fatal(
                "Unable to auto-adjust host ratios to keep all hosts non-zero: %s",
                ",".join(str(x) for x in ratios),
            )
        ratios[donor] -= 1
        ratios[idx] = 1

    return ratios


def resolve_cxl_base_addr(mem_size):
    dram_size = convert.toMemorySize(mem_size)
    three_gib = convert.toMemorySize("3GB")
    four_gib = convert.toMemorySize("4GB")
    if dram_size <= three_gib:
        ram_end = dram_size
    else:
        ram_end = four_gib + (dram_size - three_gib)
    return align_up(max(four_gib, ram_end), CXL_FW_ALIGN)


def system_cpu_list(system):
    cpus = getattr(system, "cpu", [])
    if isinstance(cpus, (list, tuple)):
        return list(cpus)
    if hasattr(cpus, "__len__") and not hasattr(cpus, "descendants"):
        return list(cpus)
    return [cpus]


def assign_kvm_event_queues(systems):
    next_eventq_index = 1
    for system in systems:
        if system is None:
            continue
        for cpu in system_cpu_list(system):
            # Child objects usually inherit the parent's event queue. Override
            # that and use the same event queue for all devices.
            for obj in cpu.descendants():
                obj.eventq_index = 0
            cpu.eventq_index = next_eventq_index
            next_eventq_index += 1
        system.kvm_vm = KvmVM()


def parse_host_scripts(value):
    if value is None:
        return []
    return [entry.strip() for entry in value.split(",") if entry.strip()]


DEFER_RELEASE_MARKER = "# FLEXCXL_DEFER_RELEASE"


def _deferred_ready_block(host_idx, ready_filename):
    ready_file = "/tmp/flexcxl_ready_host%d" % host_idx
    return """ready_file="{ready_file}"
printf 'ready host {host_idx}\\n' > "$ready_file"
/sbin/m5 writefile "$ready_file" "{ready_filename}"
""".format(
        host_idx=host_idx,
        ready_file=ready_file,
        ready_filename=ready_filename,
    )


def _deferred_payload_block(host_idx, payload):
    delimiter = "__FLEXCXL_DEFERRED_PAYLOAD_%d__" % host_idx
    while delimiter in payload:
        delimiter += "_X"

    payload_file = "/tmp/flexcxl_deferred_payload_host%d" % host_idx
    return """payload_file="{payload_file}"
cat > "$payload_file" <<'{delimiter}'
{payload}
{delimiter}
chmod +x "$payload_file"
if head -n 1 "$payload_file" | grep -q '^#!'; then
    exec "$payload_file"
fi
exec /bin/sh "$payload_file"
""".format(
        delimiter=delimiter,
        payload=payload,
        payload_file=payload_file,
    )


def _payload_after_first_m5_exit(payload):
    lines = payload.splitlines(True)
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "/sbin/m5 exit" or stripped.startswith(
            "/sbin/m5 exit "
        ):
            return "".join(lines[idx + 1 :]).lstrip()
    return payload


def _payload_with_completion_before_last_m5_exit(payload, completion_block):
    lines = payload.splitlines(True)
    for idx in range(len(lines) - 1, -1, -1):
        stripped = lines[idx].strip()
        if stripped == "/sbin/m5 exit" or stripped.startswith(
            "/sbin/m5 exit "
        ):
            indent = lines[idx][: len(lines[idx]) - len(lines[idx].lstrip())]
            indented_block = "".join(
                indent + line if line.strip() else line
                for line in completion_block.splitlines(True)
            )
            return "".join(lines[:idx]) + indented_block + "".join(lines[idx:])

    fatal("Deferred host payload has no workload-completion m5 exit")


def _deferred_completion_block(host_idx, completion_filename):
    return """completion_file="/tmp/flexcxl_complete_host{host_idx}"
printf 'complete host {host_idx}\\n' > "$completion_file"
/sbin/m5 writefile "$completion_file" "{completion_filename}"
sync
sleep 0.1
""".format(
        host_idx=host_idx,
        completion_filename=completion_filename,
    )


def create_deferred_host_script(host_idx, original_script, outdir):
    try:
        original_script_path = script(original_script)
        with open(original_script_path) as fp:
            payload = fp.read()
    except IOError as exc:
        fatal(
            "Unable to read deferred host%d script '%s': %s",
            host_idx,
            original_script,
            exc,
        )

    outdir = os.path.abspath(outdir)
    os.makedirs(outdir, exist_ok=True)
    wrapper_path = os.path.join(outdir, "deferred_host%d.rcS" % host_idx)
    release_path = os.path.join(
        outdir, "deferred_host%d.release.rcS" % host_idx
    )
    ready_filename = "deferred_host%d.ready" % host_idx
    ready_path = os.path.join(outdir, ready_filename)
    completion_filename = "deferred_host%d.complete" % host_idx
    completion_path = os.path.join(outdir, completion_filename)
    if os.path.exists(ready_path):
        os.remove(ready_path)
    if os.path.exists(completion_path):
        os.remove(completion_path)
    payload = _payload_after_first_m5_exit(payload)
    payload = _payload_with_completion_before_last_m5_exit(
        payload,
        _deferred_completion_block(host_idx, completion_filename),
    )

    ready_block = _deferred_ready_block(host_idx, ready_filename)
    payload_block = _deferred_payload_block(host_idx, payload)

    wrapper = """#!/bin/sh
set -eu
signal_file="/tmp/flexcxl_defer_signal_host{host_idx}"
echo "FlexCXL host {host_idx}: boot complete; waiting for deferred script release."
{ready_block}/sbin/m5 exit
while true; do
    /sbin/m5 readfile > "$signal_file"
    if grep -qx '{marker}' "$signal_file"; then
        break
    fi
    sleep 0.2
done
echo "FlexCXL host {host_idx}: release received; running deferred script."
{payload_block}""".format(
        host_idx=host_idx,
        marker=DEFER_RELEASE_MARKER,
        ready_block=ready_block,
        payload_block=payload_block,
    )

    release = """#!/bin/sh
{marker}
set -eu
echo "FlexCXL host {host_idx}: released; running deferred script."
{payload_block}""".format(
        host_idx=host_idx,
        marker=DEFER_RELEASE_MARKER,
        payload_block=payload_block,
    )

    with open(wrapper_path, "w") as fp:
        fp.write(wrapper)
    with open(release_path, "w") as fp:
        fp.write(release)

    return wrapper_path, release_path, ready_path, completion_path


def set_pci_host_id(system, host_idx):
    if hasattr(system, "pc") and hasattr(system.pc, "pci_host"):
        system.pc.pci_host.host_id = host_idx


def build_test_system(np):

    cmdline = cmd_line_template()
    isa = get_runtime_isa()
    if isa == ISA.MIPS:
        test_sys = makeLinuxMipsSystem(test_mem_mode, bm[0], cmdline=cmdline)
    elif isa == ISA.SPARC:
        test_sys = makeSparcSystem(test_mem_mode, bm[0], cmdline=cmdline)
    elif isa == ISA.RISCV:
        test_sys = makeBareMetalRiscvSystem(
            test_mem_mode, bm[0], cmdline=cmdline
        )
    elif isa == ISA.X86:
        test_sys = makeLinuxX86System(
            test_mem_mode,
            np,
            bm[0],
            args.ruby,
            cmdline=cmdline,
            is_second=False,
        )
    elif isa == ISA.ARM:
        test_sys = makeArmSystem(
            test_mem_mode,
            args.machine_type,
            np,
            bm[0],
            args.dtb_filename,
            bare_metal=args.bare_metal,
            cmdline=cmdline,
            external_memory=args.external_memory_system,
            ruby=args.ruby,
            vio_9p=args.vio_9p,
            bootloader=args.bootloader,
        )
        if args.enable_context_switch_stats_dump:
            test_sys.enable_context_switch_stats_dump = True
    else:
        fatal("Incapable of building %s full system!", isa.name)

    set_pci_host_id(test_sys, 0)

    # Set the cache line size for the entire system
    test_sys.cache_line_size = args.cacheline_size

    # Create a top-level voltage domain
    test_sys.voltage_domain = VoltageDomain(voltage=args.sys_voltage)

    # Create a source clock for the system and set the clock period
    test_sys.clk_domain = SrcClockDomain(
        clock=args.sys_clock, voltage_domain=test_sys.voltage_domain
    )

    # Create a CPU voltage domain
    test_sys.cpu_voltage_domain = VoltageDomain()

    # Create a source clock for the CPUs and set the clock period
    test_sys.cpu_clk_domain = SrcClockDomain(
        clock=args.cpu_clock, voltage_domain=test_sys.cpu_voltage_domain
    )

    if buildEnv["USE_RISCV_ISA"]:
        test_sys.workload.bootloader = args.kernel
    elif args.kernel is not None:
        test_sys.workload.object_file = binary(args.kernel)

    if args.script is not None:
        test_sys.readfile = args.script

    test_sys.init_param = args.init_param

    # For now, assign all the CPUs to the same clock domain
    test_sys.cpu = [
        TestCPUClass(clk_domain=test_sys.cpu_clk_domain, cpu_id=i)
        for i in range(np)
    ]

    print("esj cpu clock =", args.cpu_clock)
    print("esj sys clock =", args.sys_clock)
    print("esj CXL_clock =", args.CXL_clock)

    # esj 2024-11-27 add cxl ctrl clock
    # esj 2025-02-06
    if args.cxl_mode:
        test_sys.CXL_ctrl.clk_domain = SrcClockDomain(
            clock=args.CXL_clock, voltage_domain=test_sys.voltage_domain
        )
        test_sys.CXL_ctrl2.clk_domain = SrcClockDomain(
            clock=args.CXL_clock, voltage_domain=test_sys.voltage_domain
        )
        test_sys.CXL_ctrl3.clk_domain = SrcClockDomain(
            clock=args.CXL_clock, voltage_domain=test_sys.voltage_domain
        )

    if args.ruby:
        bootmem = getattr(test_sys, "_bootmem", None)
        Ruby.create_system(
            args,
            True,
            test_sys,
            test_sys.iobus,
            test_sys._dma_ports,
            bootmem,
            is_second=False,
        )

        # Create a seperate clock domain for Ruby
        test_sys.ruby.clk_domain = SrcClockDomain(
            clock=args.ruby_clock, voltage_domain=test_sys.voltage_domain
        )

        print("esj ruby clock =", args.ruby_clock)

        # Connect the ruby io port to the PIO bus,
        # assuming that there is just one such port.
        test_sys.iobus.mem_side_ports = test_sys.ruby._io_port.in_ports

        for (i, cpu) in enumerate(test_sys.cpu):
            #
            # Tie the cpu ports to the correct ruby system ports
            #
            cpu.clk_domain = test_sys.cpu_clk_domain
            cpu.createThreads()
            cpu.createInterruptController()

            test_sys.ruby._cpu_ports[i].connectCpuPorts(cpu)

    else:
        if args.caches or args.l2cache:
            # By default the IOCache runs at the system clock
            test_sys.iocache = IOCache(addr_ranges=test_sys.mem_ranges)
            test_sys.iocache.cpu_side = test_sys.iobus.mem_side_ports
            test_sys.iocache.mem_side = test_sys.membus.cpu_side_ports
        elif not args.external_memory_system:
            test_sys.iobridge = Bridge(
                delay="50ns", ranges=test_sys.mem_ranges
            )
            test_sys.iobridge.cpu_side_port = test_sys.iobus.mem_side_ports
            test_sys.iobridge.mem_side_port = test_sys.membus.cpu_side_ports

        # Sanity check
        if args.simpoint_profile:
            if not ObjectList.is_noncaching_cpu(TestCPUClass):
                fatal("SimPoint generation should be done with atomic cpu")
            if np > 1:
                fatal(
                    "SimPoint generation not supported with more than one CPUs"
                )

        for i in range(np):
            if args.simpoint_profile:
                test_sys.cpu[i].addSimPointProbe(args.simpoint_interval)
            if args.checker:
                test_sys.cpu[i].addCheckerCpu()
            if not ObjectList.is_kvm_cpu(TestCPUClass):
                if args.bp_type:
                    bpClass = ObjectList.bp_list.get(args.bp_type)
                    test_sys.cpu[i].branchPred = bpClass()
                if args.indirect_bp_type:
                    IndirectBPClass = ObjectList.indirect_bp_list.get(
                        args.indirect_bp_type
                    )
                    test_sys.cpu[
                        i
                    ].branchPred.indirectBranchPred = IndirectBPClass()
            test_sys.cpu[i].createThreads()

        # If elastic tracing is enabled when not restoring from checkpoint and
        # when not fast forwarding using the atomic cpu, then check that the
        # TestCPUClass is DerivO3CPU or inherits from DerivO3CPU. If the check
        # passes then attach the elastic trace probe.
        # If restoring from checkpoint or fast forwarding, the code that does this for
        # FutureCPUClass is in the Simulation module. If the check passes then the
        # elastic trace probe is attached to the switch CPUs.
        if (
            args.elastic_trace_en
            and args.checkpoint_restore == None
            and not args.fast_forward
        ):
            CpuConfig.config_etrace(TestCPUClass, test_sys.cpu, args)

        print("config_cache")
        CacheConfig.config_cache(args, test_sys)
        print("config_mem")
        MemConfig.config_mem(args, test_sys)

    return test_sys


def build_second_system(np, bm_index=1, cpu_id=1):

    # TestCPUClass = X86O3CPU
    # test_mem_mode = "timing"

    cmdline = cmd_line_template()
    isa = get_runtime_isa()
    if isa == ISA.MIPS:
        test_sys = makeLinuxMipsSystem(
            test_mem_mode, bm[bm_index], cmdline=cmdline
        )
    elif isa == ISA.SPARC:
        test_sys = makeSparcSystem(
            test_mem_mode, bm[bm_index], cmdline=cmdline
        )
    elif isa == ISA.RISCV:
        test_sys = makeBareMetalRiscvSystem(
            test_mem_mode, bm[bm_index], cmdline=cmdline
        )
    elif isa == ISA.X86:
        # esj 2025-04-14
        # test_sys = makeLinuxX86System(
        #     test_mem_mode, np, bm[1], args.ruby, cmdline=cmdline
        # )
        test_sys = makeLinuxX86System(
            test_mem_mode,
            np,
            bm[bm_index],
            args.ruby,
            cmdline=cmdline,
            is_second=True,
        )
    elif isa == ISA.ARM:
        test_sys = makeArmSystem(
            test_mem_mode,
            args.machine_type,
            np,
            bm[bm_index],
            args.dtb_filename,
            bare_metal=args.bare_metal,
            cmdline=cmdline,
            external_memory=args.external_memory_system,
            ruby=args.ruby,
            vio_9p=args.vio_9p,
            bootloader=args.bootloader,
        )
        if args.enable_context_switch_stats_dump:
            test_sys.enable_context_switch_stats_dump = True
    else:
        fatal("Incapable of building %s full system!", isa.name)

    set_pci_host_id(test_sys, bm_index)

    # Set the cache line size for the entire system
    test_sys.cache_line_size = args.cacheline_size

    # Create a top-level voltage domain
    test_sys.voltage_domain = VoltageDomain(voltage=args.sys_voltage)

    # Create a source clock for the system and set the clock period
    test_sys.clk_domain = SrcClockDomain(
        clock=args.sys_clock, voltage_domain=test_sys.voltage_domain
    )

    # Create a CPU voltage domain
    test_sys.cpu_voltage_domain = VoltageDomain()

    # Create a source clock for the CPUs and set the clock period
    test_sys.cpu_clk_domain = SrcClockDomain(
        clock=args.cpu_clock, voltage_domain=test_sys.cpu_voltage_domain
    )

    if buildEnv["USE_RISCV_ISA"]:
        test_sys.workload.bootloader = args.kernel
    elif args.kernel is not None:
        test_sys.workload.object_file = binary(args.kernel)

    if args.script is not None:
        test_sys.readfile = args.script

    test_sys.init_param = args.init_param

    # For now, assign all the CPUs to the same clock domain
    test_sys.cpu = [
        TestCPUClass(
            clk_domain=test_sys.cpu_clk_domain, cpu_id=cpu_id * np + i
        )
        for i in range(np)
    ]

    print("esj cpu clock =", args.cpu_clock)
    print("esj sys clock =", args.sys_clock)
    print("esj CXL_clock =", args.CXL_clock)

    # esj 2024-11-27 add cxl ctrl clock
    # esj 2025-02-06
    # if args.cxl_mode:
    #     test_sys.CXL_ctrl.clk_domain = SrcClockDomain(
    #         clock=args.CXL_clock, voltage_domain=test_sys.voltage_domain
    #     )

    if args.ruby:
        bootmem = getattr(test_sys, "_bootmem", None)
        Ruby.create_system(
            args,
            True,
            test_sys,
            test_sys.iobus,
            test_sys._dma_ports,
            bootmem,
            is_second=True,
        )

        # Create a seperate clock domain for Ruby
        test_sys.ruby.clk_domain = SrcClockDomain(
            clock=args.ruby_clock, voltage_domain=test_sys.voltage_domain
        )

        print("esj ruby clock =", args.ruby_clock)

        # Connect the ruby io port to the PIO bus,
        # assuming that there is just one such port.
        test_sys.iobus.mem_side_ports = test_sys.ruby._io_port.in_ports

        for (i, cpu) in enumerate(test_sys.cpu):
            #
            # Tie the cpu ports to the correct ruby system ports
            #
            cpu.clk_domain = test_sys.cpu_clk_domain
            cpu.createThreads()
            cpu.createInterruptController()

            test_sys.ruby._cpu_ports[i].connectCpuPorts(cpu)

    else:
        if args.caches or args.l2cache:
            # By default the IOCache runs at the system clock
            test_sys.iocache = IOCache(addr_ranges=test_sys.mem_ranges)
            test_sys.iocache.cpu_side = test_sys.iobus.mem_side_ports
            test_sys.iocache.mem_side = test_sys.membus.cpu_side_ports
        elif not args.external_memory_system:
            test_sys.iobridge = Bridge(
                delay="50ns", ranges=test_sys.mem_ranges
            )
            test_sys.iobridge.cpu_side_port = test_sys.iobus.mem_side_ports
            test_sys.iobridge.mem_side_port = test_sys.membus.cpu_side_ports

        # Sanity check
        if args.simpoint_profile:
            if not ObjectList.is_noncaching_cpu(TestCPUClass):
                fatal("SimPoint generation should be done with atomic cpu")
            if np > 1:
                fatal(
                    "SimPoint generation not supported with more than one CPUs"
                )

        for i in range(np):
            if args.simpoint_profile:
                test_sys.cpu[i].addSimPointProbe(args.simpoint_interval)
            if args.checker:
                test_sys.cpu[i].addCheckerCpu()
            if not ObjectList.is_kvm_cpu(TestCPUClass):
                if args.bp_type:
                    bpClass = ObjectList.bp_list.get(args.bp_type)
                    test_sys.cpu[i].branchPred = bpClass()
                if args.indirect_bp_type:
                    IndirectBPClass = ObjectList.indirect_bp_list.get(
                        args.indirect_bp_type
                    )
                    test_sys.cpu[
                        i
                    ].branchPred.indirectBranchPred = IndirectBPClass()
            test_sys.cpu[i].createThreads()

        # If elastic tracing is enabled when not restoring from checkpoint and
        # when not fast forwarding using the atomic cpu, then check that the
        # TestCPUClass is DerivO3CPU or inherits from DerivO3CPU. If the check
        # passes then attach the elastic trace probe.
        # If restoring from checkpoint or fast forwarding, the code that does this for
        # FutureCPUClass is in the Simulation module. If the check passes then the
        # elastic trace probe is attached to the switch CPUs.
        if (
            args.elastic_trace_en
            and args.checkpoint_restore == None
            and not args.fast_forward
        ):
            CpuConfig.config_etrace(TestCPUClass, test_sys.cpu, args)

        CacheConfig.config_cache(args, test_sys)

        MemConfig.config_mem(args, test_sys)

    return test_sys


def build_drive_system(np):
    # driver system CPU is always simple, so is the memory
    # Note this is an assignment of a class, not an instance.
    DriveCPUClass = AtomicSimpleCPU
    drive_mem_mode = "atomic"
    DriveMemClass = SimpleMemory

    cmdline = cmd_line_template()
    if buildEnv["USE_MIPS_ISA"]:
        drive_sys = makeLinuxMipsSystem(drive_mem_mode, bm[1], cmdline=cmdline)
    elif buildEnv["USE_SPARC_ISA"]:
        drive_sys = makeSparcSystem(drive_mem_mode, bm[1], cmdline=cmdline)
    elif buildEnv["USE_X86_ISA"]:
        drive_sys = makeLinuxX86System(
            drive_mem_mode, np, bm[1], cmdline=cmdline
        )
    elif buildEnv["USE_ARM_ISA"]:
        drive_sys = makeArmSystem(
            drive_mem_mode,
            args.machine_type,
            np,
            bm[1],
            args.dtb_filename,
            cmdline=cmdline,
        )

    # Create a top-level voltage domain
    drive_sys.voltage_domain = VoltageDomain(voltage=args.sys_voltage)

    # Create a source clock for the system and set the clock period
    drive_sys.clk_domain = SrcClockDomain(
        clock=args.sys_clock, voltage_domain=drive_sys.voltage_domain
    )

    # Create a CPU voltage domain
    drive_sys.cpu_voltage_domain = VoltageDomain()

    # Create a source clock for the CPUs and set the clock period
    drive_sys.cpu_clk_domain = SrcClockDomain(
        clock=args.cpu_clock, voltage_domain=drive_sys.cpu_voltage_domain
    )

    drive_sys.cpu = DriveCPUClass(
        clk_domain=drive_sys.cpu_clk_domain, cpu_id=0
    )
    drive_sys.cpu.createThreads()
    drive_sys.cpu.createInterruptController()
    drive_sys.cpu.connectBus(drive_sys.membus)
    if args.kernel is not None:
        drive_sys.workload.object_file = binary(args.kernel)

    if ObjectList.is_kvm_cpu(DriveCPUClass):
        drive_sys.kvm_vm = KvmVM()

    drive_sys.iobridge = Bridge(delay="50ns", ranges=drive_sys.mem_ranges)
    drive_sys.iobridge.cpu_side_port = drive_sys.iobus.mem_side_ports
    drive_sys.iobridge.mem_side_port = drive_sys.membus.cpu_side_ports

    # Create the appropriate memory controllers and connect them to the
    # memory bus
    drive_sys.mem_ctrls = [
        DriveMemClass(range=r) for r in drive_sys.mem_ranges
    ]
    for i in range(len(drive_sys.mem_ctrls)):
        drive_sys.mem_ctrls[i].port = drive_sys.membus.mem_side_ports

    drive_sys.init_param = args.init_param

    return drive_sys


warn(
    "The fs.py script is deprecated. It will be removed in future releases of "
    " gem5."
)

# Add args
parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addFSOptions(parser)

# Add the ruby specific and protocol specific args
if "--ruby" in sys.argv:
    Ruby.define_options(parser)

args = parser.parse_args()

if args.cxl_mem_as_system_ram and not args.cxl_mode:
    fatal("--cxl-mem-as-system-ram requires --cxl-mode")
ObjectList.cxl_mem_as_system_ram = args.cxl_mem_as_system_ram
if args.cxl_error_rate is not None and not 0.0 <= args.cxl_error_rate <= 1.0:
    fatal("--cxl-error-rate must be in the range [0, 1]")
ObjectList.cxl_error_rate = args.cxl_error_rate

num_hosts = args.num_hosts
if args.dual and num_hosts == 1:
    warn("--dual is set; treating this run as --num-hosts=2")
    num_hosts = 2
elif args.dual and num_hosts != 2:
    fatal("--dual is only compatible with --num-hosts=2")

if num_hosts < 1:
    fatal("--num-hosts must be >= 1")
if num_hosts > 4:
    fatal(
        "--num-hosts=%d is not supported in this tree yet (currently supported: 1..4 in control path)",
        num_hosts,
    )

dual_mode = args.dual or (num_hosts == 2)
# Keep existing downstream logic which checks args.dual.
args.dual = dual_mode

# system under test can be any CPU
(TestCPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(args)

# Match the memories with the CPUs, based on the options for the test system
TestMemClass = Simulation.setMemClass(args)

if args.benchmark:
    try:
        bm = Benchmarks[args.benchmark]
    except KeyError:
        print(f"Error benchmark {args.benchmark} has not been defined.")
        print(f"Valid benchmarks are: {DefinedBenchmarks}")
        sys.exit(1)
else:
    if args.host_cxl_ratios is not None:
        raw_weights = parse_ratio_weights(args.host_cxl_ratios)
        if len(raw_weights) != num_hosts:
            fatal(
                "--host-cxl-ratios expects %d values for --num-hosts=%d, got %d",
                num_hosts,
                num_hosts,
                len(raw_weights),
            )
        host_cxl_weights = raw_weights
        host_ratios = normalize_weights_to_sum(raw_weights, target_sum=10)
        host_ratios = ensure_nonzero_ratios(host_ratios)
        warn(
            "Resolved host CXL ratio weights: %s (normalized view %s)",
            args.host_cxl_ratios,
            ",".join(str(x) for x in host_ratios),
        )
    elif num_hosts == 1:
        # Keep single-host behavior equivalent to SysConfig default (10/10).
        host_cxl_weights = [1]
        host_ratios = [10]
    else:
        if args.host1_cxl_ratio < 0 or args.host1_cxl_ratio > 10:
            fatal("--host1-cxl-ratio must be in range [0, 10]")

        # Existing ratio model uses a base of 10. For N hosts we keep host1
        # fixed and spread the remainder across the other hosts.
        host_ratios = [args.host1_cxl_ratio]
        remaining = 10 - args.host1_cxl_ratio
        base = remaining // (num_hosts - 1)
        rem = remaining % (num_hosts - 1)
        for i in range(num_hosts - 1):
            host_ratios.append(base + (1 if i < rem else 0))

        adjusted = ensure_nonzero_ratios(host_ratios)
        if adjusted != host_ratios:
            warn(
                "Adjusted host CXL ratios to avoid zero-sized host memory: %s -> %s",
                ",".join(str(x) for x in host_ratios),
                ",".join(str(x) for x in adjusted),
            )
            host_ratios = adjusted
        host_cxl_weights = list(host_ratios)

    total_cxl_size = convert.toMemorySize(args.cxl_mem_size)
    try:
        host_cxl_parts = partition_cxl_memory(
            total_cxl_size, host_cxl_weights, CXL_FW_ALIGN
        )
    except ValueError as err:
        fatal("%s", err)

    cxl_base_addr = resolve_cxl_base_addr(args.mem_size)
    host_cxl_sizes = [part.size for part in host_cxl_parts]
    host_cxl_device_offsets = [part.offset for part in host_cxl_parts]
    host_cxl_start_addrs = [cxl_base_addr for _ in host_cxl_parts]
    ObjectList.cxl_logical_device_count = num_hosts
    ObjectList.cxl_host_visible_sizes = host_cxl_sizes
    ObjectList.cxl_host_device_offsets = host_cxl_device_offsets
    warn(
        "Resolved host CXL windows (hpa_start,size,device_offset): %s",
        ", ".join(
            "h%d=(0x%x,0x%x,0x%x)"
            % (
                idx + 1,
                host_cxl_start_addrs[idx],
                host_cxl_sizes[idx],
                host_cxl_device_offsets[idx],
            )
            for idx in range(num_hosts)
        ),
    )

    if args.priority_host_ratios is not None:
        raw_priority_weights = parse_priority_ratio_weights(
            args.priority_host_ratios
        )
        if len(raw_priority_weights) not in (num_hosts, 4):
            fatal(
                "--priority-host-ratios expects %d values (or 4 values), got %d",
                num_hosts,
                len(raw_priority_weights),
            )

        if len(raw_priority_weights) == 4 and num_hosts < 4:
            warn(
                "--priority-host-ratios has 4 values with --num-hosts=%d; using first %d values",
                num_hosts,
                num_hosts,
            )
            active_priority_weights = raw_priority_weights[:num_hosts]
        else:
            active_priority_weights = raw_priority_weights
    else:
        # Backward-compatible default: host1 biased by --priority-limit.
        host0_weight = max(0, int(args.priority_limit))
        if num_hosts == 1:
            active_priority_weights = [max(1, host0_weight)]
        else:
            active_priority_weights = [host0_weight] + [1] * (num_hosts - 1)

    if sum(active_priority_weights) <= 0:
        fatal("--priority-host-ratios sum must be > 0")

    padded_priority_weights = list(active_priority_weights) + [0] * (
        4 - len(active_priority_weights)
    )
    priority_host_ratios_csv = ",".join(
        str(x) for x in padded_priority_weights[:4]
    )
    warn("Resolved priority host ratios: %s", priority_host_ratios_csv)

    bm = []
    for host_idx in range(num_hosts):
        bm.append(
            SysConfig(
                disks=args.disk_image,
                rootdev=args.root_device,
                mem=args.mem_size,
                os_type=args.os_type,
                cxlmem=args.cxl_mem_size,  # esj cxl option 2024-06-28
                numa_mode=args.numa_mode,  # esj numa_mode option 2024-07-12
                cxl_mode=args.cxl_mode,
                cxl_ratio=host_ratios[host_idx],
                cxl_start_addr=host_cxl_start_addrs[host_idx],
                cxl_slice_size=host_cxl_sizes[host_idx],
                cxl_total_size=total_cxl_size,
                cxl_device_offset=host_cxl_device_offsets[host_idx],
                # esj 2026-02-11
                routing_mode_type=args.routing_mode_type,
                priority_limit=args.priority_limit,
                priority_host_ratios=priority_host_ratios_csv,
            )
        )

if args.benchmark:
    if num_hosts == 1:
        num_hosts = len(bm)
    elif len(bm) != num_hosts:
        warn(
            "Benchmark '%s' defines %d hosts, but --num-hosts=%d. "
            "Using benchmark-defined host count.",
            args.benchmark,
            len(bm),
            num_hosts,
        )
        num_hosts = len(bm)

if num_hosts > 4:
    fatal(
        "--num-hosts=%d is not supported in this tree yet (currently supported: 1..4 in control path)",
        num_hosts,
    )

args.deferred_host_script_files = []
args.deferred_host_script_release_files = []
args.deferred_host_ready_files = []
args.deferred_host_complete_files = []

if args.defer_host_scripts_without_cpu_switch and args.switch_on_m5_exit:
    fatal(
        "--defer-host-scripts-without-cpu-switch cannot be used with "
        "--switch-on-m5-exit"
    )

if (
    args.defer_host_scripts_without_cpu_switch
    and not args.defer_non_trigger_host_scripts
):
    fatal(
        "--defer-host-scripts-without-cpu-switch requires "
        "--wait-all-hosts/--defer-non-trigger-host-scripts"
    )

if (
    args.defer_non_trigger_host_scripts
    and not args.switch_on_m5_exit
    and not args.defer_host_scripts_without_cpu_switch
):
    fatal(
        "--wait-all-hosts/--defer-non-trigger-host-scripts requires "
        "--switch-on-m5-exit "
        "or --defer-host-scripts-without-cpu-switch"
    )

if args.defer_non_trigger_host_scripts and args.host_scripts is None:
    fatal(
        "--wait-all-hosts/--defer-non-trigger-host-scripts requires "
        "--host-scripts"
    )

if args.host_scripts is not None:
    if args.script is not None:
        fatal("Use either --script or --host-scripts, not both")

    host_scripts = parse_host_scripts(args.host_scripts)
    if len(host_scripts) != num_hosts:
        fatal(
            "--host-scripts expects %d comma-separated scripts, got %d",
            num_hosts,
            len(host_scripts),
        )

    for idx, host_script in enumerate(host_scripts):
        if args.defer_non_trigger_host_scripts:
            (
                wrapper_path,
                release_path,
                ready_path,
                completion_path,
            ) = create_deferred_host_script(
                idx,
                host_script,
                m5.options.outdir,
            )
            bm[idx].scriptname = wrapper_path
            args.deferred_host_ready_files.append(ready_path)
            args.deferred_host_complete_files.append(completion_path)
            args.deferred_host_script_files.append(wrapper_path)
            args.deferred_host_script_release_files.append(release_path)
        else:
            bm[idx].scriptname = host_script

if num_hosts > 1:
    dual_mode = True
    args.dual = True

np = args.num_cpus

# esj 2026-04-05
extra_systems = []
extra_system_names = []
if len(bm) >= 2:
    # Preserve existing ordering/behavior:
    # - drivesys uses bm[0]
    # - testsys uses bm[1]
    test_sys = build_second_system(np, bm_index=1, cpu_id=1)
else:
    test_sys = build_test_system(np)

drive_sys = None
if len(bm) >= 2:
    # esj 2025-04-14
    # drive_sys = build_drive_system(np)
    # drive_sys = build_second_system(np)
    # esj 2025-04-19
    drive_sys = build_test_system(np)

    # drive_sys.switch.response2 = test_sys.iobus.mem_side_ports
    # drive_sys.switch.request_dma2 = test_sys.iobus.cpu_side_ports
    if hasattr(drive_sys, "switch") and hasattr(test_sys, "pc"):
        drive_sys.switch.host_second = test_sys.pc.pci_host
    if hasattr(drive_sys, "pc") and hasattr(drive_sys.pc, "cxlmemdevice1"):
        drive_sys.pc.cxlmemdevice1.host_second = test_sys.pc.pci_host

    if hasattr(drive_sys, "CXL_ctrl3"):
        drive_sys.CXL_ctrl3.upstreamRequest = test_sys.iobus.cpu_side_ports
        drive_sys.CXL_ctrl3.upstreamResponse = test_sys.iobus.mem_side_ports
        # esj 2025-07-09
        drive_sys.CXL_ctrl3.upstreamResponse2 = test_sys.iobus.mem_side_ports

    root = makeDualRoot(True, test_sys, drive_sys, args.etherdump)
    # Additional host systems are instantiated and attached to root so they
    # are visible to the control/simulation path. CXL/PCI wiring for these
    # extra hosts is left to follow-up changes.
    for bm_index in range(2, len(bm)):
        extra_sys = build_second_system(np, bm_index=bm_index, cpu_id=bm_index)
        extra_name = f"extrasys{bm_index}"
        setattr(root, extra_name, extra_sys)
        extra_systems.append(extra_sys)
        extra_system_names.append(extra_name)

    if hasattr(drive_sys, "switch"):
        if len(extra_systems) >= 1 and hasattr(extra_systems[0], "pc"):
            drive_sys.switch.host_third = extra_systems[0].pc.pci_host
        if len(extra_systems) >= 2 and hasattr(extra_systems[1], "pc"):
            drive_sys.switch.host_fourth = extra_systems[1].pc.pci_host

    if hasattr(drive_sys, "pc") and hasattr(drive_sys.pc, "cxlmemdevice1"):
        if len(extra_systems) >= 1 and hasattr(extra_systems[0], "pc"):
            drive_sys.pc.cxlmemdevice1.host_third = extra_systems[
                0
            ].pc.pci_host
        if len(extra_systems) >= 2 and hasattr(extra_systems[1], "pc"):
            drive_sys.pc.cxlmemdevice1.host_fourth = extra_systems[
                1
            ].pc.pci_host

    if hasattr(drive_sys, "CXL_ctrl4"):
        cxl4_src_sys = (
            extra_systems[0] if len(extra_systems) >= 1 else drive_sys
        )
        drive_sys.CXL_ctrl4.upstreamRequest = cxl4_src_sys.iobus.cpu_side_ports
        drive_sys.CXL_ctrl4.upstreamResponse = (
            cxl4_src_sys.iobus.mem_side_ports
        )
        drive_sys.CXL_ctrl4.upstreamResponse2 = (
            cxl4_src_sys.iobus.mem_side_ports
        )

    if hasattr(drive_sys, "CXL_ctrl5"):
        cxl5_src_sys = (
            extra_systems[1] if len(extra_systems) >= 2 else drive_sys
        )
        drive_sys.CXL_ctrl5.upstreamRequest = cxl5_src_sys.iobus.cpu_side_ports
        drive_sys.CXL_ctrl5.upstreamResponse = (
            cxl5_src_sys.iobus.mem_side_ports
        )
        drive_sys.CXL_ctrl5.upstreamResponse2 = (
            cxl5_src_sys.iobus.mem_side_ports
        )

    if extra_systems:
        warn(
            "Built %d additional host system(s) (%s). "
            "Configured switch host_third/host_fourth and CXL_ctrl4/5 upstream wiring.",
            len(extra_systems),
            ", ".join(extra_system_names),
        )
elif len(bm) == 1 and args.dist:
    # This system is part of a dist-gem5 simulation
    root = makeDistRoot(
        test_sys,
        args.dist_rank,
        args.dist_size,
        args.dist_server_name,
        args.dist_server_port,
        args.dist_sync_repeat,
        args.dist_sync_start,
        args.ethernet_linkspeed,
        args.ethernet_linkdelay,
        args.etherdump,
    )
elif len(bm) == 1:
    root = Root(full_system=True, system=test_sys)
else:
    print("Error I don't know how to create this host-system configuration.")
    sys.exit(1)

active_systems = [test_sys]
if drive_sys is not None:
    active_systems.append(drive_sys)
active_systems.extend(extra_systems)

if ObjectList.is_kvm_cpu(TestCPUClass) or ObjectList.is_kvm_cpu(FutureClass):
    assign_kvm_event_queues(active_systems)
    # Required for running kvm on multiple host cores.
    # Uses gem5's parallel event queue feature
    # Note: The simulator is quite picky about this number!
    root.sim_quantum = int(1e9)  # 1 ms

if args.timesync:
    root.time_sync_enable = True

if args.frame_capture:
    VncServer.frame_capture = True

if buildEnv["USE_ARM_ISA"] and not args.bare_metal and not args.dtb_filename:
    if args.machine_type not in [
        "VExpress_GEM5",
        "VExpress_GEM5_V1",
        "VExpress_GEM5_V2",
        "VExpress_GEM5_Foundation",
    ]:
        warn(
            "Can only correctly generate a dtb for VExpress_GEM5_* "
            "platforms, unless custom hardware models have been equipped "
            "with generation functionality."
        )

    # Generate a Device Tree
    # for sysname in ("system", "drivesys", "testsys", ...)
    dtb_sysnames = ["system", "drivesys", "testsys"] + extra_system_names
    for sysname in dtb_sysnames:
        if hasattr(root, sysname):
            sys = getattr(root, sysname)
            sys.workload.dtb_filename = os.path.join(
                m5.options.outdir, f"{sysname}.dtb"
            )
            sys.generateDtb(sys.workload.dtb_filename)

if args.wait_gdb:
    test_sys.workload.wait_for_remote_gdb = True

Simulation.setWorkCountOptions(test_sys, args)
# esj 2025-04-19
if drive_sys is not None:
    Simulation.setWorkCountOptions(drive_sys, args)
for extra_sys in extra_systems:
    Simulation.setWorkCountOptions(extra_sys, args)

# Dual mode: dump stats separately for testsys and drivesys (m5 dumpstats/dumpresetstats)
if dual_mode and drive_sys is not None:
    import os
    import m5.stats as _stats_mod

    _orig_dump = _stats_mod.dump

    def _dual_stats_dump(roots=None):
        if roots is not None:
            _orig_dump(roots=roots)
            return
        outdir = getattr(m5.options, "outdir", None) or os.getcwd()
        outdir = os.path.abspath(outdir)
        backup = list(_stats_mod.outputList)
        dump_targets = [("testsys", test_sys), ("drivesys", drive_sys)]
        for idx, extra_sys in enumerate(extra_systems, start=2):
            dump_targets.append((f"extrasys{idx}", extra_sys))
        for name, sys_obj in dump_targets:
            _stats_mod.outputList.clear()
            _stats_mod.addStatVisitor(
                "text://" + os.path.join(outdir, f"stats_{name}.txt")
            )
            _orig_dump(roots=[sys_obj])
        _stats_mod.outputList = backup

    _stats_mod.dump = _dual_stats_dump

# Simulation.run(args, root, test_sys, FutureClass)
Simulation.run(
    args,
    root,
    test_sys,
    FutureClass,
    drive_sys,
    extra_systems=extra_systems,
)

do_dot(root, m5.options.outdir, "config.dot")
