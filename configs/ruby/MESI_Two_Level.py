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
from .Ruby import create_topology, create_directories
from .Ruby import send_evicts
from common import ObjectList #esj 2024-08-09

#
# Declare caches used by the protocol
#
class L1Cache(RubyCache):
    pass


class L2Cache(RubyCache):
    pass


def define_options(parser):
    return


def configure_cxl_sequencer(seq):
    if not ObjectList.cxl_mode:
        return

    seq.cxl_mode = True
    seq.cxl_mem_start = ObjectList.cxl_mem_start
    seq.cxl_mem_size = ObjectList.cxl_mem_size
    seq.cxl_bar_size = ObjectList.cxl_bar_size
    seq.cxl_bar_start = ObjectList.cxl_bar_start


#esj 2025-04-14
# def create_system(
#     options, full_system, system, dma_ports, bootmem, ruby_system, cpus,s
# ):
def create_system(
    options, full_system, system, dma_ports, bootmem, ruby_system, cpus, is_second,
):

    if buildEnv["PROTOCOL"] != "MESI_Two_Level":
        fatal("This script requires the MESI_Two_Level protocol to be built.")

    cpu_sequencers = []

    #
    # The ruby network creation expects the list of nodes in the system to be
    # consistent with the NetDest list.  Therefore the l1 controller nodes must be
    # listed before the directory nodes and directory nodes before dma nodes, etc.
    #
    l1_cntrl_nodes = []
    l2_cntrl_nodes = []
    dma_cntrl_nodes = []

    #
    # Must create the individual controllers before the network to ensure the
    # controller constructors are called before the network constructor
    #
    l2_bits = int(math.log(options.num_l2caches, 2))
    block_size_bits = int(math.log(options.cacheline_size, 2))

    #esj 2025-04-18
    var = 0
    # if is_second:
    #     var = 5
    #     print("esj var -= 1 = ",var)
    #

    for i in range(options.num_cpus):
        #
        # First create the Ruby objects associated with this cpu
        #
        
        l1i_cache = L1Cache(
            size=options.l1i_size,
            assoc=options.l1i_assoc,
            start_index_bit=block_size_bits,
            is_icache=True,
        )
        l1d_cache = L1Cache(
            size=options.l1d_size,
            assoc=options.l1d_assoc,
            start_index_bit=block_size_bits,
            is_icache=False,
        )

        prefetcher = RubyPrefetcher()

        clk_domain = cpus[i].clk_domain

        if is_second:
            l1_cntrl = L1Cache2_Controller(
                version=i + var, #esj 2025-04-18
                L1Icache2=l1i_cache,
                L1Dcache2=l1d_cache,
                l2_select_num_bits=l2_bits,
                send_evictions=send_evicts(options),
                prefetcher=prefetcher,
                ruby_system=ruby_system,
                clk_domain=clk_domain,
                transitions_per_cycle=options.ports,
                enable_prefetch=False,
            )
        else:
            l1_cntrl = L1Cache_Controller(
                version=i + var, #esj 2025-04-18
                L1Icache=l1i_cache,
                L1Dcache=l1d_cache,
                l2_select_num_bits=l2_bits,
                send_evictions=send_evicts(options),
                prefetcher=prefetcher,
                ruby_system=ruby_system,
                clk_domain=clk_domain,
                transitions_per_cycle=options.ports,
                enable_prefetch=False,
            )

        cpu_seq = RubySequencer(
            version=i + var, #esj 2025-04-18
            dcache=l1d_cache,
            clk_domain=clk_domain,
            ruby_system=ruby_system,
            max_outstanding_requests = 2048, #esj 2024-10-17
            deadlock_threshold = 500000000
        )

        l1_cntrl.sequencer = cpu_seq
        configure_cxl_sequencer(cpu_seq)

        exec("ruby_system.l1_cntrl%d = l1_cntrl" % i)

        # Add controllers and sequencers to the appropriate lists
        cpu_sequencers.append(cpu_seq)
        l1_cntrl_nodes.append(l1_cntrl)

        # Connect the L1 controllers and the network
        l1_cntrl.mandatoryQueue = MessageBuffer()
        l1_cntrl.requestFromL1Cache = MessageBuffer()
        l1_cntrl.requestFromL1Cache.out_port = ruby_system.network.in_port
        l1_cntrl.responseFromL1Cache = MessageBuffer()
        l1_cntrl.responseFromL1Cache.out_port = ruby_system.network.in_port
        l1_cntrl.unblockFromL1Cache = MessageBuffer()
        l1_cntrl.unblockFromL1Cache.out_port = ruby_system.network.in_port

        l1_cntrl.optionalQueue = MessageBuffer()

        l1_cntrl.requestToL1Cache = MessageBuffer()
        l1_cntrl.requestToL1Cache.in_port = ruby_system.network.out_port
        l1_cntrl.responseToL1Cache = MessageBuffer()
        l1_cntrl.responseToL1Cache.in_port = ruby_system.network.out_port


        #esj 2025-04-18
        # if is_second:
        #     var -= 1
        #     print("esj var -= 1 = ",var)

    l2_index_start = block_size_bits + l2_bits

    for i in range(options.num_l2caches):
        #
        # First create the Ruby objects associated with this cpu
        #
        
        l2_cache = L2Cache(
            size=options.l2_size,
            assoc=options.l2_assoc,
            start_index_bit=l2_index_start,
        )

        if is_second:
            l2_cntrl = L2Cache2_Controller(
                version=i + var, #esj 2025-04-18
                L2cache2=l2_cache,
                transitions_per_cycle=options.ports,
                ruby_system=ruby_system,
            )

        else:
            l2_cntrl = L2Cache_Controller(
                version=i + var, #esj 2025-04-18
                L2cache=l2_cache,
                transitions_per_cycle=options.ports,
                ruby_system=ruby_system,
            )

        exec("ruby_system.l2_cntrl%d = l2_cntrl" % i)
        l2_cntrl_nodes.append(l2_cntrl)

        # Connect the L2 controllers and the network
        l2_cntrl.DirRequestFromL2Cache = MessageBuffer()
        l2_cntrl.DirRequestFromL2Cache.out_port = ruby_system.network.in_port
        l2_cntrl.L1RequestFromL2Cache = MessageBuffer()
        l2_cntrl.L1RequestFromL2Cache.out_port = ruby_system.network.in_port
        l2_cntrl.responseFromL2Cache = MessageBuffer()
        l2_cntrl.responseFromL2Cache.out_port = ruby_system.network.in_port

        l2_cntrl.unblockToL2Cache = MessageBuffer()
        l2_cntrl.unblockToL2Cache.in_port = ruby_system.network.out_port
        l2_cntrl.L1RequestToL2Cache = MessageBuffer()
        l2_cntrl.L1RequestToL2Cache.in_port = ruby_system.network.out_port
        l2_cntrl.responseToL2Cache = MessageBuffer()
        l2_cntrl.responseToL2Cache.in_port = ruby_system.network.out_port


        #esj 2025-04-18
        # if is_second:
        #     var -= 1
        #     print("esj var -= 1 = ",var)

    # Run each of the ruby memory controllers at a ratio of the frequency of
    # the ruby system
    # clk_divider value is a fix to pass regression.
    ruby_system.memctrl_clk_domain = DerivedClockDomain(
        clk_domain=ruby_system.clk_domain, clk_divider=3
    )

    mem_dir_cntrl_nodes, rom_dir_cntrl_node = create_directories(
        # options, bootmem, ruby_system, system
        options, bootmem, ruby_system, system, is_second, var
    )
    dir_cntrl_nodes = mem_dir_cntrl_nodes[:]
    if rom_dir_cntrl_node is not None:
        dir_cntrl_nodes.append(rom_dir_cntrl_node)
    for dir_cntrl in dir_cntrl_nodes:
        # Connect the directory controllers and the network
        dir_cntrl.requestToDir = MessageBuffer()
        dir_cntrl.requestToDir.in_port = ruby_system.network.out_port
        dir_cntrl.responseToDir = MessageBuffer()
        dir_cntrl.responseToDir.in_port = ruby_system.network.out_port
        dir_cntrl.responseFromDir = MessageBuffer()
        dir_cntrl.responseFromDir.out_port = ruby_system.network.in_port
        dir_cntrl.requestToMemory = MessageBuffer()
        dir_cntrl.responseFromMemory = MessageBuffer()

        dir_cntrl.version = dir_cntrl.version + var #esj 2025-04-18
        #esj 2025-04-18
        # if is_second:   
        #     var -= 1
        #     print("esj var -= 1 = ",var)

    for i, dma_port in enumerate(dma_ports):
        # Create the Ruby objects associated with the dma controller
        dma_seq = DMASequencer(
            version=i + var, #esj 2025-04-18
            ruby_system=ruby_system, in_ports=dma_port,max_outstanding_requests = 2048 #esj 2024-10-17
        )
        
        #esj 2024-10-01
        configure_cxl_sequencer(dma_seq)

        #esj 2024-10-08
        if ObjectList.numa_mode:
            dma_seq.numa_mode = True

        if is_second:
            dma_cntrl = DMA2_Controller(
                version=i + var, #esj 2025-04-18
                dma_sequencer=dma_seq,
                transitions_per_cycle=options.ports,
                ruby_system=ruby_system,
            )
        else:
            dma_cntrl = DMA_Controller(
                version=i + var, #esj 2025-04-18
                dma_sequencer=dma_seq,
                transitions_per_cycle=options.ports,
                ruby_system=ruby_system,
            )

        exec("ruby_system.dma_cntrl%d = dma_cntrl" % i)
        dma_cntrl_nodes.append(dma_cntrl)

        # Connect the dma controller to the network
        dma_cntrl.mandatoryQueue = MessageBuffer()
        dma_cntrl.responseFromDir = MessageBuffer(ordered=True)
        dma_cntrl.responseFromDir.in_port = ruby_system.network.out_port
        dma_cntrl.requestToDir = MessageBuffer()
        dma_cntrl.requestToDir.out_port = ruby_system.network.in_port

        #esj 2025-04-18
        # if is_second:
        #     var -= 1
        #     print("esj var -= 1 = ",var)

    all_cntrls = (
        l1_cntrl_nodes + l2_cntrl_nodes + dir_cntrl_nodes + dma_cntrl_nodes
    )

    # Create the io controller and the sequencer
    if full_system:
        print("esj full_system, len(dma_ports) = ",len(dma_ports),"var = ",var)

        #esj 2025-04-18
        # if is_second:
        #     var += 1
        #     print("esj var -= 1 = ",var)

        io_seq = DMASequencer(version=len(dma_ports) + var, #esj 2025-04-18
                              ruby_system=ruby_system,
                              max_outstanding_requests = 2048 )#esj 2024-10-17

        #esj 2024-10-01
        configure_cxl_sequencer(io_seq)
        
        #esj 2024-10-08
        if ObjectList.numa_mode:
            io_seq.numa_mode = True

        ruby_system._io_port = io_seq

        if is_second:
            io_controller = DMA2_Controller(
                version=len(dma_ports) + var, #esj 2025-04-18
                dma_sequencer=io_seq,
                ruby_system=ruby_system,
            )
        else:
            io_controller = DMA_Controller(
                version=len(dma_ports)+ var, #esj 2025-04-18
                dma_sequencer=io_seq,
                ruby_system=ruby_system,
            )

        ruby_system.io_controller = io_controller

        # Connect the dma controller to the network
        io_controller.mandatoryQueue = MessageBuffer()
        io_controller.responseFromDir = MessageBuffer(ordered=True)
        io_controller.responseFromDir.in_port = ruby_system.network.out_port
        io_controller.requestToDir = MessageBuffer()
        io_controller.requestToDir.out_port = ruby_system.network.in_port

        all_cntrls = all_cntrls + [io_controller]

    # esj 2026-02-13 Set the number of virtual networks in the system
    # ruby_system.network.number_of_virtual_networks = 3
    ruby_system.network.number_of_virtual_networks = 4
    topology = create_topology(all_cntrls, options)
    return (cpu_sequencers, mem_dir_cntrl_nodes, topology)



#esj 2025-03-18
def create_system_cxl(
    options, full_system, system, dma_ports, bootmem, ruby_system, cpus
):
    
    print("esj cxl create")

    if buildEnv["PROTOCOL"] != "MESI_Two_Level":
        fatal("This script requires the MESI_Two_Level protocol to be built.")

    all_cntrls=[]
    l2_bits = int(math.log(1, 2))
    print("esj l2_bit =",l2_bits)
    # l2_bits = 0

    cxl_clk_domain = None
    
    cxl_clk_domain = SrcClockDomain(
        clock=options.CXL_clock, voltage_domain=system.voltage_domain
    )
    cxl_divided_clk_domain = DerivedClockDomain(
        clk_domain=cxl_clk_domain, clk_divider=2
    )
        
    print("esj cxl_clk_domain =",cxl_divided_clk_domain)

    CXL_host_controller = CXLhost_Controller(
        l2_select_num_bits=l2_bits,
        version=0,
        ruby_system=ruby_system,
        # esj 2025-05-09
        # clk_domain=cpus[0].clk_domain,
        clk_domain=cxl_divided_clk_domain,
    )

    CXL_host_controller.requestToDevicecxl = MessageBuffer()
    CXL_host_controller.requestToDevicecxl.out_port = ruby_system.network.in_port
    CXL_host_controller.responsefromDevicecxl = MessageBuffer()
    CXL_host_controller.responsefromDevicecxl.in_port = ruby_system.network.out_port
    CXL_host_controller.responseToDevicecxl = MessageBuffer()
    CXL_host_controller.responseToDevicecxl.out_port = ruby_system.network.in_port
    CXL_host_controller.requestfromDevicecxl = MessageBuffer()
    CXL_host_controller.requestfromDevicecxl.in_port = ruby_system.network.out_port
    # CXL_host_controller.requestToMemory = MessageBuffer()
    CXL_host_controller.mandatoryQueue = MessageBuffer()
    
    
    block_size_bits = int(math.log(options.cacheline_size, 2))

    cpu_seq = CXLSequencer(
            version=0,
            # esj 2025-05-09
            # clk_domain=cpus[0].clk_domain,
            clk_domain=cxl_divided_clk_domain,
            ruby_system=ruby_system,
            max_outstanding_requests = 512, #esj 2024-10-17
            deadlock_threshold = 500000000
    )

    CXL_host_controller.cxl_sequencer = cpu_seq
    CXL_host_controller.responseFromMemory = MessageBuffer()
    CXL_host_controller.requestFromMemory = MessageBuffer()
    CXL_host_controller.responseToMemory = MessageBuffer()


    CXL_device_controller = CXLdevice_Controller(
        version=0,
        ruby_system=ruby_system,
        # esj 2025-05-09
        # clk_domain=cpus[0].clk_domain,
        clk_domain=cxl_divided_clk_domain,
    )

    
    
    CXL_device_controller.requestToHostcxl = MessageBuffer()
    CXL_device_controller.requestToHostcxl.out_port = ruby_system.network.in_port
    CXL_device_controller.responsefromHostcxl = MessageBuffer()
    CXL_device_controller.responsefromHostcxl.in_port = ruby_system.network.out_port
    CXL_device_controller.responseToHostcxl = MessageBuffer()
    CXL_device_controller.responseToHostcxl.out_port = ruby_system.network.in_port
    CXL_device_controller.requestfromHostcxl = MessageBuffer()
    CXL_device_controller.requestfromHostcxl.in_port = ruby_system.network.out_port
    CXL_device_controller.requestToMemory = MessageBuffer()
    CXL_device_controller.responseFromMemory = MessageBuffer()
    # CXL_device_controller.requestFromMemory = MessageBuffer()
    CXL_device_controller.mandatoryQueue = MessageBuffer()

    CXL_device_controller.responseToMemory = MessageBuffer()
    
    device_seq = CXLSequencer(
            version=0,
            # esj 2025-05-09
            # clk_domain=cpus[0].clk_domain,
            clk_domain=cxl_divided_clk_domain,
            ruby_system=ruby_system,
            max_outstanding_requests = 512, #esj 2024-10-17
            deadlock_threshold = 500000000
    )

    CXL_device_controller.cxl_sequencer = device_seq
    

    # # CXL_host_controller.sequencer.pio_response_port = system.CXL_ctrl.downstreamRequest
    # CXL_host_controller.cxl_sequencer.in_ports = system.CXL_ctrl.downstreamRequest
    # CXL_host_controller.memory_out_port = system.CXL_ctrl.downstreamResponse
    # CXL_device_controller.memory_out_port = system.CXL_ctrl2.downstreamResponse
    # # CXL_device_controller.sequencer.pio_response_port = system.CXL_ctrl2.downstreamRequest
    # CXL_device_controller.cxl_sequencer.in_ports = system.CXL_ctrl2.downstreamRequest


    CXL_host_controller.cxl_sequencer.in_ports = system.switch.request2
    CXL_host_controller.memory_out_port = system.switch.response_dma2

    #esj 2025-06-22
    CXL_host_controller.cxl_sequencer.in_ports = system.switch.request2_1
    CXL_device_controller.memory_out_port_1 = system.CXL_ctrl2.downstreamResponse2


    CXL_device_controller.memory_out_port = system.CXL_ctrl2.downstreamResponse
    CXL_device_controller.cxl_sequencer.in_ports = system.CXL_ctrl2.downstreamRequest


    CXL_host_controller.cxl_sequencer.mem_request_port = system.iobus.cpu_side_ports
    CXL_device_controller.cxl_sequencer.pio_request_port = system.iobus.cpu_side_ports

    all_cntrls.append(CXL_host_controller)
    all_cntrls.append(CXL_device_controller)

    ruby_system.network.number_of_virtual_networks = 4
    topology = create_topology(all_cntrls, options)


    return (topology)
