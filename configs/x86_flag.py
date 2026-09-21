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
# This is for checking logging
import logging

class SystemdLogHandler(logging.Handler):
    def __init__(self, membus):
        super().__init__()
        self.membus = membus  # membus 참조 저장

    def emit(self, record):
        if "Reached target Multi-User System." in record.msg:
            print("Multi-User System target reached, setting flag on membus.")
            self.set_flag_on_membus()  # membus에 플래그 설정 메소드 호출

    def set_flag_on_membus(self):
        # membus에 플래그 설정 로직
        self.membus.flag = True  # 예시: flag 속성 설정
        print("Flag set on membus!")

# This is for checking logging





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

    x86_sys.membus = MemBus()

# This is for checking logging

    # 로거 설정
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)

    # 로그 핸들러 추가, membus 참조 전달
    handler = SystemdLogHandler(system.membus)
    logger.addHandler(handler)


# This is for checking logging



    # North Bridge
    x86_sys.iobus = IOXBar()
    x86_sys.bridge = Bridge(delay="50ns")
    x86_sys.bridge.mem_side_port = x86_sys.iobus.cpu_side_ports
    x86_sys.bridge.cpu_side_port = x86_sys.membus.mem_side_ports
    # Allow the bridge to pass through:
    #  1) kernel configured PCI device memory map address: address range
    #     [0xC0000000, 0xFFFF0000). (The upper 64kB are reserved for m5ops.)
    #  2) the bridge to pass through the IO APIC (two pages, already contained in 1),
    #  3) everything in the IO address range up to the local APIC, and
    #  4) then the entire PCI address space and beyond.
    x86_sys.bridge.ranges = [
        AddrRange(0xC0000000, 0xFFFF0000),
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

def connectX86CXLMemory(x86_sys, pcie_devices= []):

    x86_sys.pcie1 = PCIELink(lanes = 16, speed = 32,  mps = 5, max_queue_size= 10)
    x86_sys.pcie2 = PCIELink(lanes = 16, speed = 32,  mps = 5, max_queue_size= 10)
    x86_sys.pcie3 = PCIELink(lanes = 16, speed = 32,  mps = 5, max_queue_size= 10)
    x86_sys.pcie4 = PCIELink(lanes = 16 , speed = 32, mps = 5, max_queue_size= 10)
    x86_sys.pcie5 = PCIELink(lanes = 16 , speed = 32 ,mps = 5, max_queue_size= 10)
    x86_sys.pcie6 = PCIELink(lanes = 16 , speed = 32, mps = 5, max_queue_size= 10)

    x86_sys.RootComplex = RootComplex(IOLimitUpper=0xFFFF, IOBaseUpper=0xCFFF, pci_dev1=6, pci_dev2=8, pci_dev3=10,PXCAPLinkCap =0x01009104, PXCAPLinkStatus =  0x0104) 
    x86_sys.switch = PCIESwitch(IOLimitUpper=0xFFFF, IOBaseUpper=0xCFFF, pci_dev1=0, pci_dev2=2, pci_dev3=4,PXCAPLinkCap =0x01009104, PXCAPLinkStatus =  0x0104)#, delay = '100ns'


    #host setting 
    x86_sys.RootComplex.host = x86_sys.pc.pci_host
    x86_sys.switch.host = x86_sys.pc.pci_host
    
    x86_sys.RootComplex.response = x86_sys.iobus.mem_side_ports
    x86_sys.RootComplex.request_dma = x86_sys.membus.cpu_side_ports

    x86_sys.RootComplex.response_dma1 = x86_sys.pcie1.upstreamRequest
    x86_sys.RootComplex.response_dma2 = x86_sys.pcie2.upstreamRequest
    x86_sys.RootComplex.response_dma3 = x86_sys.pcie3.upstreamRequest 
    x86_sys.RootComplex.request1    = x86_sys.pcie1.upstreamResponse 
    x86_sys.RootComplex.request2    = x86_sys.pcie2.upstreamResponse 
    x86_sys.RootComplex.request3    = x86_sys.pcie3.upstreamResponse 

    x86_sys.switch.response  = x86_sys.pcie1.downstreamRequest
    x86_sys.switch.request_dma = x86_sys.pcie1.downstreamResponse
    
    x86_sys.switch.response_dma1  = x86_sys.pcie6.upstreamRequest
    x86_sys.switch.request1 = x86_sys.pcie6.upstreamResponse
    x86_sys.switch.response_dma2  = x86_sys.pcie5.upstreamRequest
    x86_sys.switch.request2 = x86_sys.pcie5.upstreamResponse
    x86_sys.switch.response_dma3  = x86_sys.pcie4.upstreamRequest
    x86_sys.switch.request3 = x86_sys.pcie4.upstreamResponse


    #RootComplex Test
    # x86_sys.pc.cxlmemdevice1 = CxlMemory(DeviceID= 0x8086, BAR0=PciMemBar(size='256MB'),pci_bus = 3 ,pci_dev= 1, pci_func=0, InterruptLine=2, InterruptPin=1, Status = 0x0010 ,PXCAPLinkCap =0x01009104, PXCAPLinkStatus =  0x0104,PXCAPCapabilities=0x0012,Command =0x0017)
    x86_sys.pc.cxlmemdevice1 = CXLDRAMsim3(DeviceID= 0x8086, BAR0=PciMemBar(size='256MB'),pci_bus = 3 ,pci_dev= 1, pci_func=0, InterruptLine=2, InterruptPin=1, Status = 0x0010 ,PXCAPLinkCap =0x01009104, PXCAPLinkStatus =  0x0104,PXCAPCapabilities=0x0012,Command =0x0017)
    x86_sys.pc.cxlmemdevice1.pio = x86_sys.pcie6.downstreamRequest
    x86_sys.pc.cxlmemdevice1.dma = x86_sys.pcie6.downstreamResponse
    x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host
    pcie_devices.append(x86_sys.pc.cxlmemdevice1)

    # x86_sys.pc.cxlmemdevice2 = CxlMemory(BAR0=PciMemBar(size='256MB'),pci_bus = 4 ,pci_dev= 2, pci_func=0, InterruptLine=2, InterruptPin=1, Status = 0x0010 ,PXCAPLinkCap =0x01009104, PXCAPLinkStatus =  0x0104,Command = 2,PXCAPBaseOffset=0x70)
    # #x86_sys.pc.cxlmemdevice1.pio = x86_sys.membus.mem_side_ports
    # #x86_sys.pc.cxlmemdevice1.dma = x86_sys.iobus.cpu_side_ports
    # x86_sys.pc.cxlmemdevice2.pio = x86_sys.pcie5.downstreamRequest
    # x86_sys.pc.cxlmemdevice2.dma = x86_sys.pcie5.downstreamResponse
    # x86_sys.pc.cxlmemdevice2.host = x86_sys.pc.pci_host
    # pcie_devices.append(x86_sys.pc.cxlmemdevice2)

    #x86_sys.pc.cxlmemdevice3 = CxlMemory(pci_bus = 2,pci_dev=0, pci_func=0, InterruptLine=3, InterruptPin=2)
    #x86_sys.pc.cxlmemdevice3.pio = x86_sys.pcie2.downstreamRequest
    #x86_sys.pc.cxlmemdevice3.dma = x86_sys.pcie2.downstreamResponse
    #x86_sys.pc.cxlmemdevice3.host = x86_sys.pc.pci_host

    # #Switch Test
    # x86_sys.pc.cxlmemdevice2 = CxlMemory(pci_bus = 5,pci_dev=2, pci_func=0, InterruptLine=4, InterruptPin=3)
    # x86_sys.pc.cxlmemdevice2.pio = x86_sys.pcie4.downstreamRequest
    # x86_sys.pc.cxlmemdevice2.dma = x86_sys.pcie4.downstreamResponse
    # x86_sys.pc.cxlmemdevice2.host = x86_sys.pc.pci_host
    # pcie_devices.append(x86_sys.pc.cxlmemdevice2)
    #x86_sys.pc.ethernet1 = IGbE_pcie(pci_bus = 2, pci_dev = 4, pci_func = 0 , InterruptLine = 1, InterruptPin = 2)
    #x86_sys.pc.ethernet1.pio = x86_sys.pcie5.downstreamRequest
    #x86_sys.pc.ethernet1.dma = x86_sys.pcie5.downstreamResponse


    # x86_sys.pc.cxlmemdevice1 = CxlMemory(BAR0=PciMemBar(size='1GB'),pci_bus = 0,pci_dev= 6, pci_func=0, InterruptLine=2, InterruptPin=1)
    # x86_sys.pc.cxlmemdevice1.pio = x86_sys.membus.mem_side_ports
    # x86_sys.pc.cxlmemdevice1.dma = x86_sys.iobus.cpu_side_ports
    # x86_sys.pc.cxlmemdevice1.host = x86_sys.pc.pci_host
    # pcie_devices.append(x86_sys.pc.cxlmemdevice1)

def format_memory_size(decimal_value):
    if decimal_value < 0:
        return "error wrong value"

    suffixes = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB']
    index = 0

    while decimal_value >= 1024 and index < len(suffixes) - 1:
        decimal_value /= 1024.0
        index += 1

    formatted_size = "{}{}".format(int(decimal_value), suffixes[index])
    return formatted_size

def cxl_region_check(cxl_type3_size,pcie_devices):
    for i, device in enumerate(pcie_devices):
        if device.type == "CxlMemory" or device.type == "CXLDRAMsim3":
            cxl_type3_size.append(int(device.BAR0.size))
            print("esj bar size",int(device.BAR0.size))

def makeX86System(mem_mode, numCPUs=1, mdesc=None, workload=None, Ruby=False, cxl_type3_size = [], pcie_devices= []):
    self = System()

    self.m5ops_base = 0xFFFF0000

    if workload is None:
        workload = X86FsWorkload()
    self.workload = workload

    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()

    self.mem_mode = mem_mode

    # Physical memory
    # On the PC platform, the memory region 0xC0000000-0xFFFFFFFF is reserved
    # for various devices.  Hence, if the physical memory size is greater than
    # 3GB, we need to split it into two parts.
    excess_mem_size = convert.toMemorySize(mdesc.mem()) - convert.toMemorySize(
        "3GB"
    )
    print("excess_mem_size : ",excess_mem_size)
	
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

    # Platform
    self.pc = Pc()

    # Create and connect the busses required by each memory system
    if Ruby:
        connectX86RubySystem(self)
    else:
        connectX86ClassicSystem(self, numCPUs)

    #cxl memory 
    connectX86CXLMemory(self, pcie_devices)
    cxl_region_check(cxl_type3_size, pcie_devices)

    if(len(pcie_devices)!=0):
        print("eee",Addr(mdesc.mem()),cxl_type3_size[0])
        self.mem_ranges.append(AddrRange(Addr(mdesc.mem()), size=cxl_type3_size[0]) )
        

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
            bus_source=1, irq_source=irq, sys_int=apicPin, flags=0
        )
        madt_records.append(assign_to_apic_acpi)

    assignISAInt(0, 2)
    assignISAInt(1, 1)
    for i in range(3, 15):
        assignISAInt(i, i)
    workload.intel_mp_table.base_entries = base_entries
    workload.intel_mp_table.ext_entries = ext_entries

    madt = X86ACPIMadt(
        local_apic_address=0, records=madt_records, oem_id="madt"
    )
    workload.acpi_description_table_pointer.rsdt.entries.append(madt)
    workload.acpi_description_table_pointer.xsdt.entries.append(madt)
    workload.acpi_description_table_pointer.oem_id = "gem5"
    workload.acpi_description_table_pointer.rsdt.oem_id = "gem5"
    workload.acpi_description_table_pointer.xsdt.oem_id = "gem5"

    return self


def makeLinuxX86System(
    mem_mode, numCPUs=1, mdesc=None, Ruby=False, cmdline=None
):
    #cxl region 
    start_cxl_mem_addr = 0x200000000
    cxl_type3_size = []
    pcie_devices = []
    # Build up the x86 system and then specialize it for Linux
    self = makeX86System(mem_mode, numCPUs, mdesc, X86FsLinux(), Ruby, cxl_type3_size, pcie_devices)

    # We assume below that there's at least 1MB of memory. We'll require 2
    # just to avoid corner cases.
    phys_mem_size = sum([r.size() for r in self.mem_ranges])
    assert phys_mem_size >= 0x200000
    assert len(self.mem_ranges) <= 2

    if(len(pcie_devices)==0):
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
            #print("esj mem_range = ",self.mem_ranges[0].size())
            entries.append(
                X86E820Entry(
                    addr=self.mem_ranges[0].size(),
                    size="%dB" % (0xC0000000 - self.mem_ranges[0].size()),
                    range_type=2,
                )
            )
        #print("esj mem_range len= ",len(self.mem_ranges))

        # Reserve the last 16kB of the 32-bit address space for the m5op interface
        entries.append(X86E820Entry(addr=0xFFFF0000, size="64kB", range_type=2))

        # In case the physical memory is greater than 3GB, we split it into two
        # parts and add a separate e820 entry for the second part.  This entry
        # starts at 0x100000000,  which is the first address after the space
        # reserved for devices.
        if len(self.mem_ranges) == 2:
            entries.append(
                X86E820Entry(
                    addr=0x100000000,
                    size="%dB" % (self.mem_ranges[1].size()),
                    range_type=1,
                )
            )
    else: ##esj 
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
            X86E820Entry(
                addr=self.mem_ranges[0].size(),
                #size="%dB" % (self.mem_ranges[1].size()),
                size="%dB" % (self.mem_ranges[1].size()),
                range_type=1,
            ),
        ]

        # Mark [mem_size, 3GB) as reserved if memory less than 3GB, which force
        # IO devices to be mapped to [0xC0000000, 0xFFFF0000). Requests to this
        # specific range can pass though bridge to iobus.
        if len(self.mem_ranges) == 2:
            #print("esj mem_range = ",self.mem_ranges[0].size())
            entries.append(
                X86E820Entry(
                    addr=self.mem_ranges[0].size() + self.mem_ranges[1].size(),
                    size="%dB" % (0xC0000000 - (self.mem_ranges[0].size() + self.mem_ranges[1].size())),
                    range_type=2,
                )
            )
        #print("esj mem_range len= ",len(self.mem_ranges))

        # Reserve the last 16kB of the 32-bit address space for the m5op interface
        entries.append(X86E820Entry(addr=0xFFFF0000, size="64kB", range_type=2))

        # In case the physical memory is greater than 3GB, we split it into two
        # parts and add a separate e820 entry for the second part.  This entry
        # starts at 0x100000000,  which is the first address after the space
        # reserved for devices.
        if len(self.mem_ranges) == 3:
            entries.append(
                X86E820Entry(
                    addr=0x100000000,
                    size="%dB" % (self.mem_ranges[2].size()),
                    range_type=1,
                )
            )

    # #==========================
    # # Reserve the cxl memory region 
    # #type=1 : dram으로 잡힘, type=2 : 디바이스가 사용가능한 메모리영역으로 예약 
    
    # for cxlmemory in range(len(pcie_devices)):
    #     mem_size = format_memory_size(cxl_type3_size[cxlmemory])

    #     entries.append(X86E820Entry(addr=start_cxl_mem_addr, size=mem_size, range_type=2))
        
    #     start_cxl_mem_addr = start_cxl_mem_addr + AddrRange(mem_size).size()
    # #============================
    
    self.workload.e820_table.entries = entries

    # Command line
    if not cmdline:
        cmdline = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda1"  #parsec: sda1, 22.04: sda2, 18.04: sda2
#cmdline = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda2"  #22.04
		#cmdline = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/hda1"
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
    else:
        fatal("Don't know how to connect these system together")

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
