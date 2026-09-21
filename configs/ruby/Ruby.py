# Copyright (c) 2012, 2017-2018, 2021 ARM Limited
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
# Copyright (c) 2006-2007 The Regents of The University of Michigan
# Copyright (c) 2009 Advanced Micro Devices, Inc.
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

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath, fatal, warn
from gem5.isas import ISA
from gem5.runtime import get_runtime_isa
from m5.objects.MemTraceProbe import *

addToPath("../")

from common import ObjectList
from common import MemConfig
from common import FileSystemConfig

from topologies import *
from network import Network


def define_options(parser):
    # By default, ruby uses the simple timing cpu
    parser.set_defaults(cpu_type="TimingSimpleCPU")

    parser.add_argument(
        "--ruby-clock",
        action="store",
        type=str,
        default="2GHz",
        help="Clock for blocks running at Ruby system's speed",
    )

    parser.add_argument(
        "--access-backing-store",
        action="store_true",
        default=False,
        help="Should ruby maintain a second copy of memory",
    )

    # Options related to cache structure
    parser.add_argument(
        "--ports",
        action="store",
        type=int,
        default=4,
        help="used of transitions per cycle which is a proxy \
            for the number of ports.",
    )

    # network options are in network/Network.py

    # ruby mapping options
    parser.add_argument(
        "--numa-high-bit",
        type=int,
        default=0,
        help="high order address bit to use for numa mapping. "
        "0 = highest bit, not specified = lowest bit",
    )
    parser.add_argument(
        "--interleaving-bits",
        type=int,
        default=0,
        help="number of bits to specify interleaving "
        "in directory, memory controllers and caches. "
        "0 = not specified",
    )
    parser.add_argument(
        "--xor-low-bit",
        type=int,
        default=20,
        help="hashing bit for channel selection"
        "see MemConfig for explanation of the default"
        "parameter. If set to 0, xor_high_bit is also"
        "set to 0.",
    )

    parser.add_argument(
        "--recycle-latency",
        type=int,
        default=10,
        help="Recycle latency for ruby controller input buffers",
    )

    protocol = buildEnv["PROTOCOL"]
    exec(f"from . import {protocol}")
    eval(f"{protocol}.define_options(parser)")
    Network.define_options(parser)


def constrain_num_dirs_to_mem_ranges(options, system):
    mem_range_count = len(getattr(system, "mem_ranges", []))
    if mem_range_count == 0:
        return

    if getattr(ObjectList, "cxl_mode", False) and not getattr(
        ObjectList, "cxl_mem_as_system_ram", False
    ):
        required_dirs = mem_range_count + 1
        if options.num_dirs < required_dirs:
            fatal(
                "--num-dirs=%d is smaller than the %d Ruby director%s "
                "needed for DRAM plus the CXL HomeAgent director",
                options.num_dirs,
                required_dirs,
                "y" if required_dirs == 1 else "ies",
            )
        if options.num_dirs > required_dirs:
            warn(
                "--num-dirs=%d exceeds the %d Ruby director%s needed for "
                "DRAM plus the CXL HomeAgent director; using %d",
                options.num_dirs,
                required_dirs,
                "y" if required_dirs == 1 else "ies",
                required_dirs,
            )
            options.num_dirs = required_dirs
        return

    if options.num_dirs > mem_range_count:
        warn(
            "--num-dirs=%d exceeds the %d system memory range(s); using %d "
            "Ruby director%s so unused directories do not inherit AllMemory",
            options.num_dirs,
            mem_range_count,
            mem_range_count,
            "y" if mem_range_count == 1 else "ies",
        )
        options.num_dirs = mem_range_count
    elif options.num_dirs < mem_range_count:
        fatal(
            "--num-dirs=%d is smaller than the %d system memory range(s)",
            options.num_dirs,
            mem_range_count,
        )


# wsj 2025-04-14
# def setup_memory_controllers(system, ruby, dir_cntrls, options):
def setup_memory_controllers(
    system,
    ruby,
    dir_cntrls,
    options,
    is_second,
):
    print("esj setup_memory_controllers")
    if options.numa_high_bit:
        block_size_bits = (
            options.numa_high_bit + 1 - int(math.log(options.num_dirs, 2))
        )
        ruby.block_size_bytes = 2 ** (block_size_bits)
    else:
        ruby.block_size_bytes = options.cacheline_size

    ruby.memory_size_bits = 48

    index = 0
    mem_ctrls = []
    crossbars = []

    # esj 2024-08-23
    mem_monitors = []

    if options.numa_high_bit:
        dir_bits = int(math.log(options.num_dirs, 2))
        intlv_size = 2 ** (options.numa_high_bit - dir_bits + 1)
    else:
        # if the numa_bit is not specified, set the directory bits as the
        # lowest bits above the block offset bits
        intlv_size = options.cacheline_size

    # Sets bits to be used for interleaving.  Creates memory controllers
    # attached to a directory controller.  A separate controller is created
    # for each address range as the abstract memory can handle only one
    # contiguous address range as of now.
    # for dir_cntrl in dir_cntrls:
    #     crossbar = None
    #     if len(system.mem_ranges) > 1:
    #         crossbar = IOXBar()
    #         crossbars.append(crossbar)
    #         dir_cntrl.memory_out_port = crossbar.cpu_side_ports

    #     mem_cnt = 0 #esj 2024-08-01

    #     dir_ranges = []
    #     for r in system.mem_ranges:
    #         print("esj mem_ranges",r)
    #         #esj 2025-04-09
    #         # if ObjectList.cxl_mode:
    #         #     mem_monitor = m5.objects.CommMonitor()

    #         mem_type = ObjectList.mem_list.get(options.mem_type)
    #         dram_intf = MemConfig.create_mem_intf(
    #             mem_type,
    #             r,
    #             index,
    #             int(math.log(options.num_dirs, 2)),
    #             intlv_size,
    #             options.xor_low_bit,
    #         )

    #         # if ObjectList.cxl_mode and mem_cnt == 1 and not is_second: #esj 2024-08-01
    #         if ObjectList.cxl_mode and mem_cnt >= 1: #esj 2024-08-01
    #             dram_intf.is_cxl = True
    #             if is_second:
    #                 dram_intf.is_second = True

    #         if issubclass(mem_type, DRAMInterface):
    #             mem_ctrl = m5.objects.MemCtrl(dram=dram_intf)

    #             #esj 2025-04-14
    #             # if dram_intf.is_cxl:
    #             # if dram_intf.is_cxl and not is_second:
    #             if dram_intf.is_cxl:
    #                 system.iobus.cpu_side_ports = mem_ctrl.Request_port
    #                 #esj 2025-05-05
    #                 if mem_cnt == 1:
    #                     mem_ctrl.cxl_mem_start = ObjectList.cxl_mem_start
    #                     mem_ctrl.cxl_mem_size = ObjectList.cxl_mem_size
    #                     mem_ctrl.cxl_bar_size = ObjectList.cxl_bar_size

    #                     mem_ctrl.cxl_bar_start = 0xc000_0000
    #                 elif mem_cnt == 2:
    #                     mem_ctrl.cxl_mem_start = ObjectList.cxl_mem_start + ObjectList.cxl_mem_size
    #                     mem_ctrl.cxl_mem_size = ObjectList.cxl_mem_size
    #                     mem_ctrl.cxl_bar_size = ObjectList.cxl_bar_size
    #                     mem_ctrl.cxl_bar_start = 0xe000_0000

    #         else:
    #             mem_ctrl = dram_intf

    #         if options.access_backing_store:
    #             dram_intf.kvm_map = False

    #         mem_ctrls.append(mem_ctrl)
    #         dir_ranges.append(dram_intf.range)

    #         if crossbar != None:
    #             mem_ctrl.port = crossbar.mem_side_ports
    #             # #esj 2024-08-23
    #             # mem_monitor.cpu_side_port = crossbar.mem_side_ports
    #             # mem_monitor.mem_side_port = mem_ctrl.port
    #             # # mem_monitor.trace = m5.objects.MemTraceProbe(trace_file = "mem_ctrl_"+str(mem_cnt)+"_trace.trc.gz")
    #             # mem_monitors.append(mem_monitor)
    #             #esj 2025-04-09
    #             # if ObjectList.cxl_mode:
    #                 # mem_monitor.cpu_side_port = crossbar.mem_side_ports
    #                 # mem_monitor.mem_side_port = mem_ctrl.port
    #                 # mem_monitor.trace = m5.objects.MemTraceProbe(trace_file = "mem_ctrl_"+str(mem_cnt)+"_trace.trc.gz")
    #                 # mem_monitors.append(mem_monitor)
    #             # elif ObjectList.numa_mode and mem_cnt == 1:
    #             if ObjectList.numa_mode and mem_cnt == 1:
    #                 system.numa_bridge = m5.objects.Bridge(delay="60ns")
    #                 system.numa_bridge.cpu_side_port = crossbar.mem_side_ports
    #                 system.numa_bridge.mem_side_port = mem_ctrl.port
    #                 system.numa_bridge.ranges = [AddrRange(ObjectList.cxl_mem_start, ObjectList.cxl_mem_start+ObjectList.cxl_mem_size -1)]
    #             # else:
    #             #     mem_ctrl.port = crossbar.mem_side_ports

    #         else:
    #             mem_ctrl.port = dir_cntrl.memory_out_port

    #         # Enable low-power DRAM states if option is set
    #         if issubclass(mem_type, DRAMInterface):
    #             mem_ctrl.dram.enable_dram_powerdown = (
    #                 options.enable_dram_powerdown
    #             )
    #         mem_cnt += 1 #esj 2024-08-01
    #     index += 1
    #     dir_cntrl.addr_ranges = dir_ranges

    # system.mem_ctrls = mem_ctrls

    # #esj 2025-04-09
    # # if ObjectList.cxl_mode:
    # #     system.mem_monitors = mem_monitors

    # if len(crossbars) > 0:
    #     ruby.crossbars = crossbars

    # esj 2025-06-14
    if ObjectList.cxl_mode:
        mem_type = ObjectList.mem_list.get(options.mem_type)
        for j in range(len(dir_cntrls)):
            print("esj j = ", j)
            # for i in range(int(options.num_dirs / 2)):
            mem_ctrl = m5.objects.MemCtrl()
            if j == 0:
                print(
                    "esj system.mem_ranges[j].start = %08x, system.mem_ranges[j].size() = %08x"
                    % (system.mem_ranges[j].start, system.mem_ranges[j].size())
                )
                mem_ctrl.dram = mem_type(
                    range=AddrRange(
                        start=system.mem_ranges[j].start,
                        size=system.mem_ranges[j].size(),
                        # masks = [1<<6],
                        # intlvMatch = i
                    )
                )
            else:
                cxl_window_start = (
                    ObjectList.cxl_mem_start
                    + ObjectList.cxl_mem_size * (j - 1)
                )
                cxl_device_offset = (
                    ObjectList.cxl_device_offset
                    + ObjectList.cxl_mem_size * (j - 1)
                )
                cxl_bar_size = getattr(ObjectList, "cxl_bar_size", 0x2_0000)
                if hasattr(system, "pc") and hasattr(
                    system.pc, "cxlmemdevice1"
                ):
                    cxl_bar_size = system.pc.cxlmemdevice1.BAR0.size

                print(
                    "esj ObjectList.cxl_mem_start = ",
                    cxl_window_start,
                    "ObjectList.cxl_mem_size = ",
                    ObjectList.cxl_mem_size,
                )
                direct_cxl_validation = getattr(
                    ObjectList, "cxl_home_agent_direct_validation", False
                )
                cxl_home_agent = m5.objects.CXLHomeAgent(
                    cxl_mem_start=cxl_window_start,
                    cxl_mem_size=ObjectList.cxl_mem_size,
                    cxl_device_offset=cxl_device_offset,
                    cxl_bar_size=cxl_bar_size,
                    cxl_bar_start=0xC000_0000,
                    validation_force_cxl_ready=direct_cxl_validation,
                    validation_host_id=0,
                )
                cxl_home_agent.ruby_side = dir_cntrls[j].memory_out_port
                cxl_home_agent.ruby_side_1 = dir_cntrls[j].memory_out_port_1
                if direct_cxl_validation:
                    cxl_home_agent.cxl_side = system.CXL_ctrl.upstreamResponse
                    cxl_home_agent.cxl_response_side = (
                        system.CXL_ctrl.upstreamResponse2
                    )
                else:
                    cxl_home_agent.cxl_side = system.iobus.cpu_side_ports
                setattr(system, "cxl_home_agent%d" % j, cxl_home_agent)
                dir_cntrls[j].addr_ranges = AddrRange(
                    start=cxl_window_start,
                    size=ObjectList.cxl_mem_size,
                )
                continue

            if options.access_backing_store:
                mem_ctrl.dram.kvm_map = False

            mem_ctrls.append(mem_ctrl)
            # mem_ctrl.port = dir_cntrls[2*j+i].memory_out_port
            # dir_cntrls[2*j+i].addr_ranges = mem_ctrl.dram.range

            mem_ctrl.port = dir_cntrls[j].memory_out_port
            mem_ctrl.port_1 = dir_cntrls[j].memory_out_port_1  # esj 2025-06-22
            dir_cntrls[j].addr_ranges = mem_ctrl.dram.range

            # Enable low-power DRAM states if option is set
            if issubclass(type(mem_ctrl.dram), DRAMInterface):
                mem_ctrl.dram.enable_dram_powerdown = (
                    options.enable_dram_powerdown
                )
    else:
        mem_type = ObjectList.mem_list.get(options.mem_type)
        for j in range(len(system.mem_ranges)):
            print("esj j = ", j)
            # for i in range(int(options.num_dirs / 2)):
            mem_ctrl = m5.objects.MemCtrl()
            if j == 0:
                mem_ctrl.dram = mem_type(
                    range=AddrRange(
                        start=0,
                        size=0xC0000000,
                        # masks = [1<<6],
                        # intlvMatch = i
                    )
                )
            else:
                mem_ctrl.dram = mem_type(
                    range=AddrRange(
                        start=ObjectList.cxl_mem_start,
                        size=ObjectList.cxl_mem_size,
                        # masks = [1<<6],
                        # intlvMatch = i
                    )
                )

            if options.access_backing_store:
                mem_ctrl.dram.kvm_map = False

            mem_ctrls.append(mem_ctrl)

            if j == 0:
                mem_ctrl.port = dir_cntrls[j].memory_out_port
            else:
                system.numa_bridge = m5.objects.Bridge(delay="60ns")
                system.numa_bridge.cpu_side_port = dir_cntrls[
                    j
                ].memory_out_port
                system.numa_bridge.mem_side_port = mem_ctrl.port
                system.numa_bridge.ranges = [
                    AddrRange(
                        ObjectList.cxl_mem_start,
                        ObjectList.cxl_mem_start + ObjectList.cxl_mem_size - 1,
                    )
                ]

            dir_cntrls[j].addr_ranges = mem_ctrl.dram.range

            # Enable low-power DRAM states if option is set
            if issubclass(type(mem_ctrl.dram), DRAMInterface):
                mem_ctrl.dram.enable_dram_powerdown = (
                    options.enable_dram_powerdown
                )

    system.mem_ctrls = mem_ctrls


def create_topology(controllers, options):
    """Called from create_system in configs/ruby/<protocol>.py
    Must return an object which is a subclass of BaseTopology
    found in configs/topologies/BaseTopology.py
    This is a wrapper for the legacy topologies.
    """
    exec(f"import topologies.{options.topology} as Topo")
    topology = eval(f"Topo.{options.topology}(controllers)")
    return topology


def create_system(
    options,
    full_system,
    system,
    piobus=None,
    dma_ports=[],
    bootmem=None,
    cpus=None,
    # esj 2025-04-14
    is_second=False,
):

    system.ruby = RubySystem()
    ruby = system.ruby

    constrain_num_dirs_to_mem_ranges(options, system)

    # Generate pseudo filesystem
    FileSystemConfig.config_filesystem(system, options)

    # Create the network object
    (
        network,
        IntLinkClass,
        ExtLinkClass,
        RouterClass,
        InterfaceClass,
    ) = Network.create_network(options, ruby)
    ruby.network = network

    if cpus is None:
        cpus = system.cpu

    protocol = buildEnv["PROTOCOL"]
    print("esj ejsej protocol", protocol)
    exec(f"from . import {protocol}")
    try:
        (cpu_sequencers, dir_cntrls, topology) = eval(
            "%s.create_system(options, full_system, system, dma_ports,\
                                    bootmem, ruby, cpus, is_second)"
            # esj 2025-04-14
            # bootmem, ruby, cpus)"
            % protocol
        )
    except:
        print(f"Error: could not create sytem for ruby protocol {protocol}")
        raise

    # Create the network topology
    topology.makeTopology(
        options, network, IntLinkClass, ExtLinkClass, RouterClass
    )

    # Register the topology elements with faux filesystem (SE mode only)
    if not full_system:
        topology.registerTopology(options)

    # Initialize network based on topology
    Network.init_network(options, network, InterfaceClass, is_cxl=False)

    # esj 2025-04-06
    ##########################
    # esj 2025-04-14
    if not is_second:
        system.cxl = RubySystem()
        cxl = system.cxl

        # esj 2025-07-20
        options.network = "garnet"
        options.topology = "Pt2Pt"

        # Create the network object
        (
            cxl_network,
            IntLinkClass,
            ExtLinkClass,
            RouterClass,
            InterfaceClass,
        ) = Network.create_network(options, cxl)
        cxl.network = cxl_network

        # esj 2025-07-28
        cxl.network.control_msg_size = 16
        cxl.network.data_msg_size = 64
        cxl.network.ni_flit_size = 68

        protocol = buildEnv["PROTOCOL"]
        print(protocol)
        exec(f"from . import {protocol}")
        try:
            (topolog_cxl) = eval(
                "%s.create_system_cxl(options, full_system, system, dma_ports,\
                                        bootmem, cxl, cpus)"
                % protocol
            )
        except:
            print(f"Error: could not create sytem for cxl protocol {protocol}")
            raise

        # Create the network topology
        topolog_cxl.makeTopology(
            options, cxl_network, IntLinkClass, ExtLinkClass, RouterClass
        )

        # Register the topology elements with faux filesystem (SE mode only)
        if not full_system:
            topolog_cxl.registerTopology(options)

        # Initialize network based on topology
        Network.init_network(options, cxl_network, InterfaceClass, is_cxl=True)

        cxl.number_of_virtual_networks = cxl.network.number_of_virtual_networks
        cxl.num_of_sequencers = 2

    ###########################

    # Create a port proxy for connecting the system port. This is
    # independent of the protocol and kept in the protocol-agnostic
    # part (i.e. here).
    sys_port_proxy = RubyPortProxy(
        ruby_system=ruby, cxl_mode=ObjectList.cxl_mode
    )
    print("esj proxy = ", sys_port_proxy.cxl_mode)
    if piobus is not None:
        sys_port_proxy.pio_request_port = piobus.cpu_side_ports

    # Give the system port proxy a SimObject parent without creating a
    # full-fledged controller
    system.sys_port_proxy = sys_port_proxy

    # Connect the system port for loading of binaries etc
    system.system_port = system.sys_port_proxy.in_ports

    # esj 2025-04-14
    # setup_memory_controllers(system, ruby, dir_cntrls, options)
    setup_memory_controllers(system, ruby, dir_cntrls, options, is_second)

    # Connect the cpu sequencers and the piobus
    if piobus != None:
        for cpu_seq in cpu_sequencers:
            cpu_seq.connectIOPorts(piobus)

    # ruby.number_of_virtual_networks = ruby.network.number_of_virtual_networks + cxl.network.number_of_virtual_networks
    ruby.number_of_virtual_networks = ruby.network.number_of_virtual_networks
    ruby._cpu_ports = cpu_sequencers
    ruby.num_of_sequencers = len(cpu_sequencers)

    # Create a backing copy of physical memory in case required
    if options.access_backing_store:
        ruby.access_backing_store = True
        ruby.phys_mem = SimpleMemory(
            range=system.mem_ranges[0], in_addr_map=False
        )


def create_directories(options, bootmem, ruby_system, system, is_second, var):
    dir_cntrl_nodes = []

    for i in range(options.num_dirs):
        if is_second:
            dir_cntrl = Directory2_Controller()
        else:
            dir_cntrl = Directory_Controller()
        dir_cntrl.version = i
        dir_cntrl.directory = RubyDirectoryMemory()
        dir_cntrl.ruby_system = ruby_system

        exec("ruby_system.dir_cntrl%d = dir_cntrl" % i)
        dir_cntrl_nodes.append(dir_cntrl)

    if bootmem is not None:
        if is_second:
            rom_dir_cntrl = Directory2_Controller()
        else:
            rom_dir_cntrl = Directory_Controller()
        rom_dir_cntrl.directory = RubyDirectoryMemory()
        rom_dir_cntrl.ruby_system = ruby_system
        rom_dir_cntrl.version = i + 1
        rom_dir_cntrl.memory = bootmem.port
        rom_dir_cntrl.addr_ranges = bootmem.range
        return (dir_cntrl_nodes, rom_dir_cntrl)

    return (dir_cntrl_nodes, None)


def send_evicts(options):
    # currently, 2 scenarios warrant forwarding evictions to the CPU:
    # 1. The O3 model must keep the LSQ coherent with the caches
    # 2. The x86 mwait instruction is built on top of coherence invalidations
    # 3. The local exclusive monitor in ARM systems
    if options.cpu_type == "DerivO3CPU" or get_runtime_isa() in (
        ISA.X86,
        ISA.ARM,
    ):
        return True
    return False
