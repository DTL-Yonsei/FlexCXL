# Copyright (c) 2008 The Regents of The University of Michigan
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

from m5.params import *
from m5.proxy import *

from m5.objects.Device import IsaFake, BadAddr
from m5.objects.Platform import Platform
from m5.objects.SouthBridge import SouthBridge
from m5.objects.Terminal import Terminal
from m5.objects.Uart import Uart8250
from m5.objects.PciHost import GenericPciHost
from m5.objects.XBar import IOXBar
from m5.objects.PciBridge2 import * #esj
from m5.objects.CommMonitor import *
from m5.objects.MemTraceProbe import *

def x86IOAddress(port):
    IO_address_space_base = 0x8000000000000000
    return IO_address_space_base + port


class PcPciHost(GenericPciHost):
    conf_base = 0xC000000000000000
    conf_size = "16MiB"

    pci_pio_base = 0x8000000000000000


class Pc(Platform):
    type = "Pc"
    cxx_header = "dev/x86/pc.hh"
    cxx_class = "gem5::Pc"
    system = Param.System(Parent.any, "system")

    south_bridge = Param.SouthBridge(SouthBridge(), "Southbridge")

    #cxl esj
    #RootComplex = Param.RootComplex(RootComplex(), "RootComplex")
    #switch = Param.PCIESwitch(PCIESwitch(), "switch")

    pci_host = PcPciHost()

    # Serial port and terminal
    com_1 = Uart8250()
    com_1.pio_addr = x86IOAddress(0x3F8)
    com_1.device = Terminal()

    # Devices to catch access to non-existant serial ports.
    fake_com_2 = IsaFake(pio_addr=x86IOAddress(0x2F8), pio_size=8)
    fake_com_3 = IsaFake(pio_addr=x86IOAddress(0x3E8), pio_size=8)
    fake_com_4 = IsaFake(pio_addr=x86IOAddress(0x2E8), pio_size=8)

    # A device to catch accesses to the non-existant floppy controller.
    fake_floppy = IsaFake(pio_addr=x86IOAddress(0x3F2), pio_size=2)

    # A bus for accesses not claimed by a specific device.
    default_bus = IOXBar()

    # A device to handle accesses to unclaimed IO ports.
    empty_isa = IsaFake(
        pio_addr=x86IOAddress(0),
        pio_size="64KiB",
        ret_data8=0,
        ret_data16=0,
        ret_data32=0,
        ret_data64=0,
        pio=default_bus.mem_side_ports,
    )

    #esj 
    # comm_monitor_pci = CommMonitor()
    # #esj add south_bridge monitor
    # south_bus = IOXBar()
    # bus_monitor = CommMonitor()
    # bus_monitor.trace = MemTraceProbe(trace_compress=False,trace_file = "south_bridge.trc.gz")

    # A device to handle any other type of unclaimed access.
    bad_addr = BadAddr(pio=default_bus.default)

    def attachIO(self, bus, dma_ports=[]):
        #esj add south_bridge monitor
        # self.bus_monitor.cpu_side_port = bus.mem_side_ports
        # self.bus_monitor.mem_side_port = self.south_bus.cpu_side_ports
        # self.south_bridge.attachIO(self.south_bus, dma_ports)
        # self.south_bridge.io_apic.int_requestor = bus.cpu_side_ports
        self.south_bridge.attachIO(bus, dma_ports)

        self.com_1.pio = bus.mem_side_ports
        self.fake_com_2.pio = bus.mem_side_ports
        self.fake_com_3.pio = bus.mem_side_ports
        self.fake_com_4.pio = bus.mem_side_ports
        self.fake_floppy.pio = bus.mem_side_ports

        ##
        # self.pci_host.pio = self.comm_monitor_pci.mem_side_port
        # bus.mem_side_ports = self.comm_monitor_pci.cpu_side_port
        # self.comm_monitor_pci.trace = MemTraceProbe(trace_file = "pci_host_trace.trc.gz")
        self.pci_host.pio = bus.mem_side_ports

        self.default_bus.cpu_side_ports = bus.default
