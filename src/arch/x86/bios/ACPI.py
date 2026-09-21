# Copyright (c) 2008 The Hewlett-Packard Development Company
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
from m5.SimObject import SimObject

# ACPI description table header. Subclasses contain and handle the actual
# contents as appropriate for that type of table.
class X86ACPISysDescTable(SimObject):
    type = "X86ACPISysDescTable"
    cxx_class = "gem5::X86ISA::ACPI::SysDescTable"
    cxx_header = "arch/x86/bios/acpi.hh"
    abstract = True

    oem_id = Param.String("", "string identifying the oem")
    oem_table_id = Param.String("", "oem table ID")
    oem_revision = Param.UInt32(0, "oem revision number for the table")

    creator_id = Param.UInt32(0, "ID identifying the generator of the table")
    creator_revision = Param.UInt32(
        0, "revision number for the creator of the table"
    )


class X86ACPIRSDT(X86ACPISysDescTable):
    type = "X86ACPIRSDT"
    cxx_class = "gem5::X86ISA::ACPI::RSDT"
    cxx_header = "arch/x86/bios/acpi.hh"

    entries = VectorParam.X86ACPISysDescTable([], "system description tables")


class X86ACPIXSDT(X86ACPISysDescTable):
    type = "X86ACPIXSDT"
    cxx_class = "gem5::X86ISA::ACPI::XSDT"
    cxx_header = "arch/x86/bios/acpi.hh"

    entries = VectorParam.X86ACPISysDescTable([], "system description tables")


class X86ACPISsdtRaw(X86ACPISysDescTable):
    type = "X86ACPISsdtRaw"
    cxx_class = "gem5::X86ISA::ACPI::SSDT::Raw"
    cxx_header = "arch/x86/bios/acpi.hh"

    body = VectorParam.UInt8([], "Raw AML body bytes")


class X86ACPIDsdtRaw(X86ACPISysDescTable):
    type = "X86ACPIDsdtRaw"
    cxx_class = "gem5::X86ISA::ACPI::DSDT::Raw"
    cxx_header = "arch/x86/bios/acpi.hh"

    body = VectorParam.UInt8([], "Raw AML body bytes")


class X86ACPIFadt(X86ACPISysDescTable):
    type = "X86ACPIFadt"
    cxx_class = "gem5::X86ISA::ACPI::FADT"
    cxx_header = "arch/x86/bios/acpi.hh"

    dsdt = Param.X86ACPIDsdtRaw(
        X86ACPIDsdtRaw(), "Differentiated System Description Table"
    )
    preferred_pm_profile = Param.UInt8(0, "Preferred power management profile")
    sci_interrupt = Param.UInt16(9, "System control interrupt vector")
    pm1a_event_block = Param.UInt32(0, "PM1A event block system I/O port")
    pm1a_control_block = Param.UInt32(0, "PM1A control block system I/O port")
    pm1_event_length = Param.UInt8(0, "PM1 event block byte length")
    pm1_control_length = Param.UInt8(0, "PM1 control block byte length")
    iapc_boot_arch = Param.UInt16(
        0x3, "IA-PC boot architecture flags: legacy devices and 8042"
    )
    flags = Param.UInt32(0, "FADT flags")


class X86ACPIMcfg(X86ACPISysDescTable):
    type = "X86ACPIMcfg"
    cxx_class = "gem5::X86ISA::ACPI::MCFG"
    cxx_header = "arch/x86/bios/acpi.hh"

    base_address = Param.Addr(0, "PCIe enhanced configuration space base")
    segment = Param.UInt16(0, "PCI segment group number")
    start_bus = Param.UInt8(0, "First PCI bus number covered")
    end_bus = Param.UInt8(0, "Last PCI bus number covered")


class X86ACPISratRecord(SimObject):
    type = "X86ACPISratRecord"
    cxx_class = "gem5::X86ISA::ACPI::SRAT::Record"
    cxx_header = "arch/x86/bios/acpi.hh"
    abstract = True


class X86ACPISrat(X86ACPISysDescTable):
    type = "X86ACPISrat"
    cxx_class = "gem5::X86ISA::ACPI::SRAT::SRAT"
    cxx_header = "arch/x86/bios/acpi.hh"

    records = VectorParam.X86ACPISratRecord([], "Records in this SRAT")


class X86ACPISratX2Apic(X86ACPISratRecord):
    type = "X86ACPISratX2Apic"
    cxx_class = "gem5::X86ISA::ACPI::SRAT::X2Apic"
    cxx_header = "arch/x86/bios/acpi.hh"

    proximity_domain = Param.UInt32(0, "Processor proximity domain")
    apic_id = Param.UInt32(0, "x2APIC ID")
    flags = Param.UInt32(1, "SRAT x2APIC affinity flags")
    clock_domain = Param.UInt32(0, "Clock domain")


class X86ACPISratMemory(X86ACPISratRecord):
    type = "X86ACPISratMemory"
    cxx_class = "gem5::X86ISA::ACPI::SRAT::Memory"
    cxx_header = "arch/x86/bios/acpi.hh"

    proximity_domain = Param.UInt32(0, "Memory proximity domain")
    base_address = Param.Addr(0, "Memory range base address")
    length = Param.UInt64(0, "Memory range length")
    flags = Param.UInt32(1, "SRAT memory affinity flags")


class X86ACPICedtRecord(SimObject):
    type = "X86ACPICedtRecord"
    cxx_class = "gem5::X86ISA::ACPI::CEDT::Record"
    cxx_header = "arch/x86/bios/acpi.hh"
    abstract = True


class X86ACPICedt(X86ACPISysDescTable):
    type = "X86ACPICedt"
    cxx_class = "gem5::X86ISA::ACPI::CEDT::CEDT"
    cxx_header = "arch/x86/bios/acpi.hh"

    records = VectorParam.X86ACPICedtRecord([], "Records in this CEDT")


class X86ACPICedtChbs(X86ACPICedtRecord):
    type = "X86ACPICedtChbs"
    cxx_class = "gem5::X86ISA::ACPI::CEDT::CHBS"
    cxx_header = "arch/x86/bios/acpi.hh"

    uid = Param.UInt32(0, "ACPI0016 host bridge UID")
    cxl_version = Param.UInt32(1, "CXL version; ACPI CEDT CXL 2.0 is 1")
    base = Param.Addr(0, "CHBS register base address")
    length = Param.UInt64(0x10000, "CHBS register block length")


class X86ACPICedtCfmws(X86ACPICedtRecord):
    type = "X86ACPICedtCfmws"
    cxx_class = "gem5::X86ISA::ACPI::CEDT::CFMWS"
    cxx_header = "arch/x86/bios/acpi.hh"

    base_hpa = Param.Addr(0, "CXL fixed memory window base HPA")
    window_size = Param.UInt64(0, "CXL fixed memory window size")
    restrictions = Param.UInt16(0, "CFMWS restriction flags")
    qtg_id = Param.UInt16(0, "QoS throttling group ID")
    interleave_ways = Param.UInt8(0, "Encoded interleave ways")
    interleave_arithmetic = Param.UInt8(0, "Interleave arithmetic")
    granularity = Param.UInt32(0, "Encoded interleave granularity")
    targets = VectorParam.UInt32([], "Interleave target UIDs")


class X86ACPIMadtRecord(SimObject):
    type = "X86ACPIMadtRecord"
    cxx_class = "gem5::X86ISA::ACPI::MADT::Record"
    cxx_header = "arch/x86/bios/acpi.hh"
    abstract = True


class X86ACPIMadt(X86ACPISysDescTable):
    type = "X86ACPIMadt"
    cxx_class = "gem5::X86ISA::ACPI::MADT::MADT"
    cxx_header = "arch/x86/bios/acpi.hh"

    local_apic_address = Param.UInt32(0, "Address of the local apic")
    flags = Param.UInt32(0, "Flags")
    records = VectorParam.X86ACPIMadtRecord([], "Records in this MADT")


class X86ACPIMadtLAPIC(X86ACPIMadtRecord):
    type = "X86ACPIMadtLAPIC"
    cxx_header = "arch/x86/bios/acpi.hh"
    cxx_class = "gem5::X86ISA::ACPI::MADT::LAPIC"

    acpi_processor_id = Param.UInt8(0, "ACPI Processor ID")
    apic_id = Param.UInt8(0, "APIC ID")
    flags = Param.UInt32(0, "Flags")


class X86ACPIMadtIOAPIC(X86ACPIMadtRecord):
    type = "X86ACPIMadtIOAPIC"
    cxx_header = "arch/x86/bios/acpi.hh"
    cxx_class = "gem5::X86ISA::ACPI::MADT::IOAPIC"

    id = Param.UInt8(0, "I/O APIC ID")
    address = Param.Addr(0, "I/O APIC Address")
    int_base = Param.UInt32(0, "Global Interrupt Base")


class X86ACPIMadtIntSourceOverride(X86ACPIMadtRecord):
    type = "X86ACPIMadtIntSourceOverride"
    cxx_header = "arch/x86/bios/acpi.hh"
    cxx_class = "gem5::X86ISA::ACPI::MADT::IntSourceOverride"

    bus_source = Param.UInt8(0, "Bus Source")
    irq_source = Param.UInt8(0, "IRQ Source")
    sys_int = Param.UInt32(0, "Global System Interrupt")
    flags = Param.UInt16(0, "Flags")


class X86ACPIMadtNMI(X86ACPIMadtRecord):
    type = "X86ACPIMadtNMI"
    cxx_header = "arch/x86/bios/acpi.hh"
    cxx_class = "gem5::X86ISA::ACPI::MADT::NMI"

    acpi_processor_id = Param.UInt8(0, "ACPI Processor ID")
    flags = Param.UInt16(0, "Flags")
    lint_no = Param.UInt8(0, "LINT# (0 or 1)")


class X86ACPIMadtLAPICOverride(X86ACPIMadtRecord):
    type = "X86ACPIMadtLAPICOverride"
    cxx_header = "arch/x86/bios/acpi.hh"
    cxx_class = "gem5::X86ISA::ACPI::MADT::LAPICOverride"

    address = Param.Addr(0, "64-bit Physical Address of Local APIC")


# Root System Description Pointer Structure
class X86ACPIRSDP(SimObject):
    type = "X86ACPIRSDP"
    cxx_class = "gem5::X86ISA::ACPI::RSDP"
    cxx_header = "arch/x86/bios/acpi.hh"

    oem_id = Param.String("", "string identifying the oem")
    # Because 0 encodes ACPI 1.0, 2 encodes ACPI 3.0, the version implemented
    # here.
    revision = Param.UInt8(2, "revision of ACPI being used, zero indexed")

    rsdt = Param.X86ACPIRSDT(X86ACPIRSDT(), "root system description table")
    xsdt = Param.X86ACPIXSDT(
        X86ACPIXSDT(), "extended system description table"
    )
