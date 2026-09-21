from m5.params import *
from m5.objects.PciDevice import *


class CXLDRAMsim3(PciDevice):
    type = "CXLDRAMsim3"
    cxx_header = "dev/cxl_dramsim3.hh"
    cxx_class = "gem5::CXLDRAMsim3"
    latency = Param.Latency(
        "50ns", "cxl-memory device's latency for mem access"
    )
    cxl_mem_latency = Param.Latency(
        "2ns", "cxl.mem protocol processing's latency for device"
    )
    configFile = Param.String(
        "ext/dramsim3/DRAMsim3/configs/DDR3_4Gb_x16_1600.ini",
        "The configuration file to use with DRAMSim3",
    )
    filePath = Param.String(
        "ext/dramsim3/DRAMsim3/", "Directory to prepend to file names"
    )

    host_second = Param.PciHost(NULL, "Optional PCI host #2")
    host_third = Param.PciHost(NULL, "Optional PCI host #3")
    host_fourth = Param.PciHost(NULL, "Optional PCI host #4")

    configFile_write = Param.String(
        "ext/dramsim3/DRAMsim3/configs/DDR3_4Gb_x16_1600.ini",
        "The configuration file to use with DRAMSim3",
    )

    # esj 2025-05-19
    # response_port = ResponsePort("Programmed I/O port")

    # esj 2025-06-22
    Pio2 = ResponsePort("Programmed IO port")

    cxl_mem_size = Param.UInt64(0x0, "cxl-memory size")
    cxl_mem_start = Param.Addr(
        0x0, "Host physical base address of the CXL memory window"
    )
    cxl_logical_device_count = Param.UInt8(
        1, "Number of logical devices sharing this CXL backing device"
    )
    host_visible_cxl_mem_sizes = VectorParam.UInt64(
        [], "Per-host guest-visible CXL memory size"
    )
    host_cxl_device_offsets = VectorParam.UInt64(
        [], "Per-host offset into the shared CXL backing device"
    )
    cxl_chbs_base = Param.Addr(
        0xE0000000, "Fixed ACPI CEDT CHBS component register base"
    )
    cxl_chbs_size = Param.UInt64(
        0x0, "Fixed ACPI CEDT CHBS component register size"
    )

    # esj 2024-10-02
    switch_mode = Param.Bool(False, "cpu switch_mode")

    VendorID = 0x8086  # 00    ##smdk cxl vendor  0x1e98
    DeviceID = 0xABCD  # 02    ##smdk cxl devices 0X0d93
    Command = 0x0017  # 04    Command
    Status = 0x0010  # 06    Capability List 0x0010
    Revision = 0x0  # 08    Device
    ProgIF = 0x10  # 09    CXL memory device programming interface
    SubClassCode = 0x02  # 0A    CXL memory device subclass
    ClassCode = 0x05  # 0B    Memory controller
    # CacheLineSize             # 0C    Written by system
    # LatencyTimer              # 0D    Master Latency Timer (ZERO)
    # HeaderType                # 0E    Single Function | Header Layout
    # BIST                      # 0F    Built-in Self Test (ZERO)
    BAR0 = PciMemBar(size="128KiB")  # 10    CXL component + memdev registers
    # BAR1 = PciMemUpperBar()      # 14    Should be ZERO
    # BAR2 = 0x00000000           # 18    Index/Data pair is not supported
    # BAR3 = 0x00000000           # 1C    Not used (RESERVED)
    # BAR4 = 0x00000000           # 20    MSI-X Table
    # BAR5 = 0x00000000           # 24    MSI-X PBA
    # CardbusCIS                # 28    Cardbus Card Information Structure
    SubsystemVendorID = 0x1AF4  # 2C    Intel Corporation
    SubsystemID = 0x1100  # 2E    Intel 750 Series NVMe SSD
    ExpansionROM = 0x00000000  # 30    Expansion ROM Base Address
    CapabilityPtr = 0x48  # 34    First standard capability pointer
    InterruptLine = 0x1F  # 3C    Interrupt Line
    InterruptPin = 0x00  # 3D    CXL.mem functions use MSI/MSI-X, not INTx
    # MaximumLatency            # 3E    Maximum Latency
    # MinimumGrant              # 3F    Minimum Grant

    # # PMCAP - PCI Power Management Capability
    # PMCAPBaseOffset = 0x40      # --    Base offset of PMCAP in PCI Config space
    # PMCAPCapId = 0x01           # 40    Specifies this is the Power Management capability
    # PMCAPNextCapability = 0x48  # 41    Pointer to next capability block
    # PMCAPCapabilities = 0x0001  # 42    PCI Power Management Capabilities Register  //Device Specific Initialization (No) | Version (1.2)
    # PMCAPCtrlStatus = 0x0008    # 44    PCI Power Management Control and Status     //No Soft Resets

    # MSICAP - Message Signaled Interrupt Capability
    MSICAPBaseOffset = 0x48  # --    Base offset of MSICAP in PCI Config space
    MSICAPCapId = 0x05  # 48    Specifies this is the MSI Capability
    MSICAPNextCapability = 0x70  # 49    Pointer to next capability block
    MSICAPMsgCtrl = 0x0080  # 4A    64-bit MSI capable, one vector
    # MSICAPMsgAddr             # 4C    MSI Message Address
    # MSICAPMsgUpperAddr        # 50    MSI Message Upper Address
    # MSICAPMsgData             # 54    MSI Message Data
    # MSICAPMaskBits            # 56    MSI Mask Bits
    # MSICAPPendingBits         # 5A    MSI Pending Bits

    # # MSIXCAP Message Signaled Interrupt eXtended Capability
    # MSIXCAPBaseOffset = 0x60    # --    MSIXCAP capability base
    # MSIXCAPCapId = 0x11         # 60    MSIXCAP ID
    # MSIXCAPNextCapability = 0x70# 61   Next capability pointer
    # MSIXMsgCtrl = 0x01FF        # 62    MSI-X Message Control (512 vectors)
    # MSIXTableOffset = 0x00000004# 64   MSI-X Table Offset/BIR (Use BAR4)
    # MSIXPbaOffset = 0x00000005  # 68    MSI-X PBA Offset/BIR (Use BAR5)

    # PXCAP - PCI Express Capability
    PXCAPBaseOffset = (
        0x70  # --    Base offset of PXCAP in PCI Config space 04h
    )
    PXCAPCapId = 0x10  # 58    Specifies this is the PCIe Capability
    PXCAPNextCapability = 0x00  # 59    Pointer to next standard capability
    PXCAPCapabilities = 0x0002  # 5A    PCIe Device Capabilities
    PXCAPDevCapabilities = 0x10048025  # 5C    PCIe Device Capabilities  //Function Level Reset Capability | Role-based Error Reporting
    PXCAPDevCtrl = 0x51A0  # 60    PCIe Device Control
    PXCAPDevStatus = 0x0000  # 62    PCIe Device Status
    PXCAPLinkCap = 0x01009104  # 64    PCIe Link Capabilities
    PXCAPLinkCtrl = 0x0000  # 68    PCIe Link Control
    PXCAPLinkStatus = 0x0104  # 6A    PCIe Link Status
    PXCAPDevCap2 = 0x01009104  # 6C    PCIe Device Capabilities 2
    PXCAPDevCtrl2 = 0x00000000  # 70    PCIe Device Control 2

    # CXL DVSEC locator Register
    # PCI Express Extended Capability Header (offset 40h)    ##32bit
    DVSECCapId2 = Param.UInt16(0x23, "DVSEC CapId")  # 16bit
    DVSECversion2 = Param.UInt8(0x1, "DVSEC version")  # 8bit
    DVSECNextCapability2 = Param.UInt8(0xB0, "DVSEC NextCapability")  # 8bit
    # Designated Vendor-Specific Header 1 (Offset 44h)   ##32bit
    REGBLK_VendorID2 = Param.UInt16(0x1E98, "REGBLK_VendorID2")  # 16bit
    REGBLK_Revision2 = Param.UInt8(0x0, "REGBLK_Revision2")  # 4bit
    REGBLK_length = Param.UInt16(0x1C, "REGBLK_length")  # 44    REGBLK_length

    # Designated Vendor-Specific Header 2 (Offset 48h)   ##16bit
    REGBLK_DeviceID = Param.UInt16(0x8, "Device ID_base")  # 16bit

    REGBLK1_LOW = Param.UInt32(
        0x00000100, "REGBLK1_low"
    )  # Component RBI, BAR0
    REGBLK1_HIGH = Param.UInt32(
        0x00000000, "REGBLK1_high"
    )  # 50    REGBLK1_high
    REGBLK2_LOW = Param.UInt32(
        0x00010300, "REGBLK1_low"
    )  # Memdev RBI, BAR0 + 64KiB
    REGBLK2_HIGH = Param.UInt32(
        0x00000000, "REGBLK1_low"
    )  # 48    REGBLK2_high
    REGBLK3_LOW = Param.UInt32(0x00000000, "REGBLK1_low")  # 5C    REGBLK3_low
    REGBLK3_HIGH = Param.UInt32(
        0x00000000, "REGBLK1_low"
    )  # 60    REGBLK3_high

    # CXL Capability Header Register
    # PCI Express Extended Capability Header (offset 64h)    ##32bit
    CXL_CapId = Param.UInt16(0x23, "CXL CapId")  # 16bit
    CXLversion = Param.UInt8(0x1, "CXL version")  # 4bit
    CXLCacheMemversion = Param.UInt8(0xB0, "CXL CXLCacheMemversion")  # 4bit
    CXL_ArraySize = Param.UInt16(0x50, "CXL_ArraySize")  # 16bit

    # ##########################################
    # 000h
    # PCI Express Extended Capability Header (offset 100h)    ##32bit
    DVSECCapId = Param.UInt16(0x23, "DVSEC CapId")  # 16bit
    DVSECversion = Param.UInt8(0x1, "DVSEC version")  # 8bit
    DVSECNextCapability = Param.UInt8(0xB4, "DVSEC NextCapability")  # 8bit

    # Designated Vendor-Specific Header 1 (Offset 104h)   ##32bit
    VendorID2 = Param.UInt16(0x1E98, "Vendor ID")  # 16bit
    Revision2 = Param.UInt8(0x3, "Device")  # 4bit
    Length = Param.UInt16(0x3C, "Device")  # 12bit

    DVSEC_Header1_Offset = Param.UInt16(
        0xB4, "Base offset of Designated Vendor-Specific Header 1"
    )

    # Designated Vendor-Specific Header 2 (Offset 108h)   ##16bit
    DeviceID = Param.UInt16(0x0, "Device ID_base")  # 16bit

    DVSEC_Header2_Offset = Param.UInt16(
        0xB8, "Base offset of Designated Vendor-Specific Header 1"
    )

    # DVSEC CXL Capability (Offset 10Ah)                                                                  ##16bit
    Cache_Capable = Param.UInt8(0, "Cache_Capable")  # 1bit
    IO_Capable = Param.UInt8(1, "IO_Capable")  # 1bit
    Mem_Capable = Param.UInt8(1, "Mem_Capable")  # 1bit
    Mem_HwInit_Mode = Param.UInt8(1, "Mem_HwInit_Mode")  # 1bit
    HDM_Count = Param.UInt8(1, "HDM_Count")  # 2bit
    Cache_Writeback_Invalidate_Capable = Param.UInt8(
        0, "Cache_Writeback_Invalidate_Capable"
    )  # 1bit
    CXL_Reset_Capable = Param.UInt8(0, "CXL_Reset_Capable")  # 1bit
    CXL_Reset_Timeout = Param.UInt8(0, "CXL_Reset_Timeout")  # 3bit
    CXL_Reset_Mem_Clr_Capable = Param.UInt8(
        0, "CXL_Reset_Mem_Clr_Capable"
    )  # 1bit
    TSP_Capable = Param.UInt8(0, "TSP_Capable")  # 1bit
    Multiple_Logical_Device = Param.UInt8(0, "Multiple_Logical_Device")  # 1bit
    Viral_Capable = Param.UInt8(1, "Viral_Capable")  # 1bit
    PM_Init_Completion_Reporting_Capable = Param.UInt8(
        0, "PM_Init_Completion_Reporting_Capable"
    )  # 1bit

    DVSEC_CXL_Cap_Offset = Param.UInt16(
        0xBA, "Base offset of DVSEC CXL Capability"
    )

    # DVSEC CXL Control (Offset 11Ch)                                             ##16bit
    Cache_Enable = Param.UInt8(0, "Cache_Enable")  # 1bit
    IO_Enable = Param.UInt8(1, "IO_Capable")  # 1bit
    Mem_Enable = Param.UInt8(1, "Mem_Capable")  # 1bit
    Cache_SF_Coverage = Param.UInt8(1, "Cache_SF_Coverage")  # 5bit
    Cache_SF_Granularity = Param.UInt8(0, "Cache_SF_Granularity")  # 3bit
    Cache_Clean_Eviction = Param.UInt8(0, "Cache_Clean_Eviction")  # 1bit
    Direct_P2P_Mem_Enable = Param.UInt8(0, "Direct P2P Mem Enable")  # 1bit
    Reserved1 = Param.UInt8(0, "Reserved1")  # 1bit
    Viral_Enable = Param.UInt8(0, "Viral_Enable")  # 1bit
    Reserved2 = Param.UInt8(0, "Reserved2")  # 1bit

    DVSEC_CXL_Control_Offset = Param.UInt16(
        0xBC, "Base offset of DVSEC CXL Control"
    )

    # DVSEC CXL Status (Offset 11Eh)                              ##16bit
    Reserved3 = Param.UInt16(0, "Reserved3")  # 14bits
    Viral_Status = Param.UInt8(0, "Viral_Status")  # 1bit
    Reserved4 = Param.UInt8(0, "Reserved4")  # 1bit

    DVSEC_CXL_Status_Offset = Param.UInt16(
        0xBE, "Base offset of DVSEC CXL Status"
    )

    # DVSEC CXL Control2 (Offset 120h)                                                                            ##16bit
    Disable_Caching = Param.UInt8(0, "Disable_Caching")  # 1bit
    Initiate_Cache_Write_Back_and_Invalidation = Param.UInt8(
        0, "Initiate_Cache_Write_Back_and_Invalidation"
    )  # 1bit
    Initiate_CXL_Reset = Param.UInt8(0, "Initiate_CXL_Reset")  # 1bit
    CXL_Reset_Mem_Clr_Enable = Param.UInt8(
        0, "CXL_Reset_Mem_Clr_Enable"
    )  # 1bit
    Desired_Volatile_HDM_State_after_Hot_Reset = Param.UInt8(
        0, "Desired_Volatile_HDM_State_after_Hot_Reset"
    )  # 1bit
    Modified_Completion_Enable = Param.UInt8(
        0, "Modified_Completion_Enable"
    )  # 1bit
    Reserved5 = Param.UInt16(0, "Reserved")  # 10bits

    DVSEC_CXL_Control2_Offset = Param.UInt16(
        0xC0, "Base offset of DVSEC CXL Control2"
    )

    # DVSEC CXL Status2 (Offset 122h)                                                                             ##16bit
    Cache_Invalid = Param.UInt8(0, "Cache_Invalid")  # 1bit
    CXL_Reset_Complete = Param.UInt8(0, "CXL_Reset_Complete")  # 1bit
    CXL_Reset_Error = Param.UInt8(0, "CXL Reset Error")  # 1bit
    Volatile_HDM_Preservation_Error = Param.UInt8(
        0, "Volatile HDM Preservation Error"
    )  # 1bit
    Reserved6 = Param.UInt16(0, "Reserved")  # 11bit
    Power_Management_Initialization_Complete = Param.UInt8(
        0, "Power Management Initialization Complete"
    )  # 1bit

    DVSEC_CXL_Status2_Offset = Param.UInt16(
        0xC2, "Base offset of DVSEC CXL Status2"
    )

    # DVSEC CXL Lock (Offset 124h)                        ##16bit
    CONFIG_LOCK = Param.UInt8(0, "CONFIG_LOCK")  # 1bit
    Reserved7 = Param.UInt16(0, "Reserved")  # 15bit

    DVSEC_CXL_Lock_Offset = Param.UInt16(0xC4, "Base offset of DVSEC CXL Lock")

    # DVSEC CXL Capability2 (Offset 126h)
    Cache_Size_Unit = Param.UInt8(0, "Cache Size Unit")  # 4bit
    Fallback_Capability = Param.UInt16(
        0, "Fallback_Capability"
    )  # type3 = 11b              #2bit
    Modified_Completion_Capable = Param.UInt8(
        0, "Modified_Completion_Capable"
    )  # 1bit
    No_Clean_Writeback = Param.UInt8(0, "No_Clean_Writeback")  # 1bit
    Cache_Size = Param.UInt8(0, "Cache_Size")  # 8bit

    DVSEC_CXL_Cap2_Offset = Param.UInt16(
        0xC6, "Base offset of DVSEC CXL Capability2"
    )

    # DVSEC CXL Range 1 Size High (Offset 128h)
    Memory_Size_High1 = Param.UInt32(0, "Memory_Size_High")  # 32bit

    DVSEC_CXL_Range1_Size_high_Offset = Param.UInt16(
        0xC8, "Base offset of DVSEC CXL Range 1 Size High"
    )

    # DVSEC CXL Range 1 Size Low (Offset 12Ch)
    Memory_Info_Valid1 = Param.UInt8(1, "Memory_Info_Valid1")  # 1bit
    Memory_Active1 = Param.UInt8(1, "Memory_Active1")  # 1bit
    Media_Type1 = Param.UInt8(
        0, "Media_Type1"
    )  # 0: volatile mem                             #3bit
    Memory_Class1 = Param.UInt8(
        0, "Memory_Class1"
    )  # 0: Memory Class(norm dram)                #3bit
    Desired_Interleave1 = Param.UInt8(0, "Desired_Interleave1")  # 5bit
    Memory_Active_Timeout1 = Param.UInt8(0, "Memory_Active_Timeout1")  # 3bit
    Memory_Active_Degraded1 = Param.UInt8(0, "Memory_Active_Degraded1")  # 1bit
    Reserved8 = Param.UInt16(0, "Reserved")  # 11bit
    Memory_Size_Low1 = Param.UInt8(0, "Memory_Size_Low1")  # 4bit

    DVSEC_CXL_Range1_Size_Low_Offset = Param.UInt8(
        0xCC, "Base offset of DVSEC CXL Range 1 Size Low"
    )

    # DVSEC CXL Range 1 Base High (Offset 130h)
    Memory_Base_High1 = Param.Int(0, "Memory_Base_High1")  # 32bit

    DVSEC_CXL_Range1_Base_high_Offset = Param.UInt16(
        0xD0, "Base offset of DVSEC CXL Range 1 Base High"
    )

    # DVSEC CXL Range 1 Base Low (Offset 134h)
    Reserved9 = Param.UInt32(0, "Reserved")  # 28bit
    Memory_Base_Low1 = Param.UInt8(0, "Memory_Base_Low1")  # 4bit

    DVSEC_CXL_Range1_Base_Low_Offset = Param.UInt16(
        0xD4, "Base offset of DVSEC CXL Range 1 Base Low"
    )

    # DVSEC CXL Range 2 Size High (Offset 138h)
    Memory_Size_High2 = Param.UInt32(32, "Memory_Size_High")  # 32bit

    DVSEC_CXL_Range2_Size_high_Offset = Param.UInt16(
        0xD8, "Base offset of DVSEC CXL Range 2 Size High"
    )

    # DVSEC CXL Range 2 Size Low (Offset 13Ch)
    Memory_Info_Valid2 = Param.UInt8(0, "Memory_Info_Valid2")  # 1bit
    Memory_Active2 = Param.UInt8(0, "Memory_Active2")  # 1bit
    Media_Type2 = Param.UInt8(0, "Media_Type2")  # 3bit
    Memory_Class2 = Param.UInt8(0, "Memory_Class2")  # 3bit
    Desired_Interleave2 = Param.UInt8(0, "Desired_Interleave2")  # 5bit
    Memory_Active_Timeout2 = Param.UInt8(0, "Memory_Active_Timeout2")  # 3bit
    Memory_Active_Degraded2 = Param.UInt8(0, "Memory_Active_Degraded2")  # 1bit
    Reserved10 = Param.UInt16(0, "Reserved")  # 11bit
    Memory_Size_Low2 = Param.UInt8(0, "Memory_Size_Low2")  # 4bit

    DVSEC_CXL_Range2_Size_Low_Offset = Param.UInt8(
        0xDC, "Base offset of DVSEC CXL Range 2 Size Low"
    )

    # DVSEC CXL Range 2 Base High (Offset 140h)
    Memory_Base_High2 = Param.UInt32(0, "Memory_Base_High2")  # 32bit

    DVSEC_CXL_Range2_Base_high_Offset = Param.UInt16(
        0xE0, "Base offset of DVSEC CXL Range 2 Base High"
    )

    # DVSEC CXL Range 2 Base Low (Offset 144h)
    Reserved11 = Param.UInt32(0, "Reserved")  # 28bit
    Memory_Base_Low2 = Param.UInt8(0, "Memory_Base_Low2")  # 4bit

    DVSEC_CXL_Range2_Base_Low_Offset = Param.UInt16(
        0xEC, "Base offset of DVSEC CXL Range 2 Base Low"
    )

    # DVSEC CXL Capability3 (Offset 148h)
    Default_Volatile_HDM_State_after_Cold_Reset = Param.UInt8(
        0, "Default_Volatile_HDM_State_after_Cold_Reset"
    )  # 1bit
    Default_Volatile_HDM_State_after_Warm_Reset = Param.UInt8(
        0, "Default_Volatile_HDM_State_after_Warm_Reset"
    )  # 1bit
    Default_Volatile_HDM_State_after_Hot_Reset = Param.UInt8(
        0, "Default_Volatile_HDM_State_after_Hot_Reset"
    )  # 1bit
    Volatile_HDM_State_after_Hot_Reset_Configurability = Param.UInt8(
        0, "Volatile_HDM_State_after_Hot_Reset_Configurability"
    )  # 1bit
    Direc_P2P_Mem_Capable = Param.UInt8(0, "Direc_P2P_Mem_Capable")  # 1bit
    Reserved12 = Param.UInt16(0, "Reserved")  # 11bit

    DVSEC_CXL_Capability3_Offset = Param.UInt16(
        0xE8, "Base offset of DVSEC CXL Capability3"
    )
