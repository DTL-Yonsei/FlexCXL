# Copyright (c) 2010-2012, 2015-2019 ARM Limited
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
# Copyright (c) 2010-2011 Advanced Micro Devices, Inc.
# Copyright (c) 2006-2008 The Regents of The University of Michigan
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

import m5
import m5.defines
from m5.objects import *
from m5.util import *
from common.Benchmarks import *
from common import ObjectList

# Populate to reflect supported os types per target ISA
os_types = set()
if m5.defines.buildEnv["USE_ARM_ISA"]:
    os_types.update(
        [
            "linux",
            "android-gingerbread",
            "android-ics",
            "android-jellybean",
            "android-kitkat",
            "android-nougat",
        ]
    )
if m5.defines.buildEnv["USE_MIPS_ISA"]:
    os_types.add("linux")
if m5.defines.buildEnv["USE_POWER_ISA"]:
    os_types.add("linux")
if m5.defines.buildEnv["USE_RISCV_ISA"]:
    os_types.add("linux")  # TODO that's a lie
if m5.defines.buildEnv["USE_SPARC_ISA"]:
    os_types.add("linux")
if m5.defines.buildEnv["USE_X86_ISA"]:
    os_types.add("linux")


class CowIdeDisk(IdeDisk):
    image = CowDiskImage(child=RawDiskImage(read_only=True), read_only=False)

    def childImage(self, ci):
        self.image.child.image_file = ci


class MemBus(SystemXBar):
    badaddr_responder = BadAddr()
    default = Self.badaddr_responder.pio


def attach_9p(parent, bus):
    viopci = PciVirtIO()
    viopci.vio = VirtIO9PDiod()
    viodir = os.path.realpath(os.path.join(m5.options.outdir, "9p"))
    viopci.vio.root = os.path.join(viodir, "share")
    viopci.vio.socketPath = os.path.join(viodir, "socket")
    os.makedirs(viopci.vio.root, exist_ok=True)
    if os.path.exists(viopci.vio.socketPath):
        os.remove(viopci.vio.socketPath)
    parent.viopci = viopci
    parent.attachPciDevice(viopci, bus)


def fillInCmdline(mdesc, template, **kwargs):
    kwargs.setdefault("rootdev", mdesc.rootdev())
    kwargs.setdefault("mem", mdesc.mem())
    kwargs.setdefault("script", mdesc.script())
    return template % kwargs


def reset_x86_acpi_tables(workload):
    # Keep each full-system guest from inheriting RSDT/XSDT entries that were
    # appended while constructing a previous host.
    workload.acpi_description_table_pointer = X86ACPIRSDP(
        rsdt=X86ACPIRSDT(entries=[]),
        xsdt=X86ACPIXSDT(entries=[]),
    )


def makeCowDisks(disk_paths):
    disks = []
    for disk_path in disk_paths:
        disk = CowIdeDisk(driveID="device0")
        disk.childImage(disk_path)
        disks.append(disk)
    return disks


def x86IOAddress(port):
    IO_address_space_base = 0x8000000000000000
    return IO_address_space_base + port


def connectX86ClassicSystem(x86_sys, numCPUs):
    # Constants similar to x86_traits.hh
    IO_address_space_base = 0x8000000000000000
    pci_config_address_space_base = 0xC000000000000000
    interrupts_address_space_base = 0xA000000000000000
    APIC_range_size = 1 << 12

    x86_sys.membus = MemBus(numa_mode=ObjectList.numa_mode)
    x86_sys.membus.numa_mode = ObjectList.numa_mode

    # North Bridge
    x86_sys.iobus = IOXBar()
    x86_sys.bridge = Bridge()
    # x86_sys.bridge = Bridge(CXLDRAMsim3)
    x86_sys.bridge.mem_side_port = x86_sys.iobus.cpu_side_ports
    x86_sys.bridge.cpu_side_port = x86_sys.membus.mem_side_ports
    # Allow the bridge to pass through:
    #  1) kernel configured PCI device memory map address: address range
    #     [0xC0000000, 0xFFFF0000). (The upper 64kB are reserved for m5ops.)
    #  2) the bridge to pass through the IO APIC (two pages, already contained in 1),
    #  3) everything in the IO address range up to the local APIC, and
    #  4) then the entire PCI address space and beyond.
    x86_sys.bridge.ranges = [
        # AddrRange(0xC0000000, 0xFFFF0000),
        # AddrRange(0xC00000000, 0xFEE000000 - 1),
        AddrRange(0xC0000000, 0xFEE00000 - 1),
        AddrRange(
            0xFEF00000, 0xFFFF0000
        ),  # Address range 0xFEE00000 to 0xFEF00000 is reserved for MSI/MSI-X
        AddrRange(IO_address_space_base, interrupts_address_space_base - 1),
        AddrRange(pci_config_address_space_base, Addr.max),
    ]

    # Create a bridge from the IO bus to the memory bus to allow access to
    # the local APIC (two pages)
    x86_sys.apicbridge = Bridge(delay="50ns")
    x86_sys.apicbridge.cpu_side_port = x86_sys.iobus.mem_side_ports
    x86_sys.apicbridge.mem_side_port = x86_sys.membus.cpu_side_ports
    x86_sys.apicbridge.ranges = [
        AddrRange(
            interrupts_address_space_base,
            interrupts_address_space_base + numCPUs * APIC_range_size - 1,
        )
    ]

    # connect the io bus
    x86_sys.pc.attachIO(x86_sys.iobus)

    x86_sys.system_port = x86_sys.membus.cpu_side_ports


def connectX86RubySystem(x86_sys):
    # North Bridge
    x86_sys.iobus = IOXBar()

    # add the ide to the list of dma devices that later need to attach to
    # dma controllers
    x86_sys._dma_ports = [x86_sys.pc.south_bridge.ide.dma]
    x86_sys.pc.attachIO(x86_sys.iobus, x86_sys._dma_ports)


# def connectX86CXLMemory(x86_sys, pcie_devices= [], cxl_size = None, Ruby = None):


#     # x86_sys.pcie1 = PCIELink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10) #, delay_var='1ns'
#     # x86_sys.pcie3 = PCIELink(lanes = 8, speed = '16Gbps',  mps = 64, max_queue_size= 1024)
#     # x86_sys.pcie4 = PCIELink(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10)
#     # x86_sys.pcie5 = PCIELink(lanes = 16 , speed = '16Gbps' ,mps = 64, max_queue_size= 10)
#     # x86_sys.pcie6 = PCIELink(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10)
#     # x86_sys.pcie7 = PCIELink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10)

#     x86_sys.pcie1 = CXLLink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10) #, delay_var='1ns'
#     x86_sys.pcie3 = CXLLink(lanes = 8, speed = '16Gbps',  mps = 64, max_queue_size= 16)
#     x86_sys.pcie4 = CXLLink(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10)
#     x86_sys.CXL_ctrl = CXL_ctrl(lanes = 16 , speed = '16Gbps' ,mps = 64, max_queue_size= 10)
#     x86_sys.CXL_ctrl2 = CXL_ctrl(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10)
#     x86_sys.pcie7 = CXLLink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10)
#     x86_sys.CXL_ctrl.attachIO()
#     x86_sys.CXL_ctrl2.attachIO()

#     x86_sys.CXL_ctrl.downstreamResponse = x86_sys.CXL_ctrl2.downstreamRequest
#     x86_sys.CXL_ctrl.downstreamRequest = x86_sys.CXL_ctrl2.downstreamResponse


#     if Ruby:
#         # x86_sys.switch = PCIESwitch(delay='50ns')
#         x86_sys.switch = PCIESwitch(delay='50ns')
#     else:
#         x86_sys.switch = PCIESwitch()

#     #host setting
#     x86_sys.switch.host = x86_sys.pc.pci_host

#     # x86_sys.switch.response  = x86_sys.iobus.mem_side_ports

#     if Ruby:
#         # x86_sys._dma_ports.append(x86_sys.switch.request_dma)
#         x86_sys.switch.response  = x86_sys.iobus.mem_side_ports
#         x86_sys.iobus.cpu_side_ports = x86_sys.switch.request_dma
#     else:
#         x86_sys.switch.response  = x86_sys.iobus.mem_side_ports
#         x86_sys.switch.request_dma = x86_sys.membus.cpu_side_ports

#     x86_sys.switch.response_dma1  = x86_sys.pcie3.upstreamRequest
#     x86_sys.switch.response_dma2  = x86_sys.pcie4.upstreamRequest
#     x86_sys.switch.response_dma3  = x86_sys.CXL_ctrl.upstreamRequest
#     x86_sys.switch.request1 = x86_sys.pcie3.upstreamResponse
#     x86_sys.switch.request2 = x86_sys.pcie4.upstreamResponse
#     x86_sys.switch.request3 = x86_sys.CXL_ctrl.upstreamResponse

#     # x86_sys.cxl_bridge = Bridge(delay='50ns',req_size=64, resp_size=64)
#     # x86_sys.cxl_bridge.ranges = [AddrRange(0xC0000000, 0xF0000000),]
#     # x86_sys.iobus.cpu_side_ports = x86_sys.pcie3.upstreamRequest

#     # x86_sys.cxl_bridge.cpu_side_port = x86_sys.iobus.mem_side_ports
#     # x86_sys.cxl_bridge.mem_side_port = x86_sys.pcie3.upstreamResponse

#     if ObjectList.cxl_mode:

#         x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 3 ,pci_dev= 1, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini")

#         # x86_sys.pc.cxlmemdevice1.pio = x86_sys.pcie3.downstreamRequest

#         comm_monitor_cxlmem = CommMonitor()
#         x86_sys.comm_monitor_CXLMem = comm_monitor_cxlmem
#         #2024-11-28
#         # x86_sys.pcie3.downstreamRequest = comm_monitor_cxlmem.cpu_side_port
#         x86_sys.CXL_ctrl2.upstreamRequest = comm_monitor_cxlmem.cpu_side_port

#         comm_monitor_cxlmem.mem_side_port = x86_sys.pc.cxlmemdevice1.pio
#         # comm_monitor_cxlmem.trace = MemTraceProbe(trace_compress=False, trace_file = "cxl_mem_trace.trc.gz")

#         #2024-11-28
#         # x86_sys.pc.cxlmemdevice1.dma = x86_sys.pcie3.downstreamResponse
#         x86_sys.pc.cxlmemdevice1.dma = x86_sys.CXL_ctrl2.upstreamResponse

#         x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host
#         # x86_sys.pc.cxlmemdevice1.host = x86_sys.switch.host
#         pcie_devices.append(x86_sys.pc.cxlmemdevice1)
#         if Ruby:
#             x86_sys.iobus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
#         else:
#             x86_sys.membus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size

#     # x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 0 ,pci_dev= 2, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_4Gb_x4_2400.ini")
#     # x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host
#     # x86_sys.iobus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
#     # x86_sys.iobus.cpu_side_ports = x86_sys.pc.cxlmemdevice1.dma
#     # x86_sys.iobus.mem_side_ports = x86_sys.pc.cxlmemdevice1.pio
#     # pcie_devices.append(x86_sys.pc.cxlmemdevice1)


def connectX86CXLMemory(x86_sys, pcie_devices=[], cxl_size=None, Ruby=None):

    index = 25
    print("index", index)
    config = [
        (0.01, 32, 10, 10),  # index 1
        (0.01, 32, 10, 100),  # index 2
        (0.01, 32, 100, 10),  # index 3
        (0.01, 32, 100, 100),  # index 4
        (0.05, 32, 10, 10),  # index 5
        (0.05, 32, 10, 100),  # index 6
        (0.05, 32, 100, 10),  # index 7
        (0.05, 32, 100, 100),  # index 8
        (0.1, 32, 10, 10),  # index 9
        (0.1, 32, 10, 100),  # index 10
        (0.1, 32, 100, 10),  # index 11
        (0.1, 32, 100, 100),  # index 12
        (0, 8, 10, 100),  # index 13
        (0, 16, 10, 100),  # index 14
        (0, 32, 10, 100),  # index 15
        (0, 64, 10, 100),  # index 16
        (0, 128, 100, 100),  # index 17
        (0.5, 16, 100, 100),  # index 18
        (0.5, 8, 100, 100),  # index 19
        (0, 32, 100, 100),  # index 20
        (0, 8, 100, 100),  # index 21
        (0, 16, 100, 100),  # index 22
        (0, 32, 100, 100),  # index 23
        (0, 64, 100, 100),  # index 24
        (0, 128, 10, 100),  # index 25
    ]

    if index < 1 or index > len(config):
        raise ValueError(f"index는 1~{len(config)} 범위여야 함")

    error_rate, buffer_size, ack_cycles, nak_cycles = config[index - 1]
    configured_error_rate = getattr(ObjectList, "cxl_error_rate", None)
    if configured_error_rate is not None:
        error_rate = configured_error_rate

    print(
        "error_rate",
        error_rate,
        "buffer_size",
        buffer_size,
        "ack_cycles",
        ack_cycles,
        "nak_cycles",
        nak_cycles,
    )

    x86_sys.pcie3 = CXL_ctrl(
        lanes=8,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        Host=True,
        crc_checker=CXL_CRC_check(success_rate=error_rate),
    )
    x86_sys.pcie4 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        Host=True,
        crc_checker=CXL_CRC_check(success_rate=error_rate),
    )

    x86_sys.CXL_ctrl = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        Host=True,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )

    x86_sys.CXL_ctrl2 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )

    x86_sys.CXL_ctrl.attachIO_host()
    x86_sys.pcie3.attachIO_host()
    x86_sys.pcie4.attachIO_host()
    x86_sys.CXL_ctrl2.attachIO_device()

    # x86_sys.CXL_ctrl.downstreamResponse = x86_sys.CXL_ctrl2.downstreamRequest
    # x86_sys.CXL_ctrl.downstreamRequest = x86_sys.CXL_ctrl2.downstreamResponse

    ###########
    x86_sys.pcielink = PCIELink(
        lanes=16, speed="16Gbps", mps=64, max_queue_size=10
    )
    x86_sys.CXL_ctrl.flexbus.PCIe_req_port = x86_sys.pcielink.upstreamResponse
    x86_sys.CXL_ctrl.flexbus.PCIe_resp_port = x86_sys.pcielink.upstreamRequest
    x86_sys.CXL_ctrl2.flexbus.PCIe_req_port = (
        x86_sys.pcielink.downstreamResponse
    )
    x86_sys.CXL_ctrl2.flexbus.PCIe_resp_port = (
        x86_sys.pcielink.downstreamRequest
    )

    if Ruby:
        # x86_sys.switch = PCIESwitch(delay='50ns')
        x86_sys.switch = PCIESwitch(
            delay="50ns",
            req_size=buffer_size,
            resp_size=buffer_size,
            cxl_chbs_base=CXL_CHBS_BASE,
            cxl_chbs_size=CXL_CHBS_SIZE,
            cxl_hdm_base=ObjectList.cxl_mem_start,
            cxl_hdm_size=cxl_size,
            host_cxl_hdm_sizes=getattr(
                ObjectList, "cxl_host_visible_sizes", [cxl_size]
            ),
        )
    else:
        x86_sys.switch = PCIESwitch()

    # host setting
    x86_sys.switch.host = x86_sys.pc.pci_host

    # x86_sys.switch.response  = x86_sys.iobus.mem_side_ports

    if Ruby:
        # x86_sys._dma_ports.append(x86_sys.switch.request_dma)
        # esj 2025-04-21
        x86_sys.switch.response1 = x86_sys.iobus.mem_side_ports
        x86_sys.iobus.cpu_side_ports = x86_sys.switch.request_dma1
    else:
        x86_sys.switch.response = x86_sys.iobus.mem_side_ports
        x86_sys.switch.request_dma = x86_sys.membus.cpu_side_ports

    x86_sys.switch.response_dma1 = x86_sys.CXL_ctrl.upstreamRequest
    x86_sys.switch.response_dma2 = x86_sys.pcie4.upstreamRequest
    x86_sys.switch.response_dma3 = x86_sys.pcie3.upstreamRequest
    x86_sys.switch.request1 = x86_sys.CXL_ctrl.upstreamResponse
    x86_sys.switch.request2 = x86_sys.pcie4.upstreamResponse
    x86_sys.switch.request3 = x86_sys.pcie3.upstreamResponse

    if ObjectList.cxl_mode:
        x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(
            switch_mode=ObjectList.switch_mode,
            pci_bus=3,
            pci_dev=1,
            pci_func=0,
            cxl_mem_size=cxl_size,
            cxl_mem_start=ObjectList.cxl_mem_start,
            cxl_chbs_base=CXL_CHBS_BASE,
            cxl_chbs_size=CXL_CHBS_SIZE,
            configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini",
            configFile_write="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400_write.ini",
            CapabilityPtr=0x48,
            ClassCode=0x05,
            SubClassCode=0x02,
            ProgIF=0x10,
            DeviceID=0x0,
            VendorID=0x1E98,
        )
        # x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 3 ,pci_dev= 1, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini")

        # comm_monitor_cxlmem = CommMonitor()
        # x86_sys.comm_monitor_CXLMem = comm_monitor_cxlmem
        # x86_sys.CXL_ctrl2.upstreamRequest = comm_monitor_cxlmem.cpu_side_port
        # comm_monitor_cxlmem.mem_side_port = x86_sys.pc.cxlmemdevice1.pio
        # comm_monitor_cxlmem.trace = MemTraceProbe(trace_compress=False, trace_file = "cxl_mem_trace.trc.gz")

        x86_sys.CXL_ctrl2.upstreamRequest = x86_sys.pc.cxlmemdevice1.pio

        x86_sys.pc.cxlmemdevice1.dma = x86_sys.CXL_ctrl2.upstreamResponse
        x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host

        pcie_devices.append(x86_sys.pc.cxlmemdevice1)
        ObjectList.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
        ObjectList.cxl_bar_start = CXL_PCI_MMIO_BASE
        if Ruby:
            x86_sys.iobus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
        else:
            x86_sys.membus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size


def connectX86CXLMemory2(
    x86_sys,
    pcie_devices=[],
    cxl_size=None,
    Ruby=None,
    cxl_device_size=None,
    connect_primary_host_upstream=True,
):
    if cxl_device_size is None:
        cxl_device_size = cxl_size

    index = 29
    print("index", index)
    config = [
        (0.01, 32, 10, 10),  # index 1
        (0.01, 32, 10, 100),  # index 2
        (0.01, 32, 100, 10),  # index 3
        (0.01, 32, 100, 100),  # index 4
        (0.05, 32, 10, 10),  # index 5
        (0.05, 32, 10, 100),  # index 6
        (0.05, 32, 100, 10),  # index 7
        (0.05, 32, 100, 100),  # index 8
        (0.1, 32, 10, 10),  # index 9
        (0.1, 32, 10, 100),  # index 10
        (0.1, 32, 100, 10),  # index 11
        (0.1, 32, 100, 100),  # index 12
        (0, 8, 10, 100),  # index 13
        (0, 16, 10, 100),  # index 14
        (0, 32, 10, 100),  # index 15
        (0, 64, 10, 100),  # index 16
        (0, 128, 100, 100),  # index 17
        (0.5, 16, 100, 100),  # index 18
        (0.5, 8, 100, 100),  # index 19
        (0, 32, 100, 100),  # index 20
        (0, 8, 100, 100),  # index 21
        (0, 16, 100, 100),  # index 22
        (0, 32, 100, 100),  # index 23
        (0, 64, 100, 100),  # index 24
        (0, 128, 10, 100),  # index 25
        (0.001, 32, 1000, 1000),  # index 26
        (0.01, 24, 50, 50),  # index 27
        (0.01, 24, 1000, 1000),  # index 28
        (0, 24, 50, 50),  # index 29
    ]

    if index < 1 or index > len(config):
        raise ValueError(f"index는 1~{len(config)} 범위여야 함")

    error_rate, buffer_size, ack_cycles, nak_cycles = config[index - 1]
    configured_error_rate = getattr(ObjectList, "cxl_error_rate", None)
    if configured_error_rate is not None:
        error_rate = configured_error_rate
    logical_device_count = max(
        1, int(getattr(ObjectList, "cxl_logical_device_count", 1))
    )
    # Each host has an independent link-level replay window, but the device
    # controller and its PHY pipeline are shared.  Size that shared capacity
    # for the aggregate of all logical host windows.
    device_buffer_size = buffer_size * logical_device_count

    print(
        "error_rate",
        error_rate,
        "buffer_size",
        buffer_size,
        "device_buffer_size",
        device_buffer_size,
        "ack_cycles",
        ack_cycles,
        "nak_cycles",
        nak_cycles,
    )

    # x86_sys.pcie3 = CXL_ctrl(lanes = 8, speed = '16Gbps',  mps = 64, max_queue_size= buffer_size,
    #                          Host = True, crc_checker = CXL_CRC_check(success_rate=error_rate))
    # x86_sys.pcie4 = CXL_ctrl(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= buffer_size,
    #                          Host = True, crc_checker = CXL_CRC_check(success_rate=error_rate))

    # x86_sys.pcie4 = PCIELink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10)
    # x86_sys.pcie3 = PCIELink(lanes = 16, speed = '16Gbps',  mps = 64, max_queue_size= 10)

    x86_sys.CXL_ctrl = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        Host=True,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )

    x86_sys.CXL_ctrl2 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=device_buffer_size,
        read_queue_size=device_buffer_size / 2,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=device_buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=device_buffer_size),
        packing=CXL_Packing(max_queue_size=device_buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=device_buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=device_buffer_size),
        framer=CXL_Framer(max_queue_size=device_buffer_size),
        deframer=CXL_Deframer(max_queue_size=device_buffer_size),
        decoder=CXL_Decoder(max_queue_size=device_buffer_size),
        encoder=CXL_Encoder(max_queue_size=device_buffer_size),
    )

    x86_sys.CXL_ctrl3 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        Host=True,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )
    x86_sys.CXL_ctrl4 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        Host=True,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )
    x86_sys.CXL_ctrl5 = CXL_ctrl(
        lanes=16,
        speed="16Gbps",
        mps=64,
        max_queue_size=buffer_size,
        read_queue_size=buffer_size / 2,
        Host=True,
        control_flit_cycle=ack_cycles,
        crc_error_control_flit_cycle=nak_cycles,
        crc_checker=CXL_CRC_check(
            success_rate=error_rate, max_queue_size=buffer_size
        ),
        crc=CXL_CRC(error_rate=error_rate, max_queue_size=buffer_size),
        packing=CXL_Packing(max_queue_size=buffer_size),
        unpacking=CXL_Unpacking(max_queue_size=buffer_size),
        flexbus=CXL_FlexBus(max_queue_size=buffer_size),
        framer=CXL_Framer(max_queue_size=buffer_size),
        deframer=CXL_Deframer(max_queue_size=buffer_size),
        decoder=CXL_Decoder(max_queue_size=buffer_size),
        encoder=CXL_Encoder(max_queue_size=buffer_size),
    )

    x86_sys.CXL_ctrl.attachIO_host()
    x86_sys.CXL_ctrl3.attachIO_host()
    x86_sys.CXL_ctrl4.attachIO_host()
    x86_sys.CXL_ctrl5.attachIO_host()
    # x86_sys.pcie3.attachIO_host()
    # x86_sys.pcie4.attachIO_host()
    x86_sys.CXL_ctrl2.attachIO_device()

    ###########
    x86_sys.pcielink = PCIELink(
        lanes=16, speed="16Gbps", mps=64, max_queue_size=10
    )
    x86_sys.CXL_ctrl.flexbus.PCIe_req_port = x86_sys.pcielink.upstreamResponse
    x86_sys.CXL_ctrl.flexbus.PCIe_resp_port = x86_sys.pcielink.upstreamRequest
    x86_sys.CXL_ctrl2.flexbus.PCIe_req_port = (
        x86_sys.pcielink.downstreamResponse
    )
    x86_sys.CXL_ctrl2.flexbus.PCIe_resp_port = (
        x86_sys.pcielink.downstreamRequest
    )

    if Ruby:
        # x86_sys.switch = PCIESwitch(delay='50ns')
        x86_sys.switch = PCIESwitch(
            delay="50ns",
            req_size=buffer_size,
            resp_size=buffer_size,
            PXCAPDevCtrl=0x51A0,
            PXCAPDevCapabilities=0x10000265,
            PXCAPLinkCtrl=0x0,
            PXCAPCapabilities=0x0062,
            PXCAPCapabilities_downstream=0x0062,
            PXCAPCapabilities_upstream=0x0042,
            pci_dev1=2,
            pci_dev2=4,
            pci_dev3=6
            # ,VendorId = 0x1e98, DeviceId_upstream = 0x3, PXCAPCapId = 0x1e, PXCAPBaseOffset = 0x40
            ,
            routing_mode_type=ObjectList.routing_mode_type,
            priority_limit=ObjectList.priority_limit,
            priority_host_ratios=ObjectList.priority_host_ratios,
            cxl_chbs_base=CXL_CHBS_BASE,
            cxl_chbs_size=CXL_CHBS_SIZE,
            cxl_hdm_base=ObjectList.cxl_mem_start,
            cxl_hdm_size=cxl_size,
            host_cxl_hdm_sizes=getattr(
                ObjectList, "cxl_host_visible_sizes", [cxl_size]
            ),
        )
    else:
        x86_sys.switch = PCIESwitch()

    # host setting
    x86_sys.switch.host = x86_sys.pc.pci_host

    # x86_sys.pcibridge = Bridge(delay="50ns")
    # x86_sys.pcibridge.mem_side_port = x86_sys.CXL_ctrl.upstreamResponse
    # x86_sys.pcibridge.cpu_side_port = x86_sys.iobus.mem_side_ports
    # x86_sys.pcibridge.ranges = [AddrRange(0xC0000000, 0xFFFF0000)]
    if connect_primary_host_upstream:
        x86_sys.CXL_ctrl.upstreamResponse = x86_sys.iobus.mem_side_ports
        x86_sys.CXL_ctrl.upstreamRequest = x86_sys.iobus.cpu_side_ports

    x86_sys.CXL_ctrl.downstreamRequest = x86_sys.switch.response1
    x86_sys.CXL_ctrl.downstreamResponse = x86_sys.switch.request_dma1

    # esj 2025-06-22
    x86_sys.CXL_ctrl.downstreamRequest2 = x86_sys.switch.response1_1
    if connect_primary_host_upstream:
        x86_sys.CXL_ctrl.upstreamResponse2 = x86_sys.iobus.mem_side_ports

    # esj 2025-05-08
    x86_sys.CXL_ctrl3.downstreamRequest = x86_sys.switch.response2
    x86_sys.CXL_ctrl3.downstreamResponse = x86_sys.switch.request_dma2

    # esj 2025-06-22
    x86_sys.CXL_ctrl3.downstreamRequest2 = x86_sys.switch.response2_1

    # esj 2026-04-06 host3/4 control path
    x86_sys.CXL_ctrl4.downstreamRequest = x86_sys.switch.response3
    x86_sys.CXL_ctrl4.downstreamRequest2 = x86_sys.switch.response3_1
    x86_sys.CXL_ctrl4.downstreamResponse = x86_sys.switch.request_dma3

    x86_sys.CXL_ctrl5.downstreamRequest = x86_sys.switch.response4
    x86_sys.CXL_ctrl5.downstreamRequest2 = x86_sys.switch.response4_1
    x86_sys.CXL_ctrl5.downstreamResponse = x86_sys.switch.request_dma4

    if ObjectList.cxl_mode:
        x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(
            switch_mode=ObjectList.switch_mode,
            # Keep the guest-visible CXL endpoint on the same direct bus
            # layout as the single-host kernel path.
            pci_bus=1,
            pci_dev=0,
            pci_func=0,
            cxl_mem_size=cxl_device_size,
            cxl_mem_start=ObjectList.cxl_mem_start,
            cxl_logical_device_count=getattr(
                ObjectList, "cxl_logical_device_count", 1
            ),
            host_visible_cxl_mem_sizes=getattr(
                ObjectList, "cxl_host_visible_sizes", [cxl_size]
            ),
            host_cxl_device_offsets=getattr(
                ObjectList, "cxl_host_device_offsets", [0]
            ),
            Multiple_Logical_Device=(
                1
                if getattr(ObjectList, "cxl_logical_device_count", 1) > 1
                else 0
            ),
            cxl_chbs_base=CXL_CHBS_BASE,
            cxl_chbs_size=CXL_CHBS_SIZE,
            configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini",
            configFile_write="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400_write.ini",
            ClassCode=0x05,
            SubClassCode=0x02,
            ProgIF=0x10,
            DeviceID=0x0,
            VendorID=0x1E98,
            CapabilityPtr=0x48,
        )

        # x86_sys.switch.request1 = x86_sys.pc.cxlmemdevice1.pio

        # x86_sys.pc.cxlmemdevice1.dma = x86_sys.switch.response_dma1
        x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host

        x86_sys.CXL_ctrl2.upstreamRequest = x86_sys.pc.cxlmemdevice1.pio
        x86_sys.CXL_ctrl2.upstreamResponse = x86_sys.pc.cxlmemdevice1.dma

        # esj 2025-06-22
        x86_sys.CXL_ctrl2.upstreamRequest2 = x86_sys.pc.cxlmemdevice1.Pio2
        x86_sys.switch.request3 = x86_sys.CXL_ctrl2.upstreamResponse2

        pcie_devices.append(x86_sys.pc.cxlmemdevice1)
        ObjectList.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
        ObjectList.cxl_bar_start = CXL_PCI_MMIO_BASE


# def connectX86CXLMemory_non_switch(x86_sys, pcie_devices= [], cxl_size = None, Ruby = None):

#     if ObjectList.cxl_mode:
#         x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 0 ,pci_dev= 6, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini",
#                                                configFile_write="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400_write.ini",
#                                                CapabilityPtr = 0xB4,
#                                                ClassCode = 0x05,SubClassCode = 0x02,ProgIF = 0x10)

#         # x86_sys.pc.cxlmemdevice1 = CxlMemory(pci_bus = 0 ,pci_dev= 6, pci_func=0,cxl_mem_size=cxl_size,
#         #                                        CapabilityPtr = 0xB4,
#         #                                        ClassCode = 0x05,SubClassCode = 0x02,ProgIF = 0x10)

#         # x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 3 ,pci_dev= 1, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini")

#         comm_monitor_cxlmem = CommMonitor()
#         x86_sys.comm_monitor_CXLMem = comm_monitor_cxlmem
#         x86_sys.iobus.mem_side_ports = comm_monitor_cxlmem.cpu_side_port
#         comm_monitor_cxlmem.mem_side_port = x86_sys.pc.cxlmemdevice1.pio
#         # comm_monitor_cxlmem.trace = MemTraceProbe(trace_compress=False, trace_file = "cxl_mem_trace.trc.gz")

#         x86_sys.pc.cxlmemdevice1.dma = x86_sys.iobus.cpu_side_ports
#         x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host

#         pcie_devices.append(x86_sys.pc.cxlmemdevice1)
#         if Ruby:
#             x86_sys.iobus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
#         else:
#             x86_sys.membus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size

# def connectX86CXLMemory(x86_sys, pcie_devices= [], cxl_size = None, Ruby = None):

#     x86_sys.pcie3 = CXL_ctrl(lanes = 8, speed = '16Gbps',  mps = 64, max_queue_size= 16,
#                              Host = True, delay = '2ns',crc_checker = CXL_CRC_check(success_rate=0.1))
#     x86_sys.pcie4 = CXL_ctrl(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10,
#                              Host = True, delay = '2ns',crc_checker = CXL_CRC_check(success_rate=0.1))
#     x86_sys.CXL_ctrl = CXL_ctrl(lanes = 16 , speed = '16Gbps' ,mps = 64, max_queue_size= 10,
#                                 Host = True, delay = '2ns',crc_checker = CXL_CRC_check(success_rate=0))
#     x86_sys.CXL_ctrl2 = CXL_ctrl(lanes = 16 , speed = '16Gbps', mps = 64, max_queue_size= 10,
#                                  delay = '2ns',crc_checker = CXL_CRC_check(success_rate=0))
#     x86_sys.CXL_ctrl.attachIO_host()
#     x86_sys.pcie3.attachIO_host()
#     x86_sys.pcie4.attachIO_host()
#     x86_sys.CXL_ctrl2.attachIO_device()


#     if Ruby:
#         # x86_sys.switch = PCIESwitch(delay='50ns')
#         x86_sys.switch = PCIESwitch(delay='50ns')
#     else:
#         x86_sys.switch = PCIESwitch()

#     #host setting
#     x86_sys.switch.host = x86_sys.pc.pci_host

#     # x86_sys.switch.response  = x86_sys.iobus.mem_side_ports

#     if Ruby:
#         # x86_sys._dma_ports.append(x86_sys.switch.request_dma)
#         x86_sys.CXL_ctrl.upstreamResponse  = x86_sys.iobus.mem_side_ports
#         x86_sys.iobus.cpu_side_ports = x86_sys.CXL_ctrl.upstreamRequest
#         x86_sys.CXL_ctrl.downstreamRequest = x86_sys.switch.response
#         x86_sys.CXL_ctrl.downstreamResponse = x86_sys.switch.request_dma

#     else:
#         x86_sys.switch.response  = x86_sys.iobus.mem_side_ports
#         x86_sys.switch.request_dma = x86_sys.membus.cpu_side_ports

#     x86_sys.switch.response_dma1  = x86_sys.CXL_ctrl2.downstreamRequest
#     x86_sys.switch.response_dma2  = x86_sys.pcie4.upstreamRequest
#     x86_sys.switch.response_dma3  = x86_sys.pcie3.upstreamRequest
#     x86_sys.switch.request1 = x86_sys.CXL_ctrl2.downstreamResponse
#     x86_sys.switch.request2 = x86_sys.pcie4.upstreamResponse
#     x86_sys.switch.request3 = x86_sys.pcie3.upstreamResponse


#     if ObjectList.cxl_mode:
#         x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(switch_mode = ObjectList.switch_mode, pci_bus = 3 ,pci_dev= 1, pci_func=0,cxl_mem_size=cxl_size,configFile="ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini")

#         comm_monitor_cxlmem = CommMonitor()
#         x86_sys.comm_monitor_CXLMem = comm_monitor_cxlmem
#         x86_sys.CXL_ctrl2.upstreamRequest = comm_monitor_cxlmem.cpu_side_port
#         comm_monitor_cxlmem.mem_side_port = x86_sys.pc.cxlmemdevice1.pio
#         # comm_monitor_cxlmem.trace = MemTraceProbe(trace_compress=False, trace_file = "cxl_mem_trace.trc.gz")

#         x86_sys.pc.cxlmemdevice1.dma = x86_sys.CXL_ctrl2.upstreamResponse
#         x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host

#         pcie_devices.append(x86_sys.pc.cxlmemdevice1)
#         if Ruby:
#             x86_sys.iobus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size
#         else:
#             x86_sys.membus.cxl_bar_size = x86_sys.pc.cxlmemdevice1.BAR0.size


def format_memory_size(decimal_value):
    if decimal_value < 0:
        return "error wrong value"

    suffixes = ["B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"]
    index = 0

    while decimal_value >= 1024 and index < len(suffixes) - 1:
        decimal_value /= 1024.0
        index += 1

    formatted_size = "{}{}".format(int(decimal_value), suffixes[index])
    return formatted_size


CXL_FW_ALIGN = 256 * 1024 * 1024
CXL_PCI_MMIO_BASE = 0xC0000000
CXL_CHBS_BASE = 0xE0000000
CXL_CHBS_SIZE = 0x10000
CXL_MCFG_BASE = 0xE1000000
CXL_MCFG_SIZE = 256 * 1024 * 1024
CXL_FADT_PM1A_EVT_BLK = 0xB000
CXL_FADT_PM1A_CNT_BLK = 0xB004
CXL_FADT_PM1_EVT_LEN = 4
CXL_FADT_PM1_CNT_LEN = 2


def align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def addr_range_end(addr_range):
    return int(addr_range.start) + int(addr_range.size())


def cxl_firmware_base(mem_ranges):
    ram_end = max([addr_range_end(r) for r in mem_ranges])
    return align_up(max(int(Addr("4GB")), ram_end), CXL_FW_ALIGN)


def append_cxl_system_ram_ranges(system, cxl_size, pcie_devices):
    ObjectList.simplememory_iter_start = (
        3 if len(system.mem_ranges) == 2 else 2
    )
    ObjectList.cxl_mem_size = cxl_size

    device_count = max(1, len(pcie_devices))
    for i in range(device_count):
        system.mem_ranges.append(
            AddrRange(ObjectList.cxl_mem_start + cxl_size * i, size=cxl_size)
        )


def cxl_partition_values(mdesc):
    total_from_option = convert.toMemorySize(mdesc.cxlmem())
    cxl_slice_size = (
        mdesc.CXL_slice_size() if hasattr(mdesc, "CXL_slice_size") else None
    )
    if cxl_slice_size is None:
        cxl_slice_size = total_from_option * mdesc.CXL_ratio() // 10

    cxl_total_size = (
        mdesc.CXL_total_size() if hasattr(mdesc, "CXL_total_size") else None
    )
    if cxl_total_size is None:
        cxl_total_size = total_from_option

    cxl_device_offset = (
        mdesc.CXL_device_offset() if hasattr(mdesc, "CXL_device_offset") else 0
    )
    return int(cxl_slice_size), int(cxl_total_size), int(cxl_device_offset)


def set_cxl_fetch_window(system):
    if ObjectList.cxl_mode:
        system.cxl_mem_start = ObjectList.cxl_mem_start
        system.cxl_mem_size = ObjectList.cxl_mem_size


def aml_pkg_len(length):
    if length <= 0x3F:
        return [length]
    if length <= 0xFFF:
        return [0x40 | (length & 0x0F), (length >> 4) & 0xFF]
    if length <= 0xFFFFF:
        return [
            0x80 | (length & 0x0F),
            (length >> 4) & 0xFF,
            (length >> 12) & 0xFF,
        ]
    return [
        0xC0 | (length & 0x0F),
        (length >> 4) & 0xFF,
        (length >> 12) & 0xFF,
        (length >> 20) & 0xFF,
    ]


def aml_pkg_len_for_payload(payload_len):
    for encoded_len in range(1, 5):
        encoded = aml_pkg_len(payload_len + encoded_len)
        if len(encoded) == encoded_len:
            return encoded
    raise ValueError("AML package payload is too large")


def aml_name_seg(name):
    assert len(name) == 4
    return [ord(c) for c in name]


def aml_integer(value):
    if value == 0:
        return [0x00]
    if value == 1:
        return [0x01]
    if value <= 0xFF:
        return [0x0A, value & 0xFF]
    if value <= 0xFFFF:
        return [0x0B, value & 0xFF, (value >> 8) & 0xFF]
    if value <= 0xFFFFFFFF:
        return [0x0C] + [(value >> (8 * i)) & 0xFF for i in range(4)]
    return [0x0E] + [(value >> (8 * i)) & 0xFF for i in range(8)]


def aml_string(value):
    return [0x0D] + [ord(c) for c in value] + [0x00]


def aml_name(name, value):
    return [0x08] + aml_name_seg(name) + value


def aml_device(name, body):
    payload = aml_name_seg(name) + body
    return [0x5B, 0x82] + aml_pkg_len_for_payload(len(payload)) + payload


def aml_buffer(data):
    size = aml_integer(len(data))
    return (
        [0x11] + aml_pkg_len_for_payload(len(size) + len(data)) + size + data
    )


def aml_package(elements):
    body = [len(elements) & 0xFF]
    for element in elements:
        body += element
    return [0x12] + aml_pkg_len_for_payload(len(body)) + body


def aml_prt_entry(device, pin, gsi):
    return aml_package(
        [
            aml_integer((device << 16) | 0xFFFF),
            aml_integer(pin),
            aml_integer(0),
            aml_integer(gsi),
        ]
    )


def aml_pci_irq_routing_table():
    # Leave the legacy IDE controller (dev 4) out of _PRT. Linux keeps legacy
    # IDE on IRQ14/15 when no device-specific PCI routing entry exists.
    return aml_package(
        [
            aml_prt_entry(0, 0, 16),
            aml_prt_entry(0, 1, 17),
            aml_prt_entry(0, 2, 18),
            aml_prt_entry(0, 3, 19),
        ]
    )


def acpi_word_bus_number(min_bus, max_bus):
    length = max_bus - min_bus + 1
    body = [
        0x02,
        0x0C,
        0x00,
        0x00,
        0x00,
        min_bus & 0xFF,
        (min_bus >> 8) & 0xFF,
        max_bus & 0xFF,
        (max_bus >> 8) & 0xFF,
        0x00,
        0x00,
        length & 0xFF,
        (length >> 8) & 0xFF,
    ]
    return [0x88, len(body) & 0xFF, (len(body) >> 8) & 0xFF] + body


def acpi_dword_memory(min_addr, max_addr):
    length = max_addr - min_addr + 1
    dwords = [0, min_addr, max_addr, 0, length]
    body = [0x00, 0x0C, 0x03]
    for value in dwords:
        body += [(value >> (8 * i)) & 0xFF for i in range(4)]
    return [0x87, len(body) & 0xFF, (len(body) >> 8) & 0xFF] + body


def acpi_dword_io(min_addr, max_addr):
    length = max_addr - min_addr + 1
    dwords = [0, min_addr, max_addr, 0, length]
    body = [0x01, 0x0C, 0x03]
    for value in dwords:
        body += [(value >> (8 * i)) & 0xFF for i in range(4)]
    return [0x87, len(body) & 0xFF, (len(body) >> 8) & 0xFF] + body


def acpi_end_tag():
    return [0x79, 0x00]


def make_cxl_root_aml():
    cxl_root = aml_device("CXLM", aml_name("_HID", aml_string("ACPI0017")))
    cxl_host_crs = (
        acpi_word_bus_number(0, 255)
        + acpi_dword_io(0x0000, 0xFFFF)
        + acpi_dword_memory(0xC0000000, 0xDFFFFFFF)
        + acpi_end_tag()
    )
    cxl_host = aml_device(
        "CXLH",
        aml_name("_HID", aml_string("ACPI0016"))
        + aml_name(
            "_CID",
            aml_package(
                [
                    aml_string("PNP0A03"),
                    aml_string("PNP0A08"),
                ]
            ),
        )
        + aml_name("_UID", aml_integer(0))
        + aml_name("_SEG", aml_integer(0))
        + aml_name("_BBN", aml_integer(0))
        + aml_name("_PRT", aml_pci_irq_routing_table())
        + aml_name("_CRS", aml_buffer(cxl_host_crs)),
    )
    return cxl_root + cxl_host


def make_cxl_srat(num_cpus, dram_ranges, cxl_base, cxl_size):
    srat_enabled = 0x1
    srat_hotpluggable = 0x2
    records = []

    for cpu_id in range(num_cpus):
        records.append(
            X86ACPISratX2Apic(
                proximity_domain=0,
                apic_id=cpu_id,
                flags=srat_enabled,
            )
        )

    for dram_range in dram_ranges:
        records.append(
            X86ACPISratMemory(
                proximity_domain=0,
                base_address=int(dram_range.start),
                length=int(dram_range.size()),
                flags=srat_enabled,
            )
        )

    records.append(
        X86ACPISratMemory(
            proximity_domain=1,
            base_address=int(cxl_base),
            length=int(cxl_size),
            flags=srat_enabled | srat_hotpluggable,
        )
    )

    return X86ACPISrat(
        oem_id="gem5",
        oem_table_id="CXLSRAT",
        oem_revision=1,
        records=records,
    )


def add_cxl_firmware_tables(
    workload,
    cxl_base,
    cxl_size,
    num_cpus=1,
    dram_ranges=None,
    cxl_chbs_base=CXL_CHBS_BASE,
    cxl_chbs_size=CXL_CHBS_SIZE,
):
    assert int(cxl_base) % CXL_FW_ALIGN == 0
    assert int(cxl_size) % CXL_FW_ALIGN == 0
    if dram_ranges is None:
        dram_ranges = []

    dsdt = X86ACPIDsdtRaw(
        oem_id="gem5",
        oem_table_id="GEM5DSDT",
        oem_revision=1,
        body=[],
    )
    fadt = X86ACPIFadt(
        oem_id="gem5",
        oem_table_id="GEM5FADT",
        oem_revision=1,
        dsdt=dsdt,
        pm1a_event_block=CXL_FADT_PM1A_EVT_BLK,
        pm1a_control_block=CXL_FADT_PM1A_CNT_BLK,
        pm1_event_length=CXL_FADT_PM1_EVT_LEN,
        pm1_control_length=CXL_FADT_PM1_CNT_LEN,
    )
    cedt = X86ACPICedt(
        oem_id="gem5",
        oem_table_id="CXLCEDT",
        records=[
            X86ACPICedtChbs(
                uid=0,
                cxl_version=1,
                base=int(cxl_chbs_base),
                length=int(cxl_chbs_size),
            ),
            X86ACPICedtCfmws(
                base_hpa=int(cxl_base),
                window_size=int(cxl_size),
                restrictions=0x6,
                qtg_id=0,
                interleave_ways=0,
                interleave_arithmetic=0,
                granularity=0,
                targets=[0],
            ),
        ],
    )
    ssdt = X86ACPISsdtRaw(
        oem_id="gem5",
        oem_table_id="CXLFWS",
        oem_revision=1,
        body=make_cxl_root_aml(),
    )
    mcfg = X86ACPIMcfg(
        oem_id="gem5",
        oem_table_id="GEM5MCFG",
        oem_revision=1,
        base_address=CXL_MCFG_BASE,
        segment=0,
        start_bus=0,
        end_bus=255,
    )
    srat = make_cxl_srat(num_cpus, dram_ranges, cxl_base, cxl_size)
    workload.acpi_description_table_pointer.rsdt.entries.append(fadt)
    workload.acpi_description_table_pointer.xsdt.entries.append(fadt)
    workload.acpi_description_table_pointer.rsdt.entries.append(mcfg)
    workload.acpi_description_table_pointer.xsdt.entries.append(mcfg)
    workload.acpi_description_table_pointer.rsdt.entries.append(srat)
    workload.acpi_description_table_pointer.xsdt.entries.append(srat)
    workload.acpi_description_table_pointer.rsdt.entries.append(cedt)
    workload.acpi_description_table_pointer.xsdt.entries.append(cedt)
    workload.acpi_description_table_pointer.rsdt.entries.append(ssdt)
    workload.acpi_description_table_pointer.xsdt.entries.append(ssdt)


def cxl_region_check(cxl_type3_size, pcie_devices):
    for i, device in enumerate(pcie_devices):
        if device.type == "CxlMemory" or device.type == "CXLDRAMsim3":
            cxl_type3_size.append(int(device.BAR0.size))
            print("esj bar size", int(device.BAR0.size))


def makeX86System(
    mem_mode,
    numCPUs=1,
    mdesc=None,
    workload=None,
    Ruby=False,
    cxl_type3_size=[],
    pcie_devices=[],
):
    self = System()

    self.m5ops_base = 0xFFFF0000

    if workload is None:
        workload = X86FsWorkload()
    self.workload = workload
    reset_x86_acpi_tables(workload)

    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()

    self.mem_mode = mem_mode

    if mdesc.NUMA_mode():
        ObjectList.numa_mode = True
    else:
        ObjectList.numa_mode = False

    if mdesc.CXL_mode():
        ObjectList.cxl_mode = True
    else:
        ObjectList.cxl_mode = False
    print("cxl mode = ", ObjectList.cxl_mode)

    # esj 2026-02-11
    if mdesc.Routing_mode_type() != "default":
        ObjectList.routing_mode_type = mdesc.Routing_mode_type()
        print("routing mode type = ", ObjectList.routing_mode_type)

    if mdesc.Priority_limit() != 10:
        ObjectList.priority_limit = mdesc.Priority_limit()
        print("priority limit = ", ObjectList.priority_limit)

    if mdesc.Priority_host_ratios() != "10,0,0,0":
        ObjectList.priority_host_ratios = mdesc.Priority_host_ratios()
        print("priority host ratios = ", ObjectList.priority_host_ratios)

    # Physical memory
    # On the PC platform, the memory region 0xC0000000-0xFFFFFFFF is reserved
    # for various devices.  Hence, if the physical memory size is greater than
    # 3GB, we need to split it into two parts.
    excess_mem_size = convert.toMemorySize(mdesc.mem()) - convert.toMemorySize(
        "3GB"
    )
    print("excess_mem_size : ", excess_mem_size)

    if excess_mem_size <= 0:
        self.mem_ranges = [AddrRange(mdesc.mem())]
    else:
        warn(
            "Physical memory size specified is %s which is greater than "
            "3GB.  Twice the number of memory controllers would be "
            "created." % (mdesc.mem())
        )

        self.mem_ranges = [
            AddrRange("3GB"),
            AddrRange(Addr("4GB"), size=excess_mem_size),
        ]
    dram_mem_ranges = list(self.mem_ranges)

    # Platform
    self.pc = Pc()
    self.pc.pci_host.ecam_base = CXL_MCFG_BASE
    self.pc.pci_host.ecam_size = CXL_MCFG_SIZE

    # Create and connect the busses required by each memory system
    if Ruby:
        connectX86RubySystem(self)
    else:
        connectX86ClassicSystem(self, numCPUs)

    # cxl memory

    cxl_size, cxl_total_size, cxl_device_offset = cxl_partition_values(mdesc)
    mem_size = convert.toMemorySize(mdesc.mem())
    # esj 2025-05-11
    first_cxl_size = max(0, cxl_total_size - cxl_size)
    cxl_start_addr = mdesc.CXL_start_addr()
    configured_cxl_start = (
        Addr(cxl_start_addr) if cxl_start_addr is not None else None
    )
    print("first_cxl_size", first_cxl_size)
    print("cxl_size", cxl_size)
    if not hasattr(ObjectList, "cxl_mem_as_system_ram"):
        ObjectList.cxl_mem_as_system_ram = False
    # connectX86CXLMemory(self, pcie_devices, cxl_size, Ruby)
    if ObjectList.cxl_mode:
        self.pc.south_bridge.ide.InterruptPin = 0
        assert cxl_size % CXL_FW_ALIGN == 0
        assert cxl_total_size % CXL_FW_ALIGN == 0
        assert cxl_device_offset % CXL_FW_ALIGN == 0
        ObjectList.cxl_mem_start = cxl_firmware_base(self.mem_ranges)
        if configured_cxl_start is not None:
            ObjectList.cxl_mem_start = configured_cxl_start
        assert int(ObjectList.cxl_mem_start) % CXL_FW_ALIGN == 0
        ObjectList.cxl_mem_size = cxl_size
        ObjectList.cxl_total_mem_size = cxl_total_size
        ObjectList.cxl_device_offset = cxl_device_offset
        ObjectList.mem_size = mem_size
        connectX86CXLMemory2(
            self, pcie_devices, cxl_size, Ruby, cxl_total_size
        )
    # connectX86CXLMemory_non_switch(self, pcie_devices, cxl_size, Ruby)

    # cxl_region_check(cxl_type3_size, pcie_devices)

    # if(len(pcie_devices)!=0):
    #     if(len(self.mem_ranges)==2):
    #         ObjectList.simplememory_iter_start = 3
    #         ObjectList.cxl_mem_start = self.mem_ranges[0].size()+self.mem_ranges[1].size()+excess_mem_size
    #         ObjectList.cxl_mem_size = cxl_size

    #         self.mem_ranges.append(AddrRange(self.mem_ranges[0].size()+self.mem_ranges[1].size()+excess_mem_size, size=cxl_size) )
    #     else:
    #         ObjectList.simplememory_iter_start = 2
    #         ObjectList.cxl_mem_start = Addr(mdesc.mem())
    #         ObjectList.cxl_mem_size = cxl_size

    #         excess_mem_size = convert.toMemorySize(mdesc.mem()) + cxl_size - convert.toMemorySize("3GB")
    #         if(excess_mem_size>0):
    #             self.mem_ranges.append(AddrRange(Addr(mdesc.mem()), size=convert.toMemorySize(mdesc.mem())-convert.toMemorySize("3GB")))
    #             self.mem_ranges.append(AddrRange(Addr("4GB"), size=excess_mem_size))
    #         else:
    #             self.mem_ranges.append(AddrRange(Addr(mdesc.mem()), size=cxl_size))
    #     if Ruby:
    #         self.iobus.cxl_mem_start = ObjectList.cxl_mem_start
    #         self.iobus.cxl_mem_size = ObjectList.cxl_mem_size
    #     else:
    #         self.membus.cxl_mem_start = ObjectList.cxl_mem_start
    #         self.membus.cxl_mem_size = ObjectList.cxl_mem_size

    # esj 2024-10-08
    if ObjectList.cxl_mode and ObjectList.cxl_mem_as_system_ram:
        append_cxl_system_ram_ranges(self, cxl_size, pcie_devices)
        ObjectList.mem_size = mem_size
    elif ObjectList.numa_mode:
        if len(self.mem_ranges) == 2:
            ObjectList.simplememory_iter_start = 3
            if configured_cxl_start is not None:
                ObjectList.cxl_mem_start = configured_cxl_start
            else:
                ObjectList.cxl_mem_start = (
                    self.mem_ranges[0].size()
                    + self.mem_ranges[1].size()
                    + excess_mem_size
                )
            ObjectList.cxl_mem_size = cxl_size

            self.mem_ranges.append(
                AddrRange(
                    ObjectList.cxl_mem_start,
                    size=cxl_size,
                )
            )
        else:
            ObjectList.simplememory_iter_start = 2
            ObjectList.cxl_mem_start = (
                configured_cxl_start
                if configured_cxl_start is not None
                else Addr(mdesc.mem())
            )
            ObjectList.cxl_mem_size = cxl_size

            excess_mem_size = (
                convert.toMemorySize(mdesc.mem())
                + cxl_size
                - convert.toMemorySize("3GB")
            )
            if excess_mem_size > 0:
                if (
                    convert.toMemorySize(mdesc.mem())
                    - convert.toMemorySize("3GB")
                    == 0
                ):
                    cxl_range_start = (
                        configured_cxl_start
                        if configured_cxl_start is not None
                        else Addr("4GB") + first_cxl_size
                    )
                    # esj 2025-05-11
                    self.mem_ranges.append(
                        AddrRange(cxl_range_start, size=excess_mem_size)
                    )
                    ObjectList.cxl_mem_start = cxl_range_start
                else:
                    self.mem_ranges.append(
                        AddrRange(
                            Addr(mdesc.mem()),
                            size=convert.toMemorySize(mdesc.mem())
                            - convert.toMemorySize("3GB"),
                        )
                    )
                    self.mem_ranges.append(
                        AddrRange(Addr("4GB"), size=excess_mem_size)
                    )
            else:
                self.mem_ranges.append(
                    AddrRange(ObjectList.cxl_mem_start, size=cxl_size)
                )
        # if Ruby:
        #     self.iobus.cxl_mem_start = ObjectList.cxl_mem_start
        #     self.iobus.cxl_mem_size = ObjectList.cxl_mem_size
        # else:
        #     self.membus.cxl_mem_start = ObjectList.cxl_mem_start
        #     self.membus.cxl_mem_size = ObjectList.cxl_mem_size

    set_cxl_fetch_window(self)

    # Disks
    disks = makeCowDisks(mdesc.disks())

    # if hasattr(self.pc, "ide"):
    #     self.pc.ide.int_primary = self.pc.south_bridge.pic2.inputs[6]
    #     self.pc.ide.int_primary = self.pc.south_bridge.io_apic.inputs[14]
    #     self.pc.ide.int_secondary = self.pc.south_bridge.pic2.inputs[7]
    #     self.pc.ide.int_secondary = self.pc.south_bridge.io_apic.inputs[15]
    #     self.pc.ide.disks = disks
    # else:
    #     self.pc.south_bridge.ide.disks = disks
    self.pc.south_bridge.ide.disks = disks
    # Add in a Bios information structure.
    structures = [X86SMBiosBiosInformation()]
    workload.smbios_table.structures = structures

    # Set up the Intel MP table
    base_entries = []
    ext_entries = []
    madt_records = []
    for i in range(numCPUs):
        bp = X86IntelMPProcessor(
            local_apic_id=i,
            local_apic_version=0x14,
            enable=True,
            bootstrap=(i == 0),
        )
        base_entries.append(bp)
        lapic = X86ACPIMadtLAPIC(acpi_processor_id=i, apic_id=i, flags=1)
        madt_records.append(lapic)
    io_apic = X86IntelMPIOAPIC(
        id=numCPUs, version=0x11, enable=True, address=0xFEC00000
    )
    self.pc.south_bridge.io_apic.apic_id = io_apic.id
    base_entries.append(io_apic)
    madt_records.append(
        X86ACPIMadtIOAPIC(id=io_apic.id, address=io_apic.address, int_base=0)
    )
    # In gem5 Pc::calcPciConfigAddr(), it required "assert(bus==0)",
    # but linux kernel cannot config PCI device if it was not connected to
    # PCI bus, so we fix PCI bus id to 0, and ISA bus id to 1.
    pci_bus = X86IntelMPBus(bus_id=0, bus_type="PCI   ")
    base_entries.append(pci_bus)
    isa_bus = X86IntelMPBus(bus_id=1, bus_type="ISA   ")
    base_entries.append(isa_bus)
    connect_busses = X86IntelMPBusHierarchy(
        bus_id=1, subtractive_decode=True, parent_bus=0
    )
    ext_entries.append(connect_busses)

    pci_dev4_inta = X86IntelMPIOIntAssignment(
        interrupt_type="INT",
        polarity="ConformPolarity",
        trigger="ConformTrigger",
        source_bus_id=0,
        source_bus_irq=0 + (4 << 2),
        dest_io_apic_id=io_apic.id,
        dest_io_apic_intin=16,
    )
    base_entries.append(pci_dev4_inta)
    pci_dev4_inta_madt = X86ACPIMadtIntSourceOverride(
        bus_source=pci_dev4_inta.source_bus_id,
        irq_source=pci_dev4_inta.source_bus_irq,
        sys_int=pci_dev4_inta.dest_io_apic_intin,
        flags=0,
    )
    madt_records.append(pci_dev4_inta_madt)

    def assignISAInt(irq, apicPin):
        assign_8259_to_apic = X86IntelMPIOIntAssignment(
            interrupt_type="ExtInt",
            polarity="ConformPolarity",
            trigger="ConformTrigger",
            source_bus_id=1,
            source_bus_irq=irq,
            dest_io_apic_id=io_apic.id,
            dest_io_apic_intin=0,
        )
        base_entries.append(assign_8259_to_apic)
        assign_to_apic = X86IntelMPIOIntAssignment(
            interrupt_type="INT",
            polarity="ConformPolarity",
            trigger="ConformTrigger",
            source_bus_id=1,
            source_bus_irq=irq,
            dest_io_apic_id=io_apic.id,
            dest_io_apic_intin=apicPin,
        )
        base_entries.append(assign_to_apic)
        # acpi
        assign_to_apic_acpi = X86ACPIMadtIntSourceOverride(
            bus_source=0, irq_source=irq, sys_int=apicPin, flags=0
        )
        madt_records.append(assign_to_apic_acpi)

    assignISAInt(0, 2)
    assignISAInt(1, 1)
    for i in range(3, 16):
        assignISAInt(i, i)
    workload.intel_mp_table.base_entries = base_entries
    workload.intel_mp_table.ext_entries = ext_entries

    madt = X86ACPIMadt(
        local_apic_address=0, records=madt_records, oem_id="madt"
    )
    workload.acpi_description_table_pointer.rsdt.entries.append(madt)
    workload.acpi_description_table_pointer.xsdt.entries.append(madt)
    if ObjectList.cxl_mode:
        add_cxl_firmware_tables(
            workload,
            ObjectList.cxl_mem_start,
            ObjectList.cxl_mem_size,
            num_cpus=numCPUs,
            dram_ranges=dram_mem_ranges,
        )
    workload.acpi_description_table_pointer.oem_id = "gem5"
    workload.acpi_description_table_pointer.rsdt.oem_id = "gem5"
    workload.acpi_description_table_pointer.xsdt.oem_id = "gem5"

    return self


def makeX86System_second(
    mem_mode,
    numCPUs=1,
    mdesc=None,
    workload=None,
    Ruby=False,
    cxl_type3_size=[],
    pcie_devices=[],
):
    self = System()

    self.m5ops_base = 0xFFFF0000

    if workload is None:
        workload = X86FsWorkload()
    self.workload = workload
    reset_x86_acpi_tables(workload)

    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()

    self.mem_mode = mem_mode

    if mdesc.NUMA_mode():
        ObjectList.numa_mode = True
    else:
        ObjectList.numa_mode = False

    if mdesc.CXL_mode():
        ObjectList.cxl_mode = True
    else:
        ObjectList.cxl_mode = False
    print("cxl mode = ", ObjectList.cxl_mode)

    # Physical memory
    # On the PC platform, the memory region 0xC0000000-0xFFFFFFFF is reserved
    # for various devices.  Hence, if the physical memory size is greater than
    # 3GB, we need to split it into two parts.
    excess_mem_size = convert.toMemorySize(mdesc.mem()) - convert.toMemorySize(
        "3GB"
    )
    print("excess_mem_size : ", excess_mem_size)

    if excess_mem_size <= 0:
        self.mem_ranges = [AddrRange(mdesc.mem())]
    else:
        warn(
            "Physical memory size specified is %s which is greater than "
            "3GB.  Twice the number of memory controllers would be "
            "created." % (mdesc.mem())
        )

        self.mem_ranges = [
            AddrRange("3GB"),
            AddrRange(Addr("4GB"), size=excess_mem_size),
        ]
    dram_mem_ranges = list(self.mem_ranges)

    # Platform
    self.pc = Pc()
    self.pc.pci_host.ecam_base = CXL_MCFG_BASE
    self.pc.pci_host.ecam_size = CXL_MCFG_SIZE

    # Create and connect the busses required by each memory system
    if Ruby:
        connectX86RubySystem(self)
    else:
        connectX86ClassicSystem(self, numCPUs)

    # cxl memory
    # cxl_size = convert.toMemorySize(mdesc.cxlmem())
    cxl_size, cxl_total_size, cxl_device_offset = cxl_partition_values(mdesc)
    cxl_start_addr = mdesc.CXL_start_addr()
    configured_cxl_start = (
        Addr(cxl_start_addr) if cxl_start_addr is not None else None
    )
    print("cxl_size", cxl_size)
    # connectX86CXLMemory(self, pcie_devices, cxl_size, Ruby)
    if not hasattr(ObjectList, "cxl_mem_as_system_ram"):
        ObjectList.cxl_mem_as_system_ram = False
    if ObjectList.cxl_mode:
        self.pc.south_bridge.ide.InterruptPin = 0
        assert cxl_size % CXL_FW_ALIGN == 0
        assert cxl_total_size % CXL_FW_ALIGN == 0
        assert cxl_device_offset % CXL_FW_ALIGN == 0
        ObjectList.cxl_mem_start = cxl_firmware_base(self.mem_ranges)
        if configured_cxl_start is not None:
            ObjectList.cxl_mem_start = configured_cxl_start
        assert int(ObjectList.cxl_mem_start) % CXL_FW_ALIGN == 0
        ObjectList.cxl_mem_size = cxl_size
        ObjectList.cxl_total_mem_size = cxl_total_size
        ObjectList.cxl_device_offset = cxl_device_offset

    # esj 2024-10-08
    if ObjectList.cxl_mode and ObjectList.cxl_mem_as_system_ram:
        append_cxl_system_ram_ranges(self, cxl_size, pcie_devices)
    elif ObjectList.numa_mode:
        if len(self.mem_ranges) == 2:
            ObjectList.simplememory_iter_start = 3
            if configured_cxl_start is not None:
                ObjectList.cxl_mem_start = configured_cxl_start
            else:
                ObjectList.cxl_mem_start = (
                    self.mem_ranges[0].size()
                    + self.mem_ranges[1].size()
                    + excess_mem_size
                )
            ObjectList.cxl_mem_size = cxl_size

            self.mem_ranges.append(
                AddrRange(
                    ObjectList.cxl_mem_start,
                    size=cxl_size,
                )
            )
        else:
            ObjectList.simplememory_iter_start = 2
            ObjectList.cxl_mem_start = (
                configured_cxl_start
                if configured_cxl_start is not None
                else Addr(mdesc.mem())
            )
            ObjectList.cxl_mem_size = cxl_size

            excess_mem_size = (
                convert.toMemorySize(mdesc.mem())
                + cxl_size
                - convert.toMemorySize("3GB")
            )
            if excess_mem_size > 0:
                if (
                    convert.toMemorySize(mdesc.mem())
                    - convert.toMemorySize("3GB")
                    == 0
                ):
                    cxl_range_start = (
                        configured_cxl_start
                        if configured_cxl_start is not None
                        else Addr("4GB")
                    )
                    self.mem_ranges.append(
                        AddrRange(cxl_range_start, size=excess_mem_size)
                    )
                    ObjectList.cxl_mem_start = cxl_range_start
                else:
                    self.mem_ranges.append(
                        AddrRange(
                            Addr(mdesc.mem()),
                            size=convert.toMemorySize(mdesc.mem())
                            - convert.toMemorySize("3GB"),
                        )
                    )
                    self.mem_ranges.append(
                        AddrRange(Addr("4GB"), size=excess_mem_size)
                    )
            else:
                self.mem_ranges.append(
                    AddrRange(ObjectList.cxl_mem_start, size=cxl_size)
                )
        # if Ruby:
        #     self.iobus.cxl_mem_start = ObjectList.cxl_mem_start
        #     self.iobus.cxl_mem_size = ObjectList.cxl_mem_size
        # else:
        #     self.membus.cxl_mem_start = ObjectList.cxl_mem_start
        #     self.membus.cxl_mem_size = ObjectList.cxl_mem_size

    set_cxl_fetch_window(self)

    # Disks
    disks = makeCowDisks(mdesc.disks())

    self.pc.south_bridge.ide.disks = disks
    # Add in a Bios information structure.
    structures = [X86SMBiosBiosInformation()]
    workload.smbios_table.structures = structures

    # Set up the Intel MP table
    base_entries = []
    ext_entries = []
    madt_records = []
    for i in range(numCPUs):
        bp = X86IntelMPProcessor(
            local_apic_id=i,
            local_apic_version=0x14,
            enable=True,
            bootstrap=(i == 0),
        )
        base_entries.append(bp)
        lapic = X86ACPIMadtLAPIC(acpi_processor_id=i, apic_id=i, flags=1)
        madt_records.append(lapic)
    io_apic = X86IntelMPIOAPIC(
        id=numCPUs, version=0x11, enable=True, address=0xFEC00000
    )
    self.pc.south_bridge.io_apic.apic_id = io_apic.id
    base_entries.append(io_apic)
    madt_records.append(
        X86ACPIMadtIOAPIC(id=io_apic.id, address=io_apic.address, int_base=0)
    )
    # In gem5 Pc::calcPciConfigAddr(), it required "assert(bus==0)",
    # but linux kernel cannot config PCI device if it was not connected to
    # PCI bus, so we fix PCI bus id to 0, and ISA bus id to 1.
    pci_bus = X86IntelMPBus(bus_id=0, bus_type="PCI   ")
    base_entries.append(pci_bus)
    isa_bus = X86IntelMPBus(bus_id=1, bus_type="ISA   ")
    base_entries.append(isa_bus)
    connect_busses = X86IntelMPBusHierarchy(
        bus_id=1, subtractive_decode=True, parent_bus=0
    )
    ext_entries.append(connect_busses)

    pci_dev4_inta = X86IntelMPIOIntAssignment(
        interrupt_type="INT",
        polarity="ConformPolarity",
        trigger="ConformTrigger",
        source_bus_id=0,
        source_bus_irq=0 + (4 << 2),
        dest_io_apic_id=io_apic.id,
        dest_io_apic_intin=16,
    )
    base_entries.append(pci_dev4_inta)
    pci_dev4_inta_madt = X86ACPIMadtIntSourceOverride(
        bus_source=pci_dev4_inta.source_bus_id,
        irq_source=pci_dev4_inta.source_bus_irq,
        sys_int=pci_dev4_inta.dest_io_apic_intin,
        flags=0,
    )
    madt_records.append(pci_dev4_inta_madt)

    def assignISAInt(irq, apicPin):
        assign_8259_to_apic = X86IntelMPIOIntAssignment(
            interrupt_type="ExtInt",
            polarity="ConformPolarity",
            trigger="ConformTrigger",
            source_bus_id=1,
            source_bus_irq=irq,
            dest_io_apic_id=io_apic.id,
            dest_io_apic_intin=0,
        )
        base_entries.append(assign_8259_to_apic)
        assign_to_apic = X86IntelMPIOIntAssignment(
            interrupt_type="INT",
            polarity="ConformPolarity",
            trigger="ConformTrigger",
            source_bus_id=1,
            source_bus_irq=irq,
            dest_io_apic_id=io_apic.id,
            dest_io_apic_intin=apicPin,
        )
        base_entries.append(assign_to_apic)
        # acpi
        assign_to_apic_acpi = X86ACPIMadtIntSourceOverride(
            bus_source=0, irq_source=irq, sys_int=apicPin, flags=0
        )
        madt_records.append(assign_to_apic_acpi)

    assignISAInt(0, 2)
    assignISAInt(1, 1)
    for i in range(3, 16):
        assignISAInt(i, i)
    workload.intel_mp_table.base_entries = base_entries
    workload.intel_mp_table.ext_entries = ext_entries

    madt = X86ACPIMadt(
        local_apic_address=0, records=madt_records, oem_id="madt"
    )
    workload.acpi_description_table_pointer.rsdt.entries.append(madt)
    workload.acpi_description_table_pointer.xsdt.entries.append(madt)
    if ObjectList.cxl_mode:
        add_cxl_firmware_tables(
            workload,
            ObjectList.cxl_mem_start,
            ObjectList.cxl_mem_size,
            num_cpus=numCPUs,
            dram_ranges=dram_mem_ranges,
        )
    workload.acpi_description_table_pointer.oem_id = "gem5"
    workload.acpi_description_table_pointer.rsdt.oem_id = "gem5"
    workload.acpi_description_table_pointer.xsdt.oem_id = "gem5"

    return self


# esj 2025-04-14
# def makeLinuxX86System(
#     mem_mode, numCPUs=1, mdesc=None, Ruby=False, cmdline=None
# ):
def makeLinuxX86System(
    mem_mode, numCPUs=1, mdesc=None, Ruby=False, cmdline=None, is_second=False
):
    # cxl region
    start_cxl_mem_addr = 0x200000000
    cxl_type3_size = []
    pcie_devices = []
    # Build up the x86 system and then specialize it for Linux

    # esj 2025-04-14
    if not is_second:
        self = makeX86System(
            mem_mode,
            numCPUs,
            mdesc,
            X86FsLinux(),
            Ruby,
            cxl_type3_size,
            pcie_devices,
        )
    else:
        self = makeX86System_second(
            mem_mode,
            numCPUs,
            mdesc,
            X86FsLinux(),
            Ruby,
            cxl_type3_size,
            pcie_devices,
        )

    # We assume below that there's at least 1MB of memory. We'll require 2
    # just to avoid corner cases.
    phys_mem_size = sum([r.size() for r in self.mem_ranges])
    assert phys_mem_size >= 0x200000
    # assert len(self.mem_ranges) <= 2
    print(
        "DEXTER @@@@@@@@@@@@@ the number of mem_ranges is ",
        len(self.mem_ranges),
    )
    print("DEXTER @@@@@@@@@@@@@ pcie devices number is ", len(pcie_devices))

    for i in range(len(self.mem_ranges)):
        print("mem range", i, " is ", self.mem_ranges[i])

    # esj 2024-10-08
    # if(len(pcie_devices)==0):
    cxl_as_system_ram = getattr(ObjectList, "cxl_mem_as_system_ram", False)
    if (len(pcie_devices) == 0 and ObjectList.numa_mode == False) or (
        ObjectList.cxl_mode and not cxl_as_system_ram
    ):
        entries = [
            # Mark the first megabyte of memory as reserved
            X86E820Entry(addr=0, size="639kB", range_type=1),
            X86E820Entry(addr=0x9FC00, size="385kB", range_type=2),
            # Mark the rest of physical memory as available
            X86E820Entry(
                addr=0x100000,
                size="%dB" % (self.mem_ranges[0].size() - 0x100000),
                range_type=1,
            ),
        ]

        # Mark [mem_size, 3GB) as reserved if memory less than 3GB, which force
        # IO devices to be mapped to [0xC0000000, 0xFFFF0000). Requests to this
        # specific range can pass though bridge to iobus.
        if len(self.mem_ranges) == 1:
            # print("esj mem_range = ",self.mem_ranges[0].size())
            entries.append(
                X86E820Entry(
                    addr=self.mem_ranges[0].size(),
                    size="%dB" % (0xC0000000 - self.mem_ranges[0].size()),
                    range_type=2,
                )
            )
        # print("esj mem_range len= ",len(self.mem_ranges))

        # Reserve the last 16kB of the 32-bit address space for the m5op interface
        entries.append(
            X86E820Entry(
                addr=CXL_MCFG_BASE,
                size="%dB" % CXL_MCFG_SIZE,
                range_type=2,
            )
        )
        entries.append(
            X86E820Entry(addr=0xFFFF0000, size="64kB", range_type=2)
        )

        # In case the physical memory is greater than 3GB, we split it into two
        # parts and add a separate e820 entry for the second part.  This entry
        # starts at 0x100000000,  which is the first address after the space
        # reserved for devices.

        # esj 2025-04-19
        #########################################
        # if len(self.mem_ranges) == 2:
        #     entries.append(
        #         X86E820Entry(
        #             addr=0x100000000,
        #             size="%dB" % (self.mem_ranges[1].size()),
        #             range_type=1,
        #         )
        #     )
        if len(self.mem_ranges) == 2:
            if self.mem_ranges[0].size() == 0xC0000000:
                entries.append(
                    X86E820Entry(
                        addr=self.mem_ranges[1].start,
                        size="%dB" % (self.mem_ranges[1].size()),
                        range_type=1,
                    )
                )
            else:
                excess_mem_size = (
                    self.mem_ranges[0].size()
                    + self.mem_ranges[1].size()
                    - 0xC0000000
                )
                if excess_mem_size > 0:
                    entries.append(
                        X86E820Entry(
                            addr=self.mem_ranges[0].size(),
                            size="%dB"
                            % (0xC0000000 - self.mem_ranges[0].size()),
                            range_type=1,
                        )
                    )
                    entries.append(
                        X86E820Entry(
                            addr=0x100000000,
                            size="%dB" % (excess_mem_size),
                            range_type=1,
                        )
                    )
                else:
                    entries.append(
                        X86E820Entry(
                            addr=self.mem_ranges[0].size(),
                            size="%dB" % (self.mem_ranges[1].size()),
                            range_type=1,
                        )
                    )
                    entries.append(
                        X86E820Entry(
                            addr=self.mem_ranges[0].size()
                            + self.mem_ranges[1].size(),
                            size="%dB"
                            % (
                                0xC0000000
                                - self.mem_ranges[0].size()
                                - self.mem_ranges[1].size()
                            ),
                            range_type=2,
                        )
                    )
        elif len(self.mem_ranges) == 3:
            entries.append(
                X86E820Entry(
                    addr=self.mem_ranges[1].start,
                    size="%dB" % (self.mem_ranges[1].size()),
                    range_type=1,
                )
            )
            entries.append(
                X86E820Entry(
                    addr=self.mem_ranges[2].start,
                    size="%dB" % (self.mem_ranges[2].size()),
                    range_type=1,
                )
            )
    else:  ##esj
        entries = [
            # Mark the first megabyte of memory as reserved
            X86E820Entry(addr=0, size="639kB", range_type=1),
            X86E820Entry(addr=0x9FC00, size="385kB", range_type=2),
            # Mark the rest of physical memory as available
            X86E820Entry(
                addr=0x100000,
                size="%dB" % (self.mem_ranges[0].size() - 0x100000),
                range_type=1,
            ),
        ]

        # Mark [mem_size, 3GB) as reserved if memory less than 3GB, which force
        # IO devices to be mapped to [0xC0000000, 0xFFFF0000). Requests to this
        # specific range can pass though bridge to iobus.
        if len(self.mem_ranges) == 3:
            # print("esj mem_range = ",self.mem_ranges[0].size())
            entries.append(
                X86E820Entry(
                    addr=0x100000000,
                    size="%dB"
                    % (self.mem_ranges[1].size() + self.mem_ranges[2].size()),
                    range_type=1,
                )
            )
        else:
            if (
                0xC0000000
                - (self.mem_ranges[0].size() + self.mem_ranges[1].size())
            ) > 0:
                entries.append(
                    X86E820Entry(
                        addr=self.mem_ranges[0].size(),
                        # size="%dB" % (self.mem_ranges[1].size()),
                        size="%dB" % (self.mem_ranges[1].size()),
                        range_type=1,
                    )
                )
                entries.append(
                    X86E820Entry(
                        addr=self.mem_ranges[0].size()
                        + self.mem_ranges[1].size(),
                        # size="%dB" % (self.mem_ranges[1].size()),
                        size="%dB"
                        % (
                            0xC0000000
                            - self.mem_ranges[0].size()
                            - self.mem_ranges[1].size()
                        ),
                        range_type=2,
                    )
                )
            else:
                # entries.append(
                #     X86E820Entry(
                #         addr=self.mem_ranges[0].size(),
                #         size="%dB" % (0xC0000000 - self.mem_ranges[0].size()),
                #         range_type=1,
                #     )
                # )
                # entries.append(
                #     X86E820Entry(
                #         addr=0x100000000,
                #         size="%dB" % (self.mem_ranges[1].size() - (0xC0000000 - self.mem_ranges[0].size())),
                #         range_type=1,
                #     )
                # )
                if self.mem_ranges[0].size() >= 0xC0000000:
                    if self.mem_ranges[0].size() == 0xC0000000:
                        # esj 2025-05-11
                        entries.append(
                            X86E820Entry(
                                addr=ObjectList.cxl_mem_start,
                                size="%dB" % (self.mem_ranges[1].size()),
                                range_type=1,
                            )
                        )
                        # entries.append(
                        #     X86E820Entry(
                        #         addr=0x100000000,
                        #         size="%dB" % (self.mem_ranges[1].size()),
                        #         range_type=1,
                        #     )
                        # )
                    else:
                        entries.append(
                            X86E820Entry(
                                addr=0x100000000,
                                size="%dB"
                                % (self.mem_ranges[0].size() - 0xC0000000),
                                range_type=1,
                            )
                        )
                        entries.append(
                            X86E820Entry(
                                addr=0x100000000
                                + self.mem_ranges[0].size()
                                - 0xC0000000,
                                size="%dB" % (self.mem_ranges[1].size()),
                                range_type=1,
                            )
                        )
        # Reserve the last 16kB of the 32-bit address space for the m5op interface
        entries.append(
            X86E820Entry(
                addr=CXL_MCFG_BASE,
                size="%dB" % CXL_MCFG_SIZE,
                range_type=2,
            )
        )
        entries.append(
            X86E820Entry(addr=0xFFFF0000, size="64kB", range_type=2)
        )
        print("DEXTER!!!!!!!!!!!")
        for entry in entries:
            print(hex(entry.addr), hex(entry.size))
    self.workload.e820_table.entries = entries

    # Command line
    if not cmdline:
        cmdline = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda1 no_timer_check nolapic_timer idle=nomwait"  # parsec: sda1, 22.04: sda2, 18.04: sda2
        # cmdline = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda1 no_timer_check"  #parsec: sda1, 22.04: sda2, 18.04: sda2
    self.workload.command_line = fillInCmdline(mdesc, cmdline)
    return self


def makeBareMetalRiscvSystem(mem_mode, mdesc=None, cmdline=None):
    self = System()
    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.mem_mode = mem_mode
    self.mem_ranges = [AddrRange(mdesc.mem())]

    self.workload = RiscvBareMetal()

    self.iobus = IOXBar()
    self.membus = MemBus()

    self.bridge = Bridge(delay="50ns")
    self.bridge.mem_side_port = self.iobus.cpu_side_ports
    self.bridge.cpu_side_port = self.membus.mem_side_ports
    # Sv39 has 56 bit physical addresses; use the upper 8 bit for the IO space
    IO_address_space_base = 0x00FF000000000000
    self.bridge.ranges = [AddrRange(IO_address_space_base, Addr.max)]

    self.system_port = self.membus.cpu_side_ports
    return self


def makeDualRoot(full_system, testSystem, driveSystem, dumpfile):
    self = Root(full_system=full_system)
    self.testsys = testSystem
    self.drivesys = driveSystem
    self.etherlink = EtherLink()

    if hasattr(testSystem, "realview"):
        self.etherlink.int0 = Parent.testsys.realview.ethernet.interface
        self.etherlink.int1 = Parent.drivesys.realview.ethernet.interface
    elif hasattr(testSystem, "tsunami"):
        self.etherlink.int0 = Parent.testsys.tsunami.ethernet.interface
        self.etherlink.int1 = Parent.drivesys.tsunami.ethernet.interface
    # else:
    #     # fatal("Don't know how to connect these system together")

    if dumpfile:
        self.etherdump = EtherDump(file=dumpfile)
        self.etherlink.dump = Parent.etherdump

    return self


def makeDistRoot(
    testSystem,
    rank,
    size,
    server_name,
    server_port,
    sync_repeat,
    sync_start,
    linkspeed,
    linkdelay,
    dumpfile,
):
    self = Root(full_system=True)
    self.testsys = testSystem

    self.etherlink = DistEtherLink(
        speed=linkspeed,
        delay=linkdelay,
        dist_rank=rank,
        dist_size=size,
        server_name=server_name,
        server_port=server_port,
        sync_start=sync_start,
        sync_repeat=sync_repeat,
    )

    if hasattr(testSystem, "realview"):
        self.etherlink.int0 = Parent.testsys.realview.ethernet.interface
    elif hasattr(testSystem, "tsunami"):
        self.etherlink.int0 = Parent.testsys.tsunami.ethernet.interface
    else:
        fatal("Don't know how to connect DistEtherLink to this system")

    if dumpfile:
        self.etherdump = EtherDump(file=dumpfile)
        self.etherlink.dump = Parent.etherdump

    return self
