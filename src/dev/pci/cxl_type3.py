from m5.params import *
from m5.objects.PciDevice import *


class cxl_type3(PciDevice):
    type = "cxl_type3"
    cxx_header = "dev/pci/cxl_type3.hh"
    cxx_class = "gem5::cxl_type3"

    VendorID = 0x1e98           # 00    ##smdk cxl vendor  0x1e98
    DeviceID = 0X0d93             # 02    ##smdk cxl devices 0X0d93
    Command = 0x0000            # 04    Command
    Status = 0x0010              # 06    Capability List 0x0010
    Revision = 0x0             # 08    Device
    ProgIF = 0x02               # 09    CXL type3 0x02
    SubClassCode = 0x01         # 0A    Non-Volatile Memory controller 0x01
    ClassCode = 0x01            # 0B    smdk cxl mem 0x01
    # CacheLineSize             # 0C    Written by system
    # LatencyTimer              # 0D    Master Latency Timer (ZERO)
    # HeaderType                # 0E    Single Function | Header Layout
    # BIST                      # 0F    Built-in Self Test (ZERO)
    #BAR0 = PciMemBar(size='4GiB')   # 10    TYPE = 32bit address space          
    #BAR1 = PciMemUpperBar()      # 14    Should be ZERO                      
    #BAR2 = 0x00000000           # 18    Index/Data pair is not supported
    #BAR3 = 0x00000000           # 1C    Not used (RESERVED)
    #BAR4 = 0x00000000           # 20    MSI-X Table
    #BAR5 = 0x00000000           # 24    MSI-X PBA
    # CardbusCIS                # 28    Cardbus Card Information Structure
    SubsystemVendorID = 0x8086  # 2C    Intel Corporation
    SubsystemID = 0x3704        # 2E    Intel 750 Series NVMe SSD
    # ExpansionROM              # 30    Expansion ROM Base Address
    CapabilityPtr = 0x48        # 34    First capability pointer
    InterruptLine = 0x1f        # 3C    Interrupt Line
    InterruptPin = 0x01         # 3D    Use INT A
    # MaximumLatency            # 3E    Maximum Latency
    # MinimumGrant              # 3F    Minimum Grant

    # MSICAP - Message Signaled Interrupt Capability
    MSICAPBaseOffset = 0x48     # --    Base offset of MSICAP in PCI Config space
    MSICAPCapId = 0x05          # 48    Specifies this is the MSI Capability
    MSICAPNextCapability = 0x60 # 49    Pointer to next capability block
    MSICAPMsgCtrl = 0x018A      # 4A    MSI Message Control
    # MSICAPMsgAddr             # 4C    MSI Message Address
    # MSICAPMsgUpperAddr        # 50    MSI Message Upper Address
    # MSICAPMsgData             # 54    MSI Message Data
    # MSICAPMaskBits            # 56    MSI Mask Bits
    # MSICAPPendingBits         # 5A    MSI Pending Bits

    # MSIXCAP Message Signaled Interrupt eXtended Capability
    MSIXCAPBaseOffset = 0x60    # --    MSIXCAP capability base
    MSIXCAPCapId = 0x11         # 60    MSIXCAP ID
    MSIXCAPNextCapability = 0x70# 61   Next capability pointer
    MSIXMsgCtrl = 0x01FF        # 62    MSI-X Message Control (512 vectors)
    MSIXTableOffset = 0x00000004# 64   MSI-X Table Offset/BIR (Use BAR4)
    MSIXPbaOffset = 0x00000005  # 68    MSI-X PBA Offset/BIR (Use BAR5)

    # PXCAP - PCI Express Capability
    PXCAPBaseOffset = 0x04      # --    Base offset of PXCAP in PCI Config space 04h
    PXCAPCapId = 0x10           # 70    Specifies this is the PCIe Capability
    PXCAPNextCapability = 0xA4  # 71    Pointer to next capability block
    PXCAPCapabilities = 0x0002  # 72    PCIe Device Capabilities
    PXCAPDevCapabilities = \
            0x10008000              # 74    PCIe Device Capabilities  //Function Level Reset Capability | Role-based Error Reporting
    PXCAPDevCtrl = 0x0000       # 78    PCIe Device Control
    PXCAPDevStatus = 0x0000     # 7A    PCIe Device Status
    PXCAPLinkCap = 0x00000000   # 7C    PCIe Link Capabilities
    PXCAPLinkCtrl = 0x0000      # 80    PCIe Link Control
    PXCAPLinkStatus = 0x0001    # 82    PCIe Link Status
    PXCAPDevCap2 = 0x00000010   # 94    PCIe Device Capabilities 2
    PXCAPDevCtrl2 = 0x00000000  # 98    PCIe Device Control 2

    # PMCAP - PCI Power Management Capability
    PMCAPBaseOffset = 0xA4      # --    Base offset of PMCAP in PCI Config space
    PMCAPCapId = 0x01           # A4    Specifies this is the Power Management capability
    PMCAPNextCapability = 0x00  # A5    Pointer to next capability block
    PMCAPCapabilities = 0x0003  # A6    PCI Power Management Capabilities Register  //Device Specific Initialization (No) | Version (1.2)
    PMCAPCtrlStatus = 0x0008    # A8    PCI Power Management Control and Status     //No Soft Resets


##########################################
    # #PCI Express Extended Capability Header (offset B0h)    ##32bit
    # DVSECCapId          = Param.UInt16(0x23, "DVSEC CapId")            #16bit 
    # DVSECversion        = Param.UInt8(0x1,"DVSEC version")          #16bit 
    # DVSECNextCapability = Param.UInt8(0,"DVSEC NextCapability")   #16bit 

    # #Designated Vendor-Specific Header 1 (Offset B4h)   ##32bit 
    # VendorID        = Param.UInt16("Vendor ID")         #16bit 
    # Revision        = Param.UInt8(0, "Device")          #4bit
    # Length          = Param.UInt16(0, "Device")         #12bit

    # DVSEC_Header1_Offset = Param.UInt16(
    #     0xB4, "Base offset of Designated Vendor-Specific Header 1"
    # )

    # #Designated Vendor-Specific Header 2 (Offset B8h)   ##16bit 
    # DeviceID        = Param.UInt16("Device ID_base")    #16bit

    # DVSEC_Header2_Offset = Param.UInt16(
    #     0xB8, "Base offset of Designated Vendor-Specific Header 1"
    # )

    
    # #DVSEC CXL Capability (Offset BAh)                                                                  ##16bit 
    # Cache_Capable                           = Param.Bool(0, "Cache_Capable")                            #1bit
    # IO_Capable                              = Param.Bool(1, "IO_Capable")                               #1bit
    # Mem_Capable                             = Param.Bool(1, "Mem_Capable")                         #1bit
    # Mem_HwInit_Mode                         = Param.Bool(1, "Mem_HwInit_Mode")                          #1bit
    # HDM_Count                               = Param.Bool(1, "HDM_Count")                                #1bit
    # Cache_Writeback_Invalidate_Capable      = Param.Bool(0, "Cache_Writeback_Invalidate_Capable")      #1bit
    # CXL_Reset_Capable                       = Param.Bool(0, "CXL_Reset_Capable")                        #1bit
    # CXL_Reset_Timeout                       = Param.UInt8(0, "CXL_Reset_Timeout")                       #3bit
    # CXL_Reset_Mem_Clr_Capable               = Param.Bool(0, "CXL_Reset_Mem_Clr_Capable")                #1bit
    # TSP_Capable                             = Param.Bool(0, "TSP_Capable")                              #1bit
    # Multiple_Logical_Device                 = Param.Bool(0, "Multiple_Logical_Device")                  #1bit
    # Viral_Capable                           = Param.Bool(1, "Viral_Capable")                            #1bit
    # PM_Init_Completion_Reporting_Capable    = Param.Bool(0, "PM_Init_Completion_Reporting_Capable")     #1bit
 
    # DVSEC_CXL_Cap_Offset = Param.UInt16(
    #     0xBA, "Base offset of DVSEC CXL Capability"
    # )

    # #DVSEC CXL Control (Offset CCh)                                             ##16bit 
    # Cache_Enable                = Param.Bool(0, "Cache_Enable")                 #1bit
    # IO_Enable                   = Param.Bool(1, "IO_Capable")                   #1bit
    # Mem_Enable                  = Param.Bool(1, "Mem_Capable")                  #1bit
    # Cache_SF_Coverage           = Param.UInt8(1, "Cache_SF_Coverage")           #5bit
    # Cache_SF_Granularity        = Param.UInt8(0, "Cache_SF_Granularity")        #3bit
    # Cache_Clean_Eviction        = Param.Bool(0, "Cache_Clean_Eviction")         #1bit
    # Direct_P2P_Mem_Enable       = Param.Bool(0, "Direct P2P Mem Enable")        #1bit
    # Reserved1                   = Param.Bool(0, "Reserved1")                    #1bit
    # Viral_Enable                = Param.Bool(0, "Viral_Enable")                 #1bit
    # Reserved2                   = Param.Bool(0, "Reserved2")                    #1bit
    
    # DVSEC_CXL_Control_Offset = Param.UInt16(
    #     0xCC, "Base offset of DVSEC CXL Control"
    # )

    # #DVSEC CXL Status (Offset CEh)                              ##16bit 
    # Reserved3           = Param.UInt16(0, "Reserved3")          #14bits
    # Viral_Status        = Param.Bool(0, "Viral_Status")         #1bit
    # Reserved4           = Param.Bool(0, "Reserved4")            #1bit

    # DVSEC_CXL_Status_Offset = Param.UInt16(
    #     0xCE, "Base offset of DVSEC CXL Status"
    # )

    # #DVSEC CXL Control2 (Offset D0h)                                                                            ##16bit 
    # Disable_Caching                             = Param.Bool(0, "Disable_Caching")                              #1bit
    # Initiate_Cache_Write_Back_and_Invalidation  = Param.Bool(0, "Initiate_Cache_Write_Back_and_Invalidation")   #1bit
    # Initiate_CXL_Reset                          = Param.Bool(0, "Initiate_CXL_Reset")                           #1bit
    # CXL_Reset_Mem_Clr_Enable                    = Param.Bool(0, "CXL_Reset_Mem_Clr_Enable")                     #1bit
    # Desired_Volatile_HDM_State_after_Hot_Reset  = Param.Bool(0, "Desired_Volatile_HDM_State_after_Hot_Reset")   #1bit
    # Modified_Completion_Enable                  = Param.Bool(0, "Modified_Completion_Enable")                   #1bit
    # Reserved5                                   = Param.UInt16(0, "Reserved")                                      #10bits

    # DVSEC_CXL_Control2_Offset = Param.UInt16(
    #     0xD0, "Base offset of DVSEC CXL Control2"
    # )

    # #DVSEC CXL Status2 (Offset D2h)                                                                             ##16bit 
    # Cache_Invalid                               = Param.Bool(0, "Cache_Invalid")                                #1bit 
    # CXL_Reset_Complete                          = Param.Bool(0, "CXL_Reset_Complete")                           #1bit 
    # CXL_Reset_Error                             = Param.Bool(0, "CXL Reset Error")                              #1bit 
    # Volatile_HDM_Preservation_Error             = Param.Bool(0, "Volatile HDM Preservation Error")              #1bit 
    # Reserved6                                   = Param.UInt16(0, "Reserved")                                      #11bit 
    # Power_Management_Initialization_Complete    = Param.Bool(0, "Power Management Initialization Complete")     #1bit 

    # DVSEC_CXL_Status2_Offset = Param.UInt16(
    #     0xD2, "Base offset of DVSEC CXL Status2"
    # )

    # #DVSEC CXL Lock (Offset D4h)                        ##16bit 
    # CONFIG_LOCK         = Param.Bool(0, "CONFIG_LOCK")  #1bit
    # Reserved7           = Param.UInt16(0, "Reserved")      #15bit

    # DVSEC_CXL_Lock_Offset = Param.UInt16(
    #     0xD4, "Base offset of DVSEC CXL Lock"
    # )

    # # DVSEC CXL Capability2 (Offset D6h)
    # Cache_Size_Unit                         = Param.UInt8(0, "Cache Size Unit")                                 #4bit
    # Fallback_Capability                     = Param.UInt16(0, "Fallback_Capability")  #type3 = 11b              #2bit
    # Modified_Completion_Capable             = Param.Bool(0, "Modified_Completion_Capable")                      #1bit
    # No_Clean_Writeback                      = Param.Bool(0,"No_Clean_Writeback")                                #1bit
    # Cache_Size                              = Param.UInt8(0, "Cache_Size")                                      #8bit

    # DVSEC_CXL_Cap2_Offset = Param.UInt16(
    #     0xD6, "Base offset of DVSEC CXL Capability2"
    # )

    # #DVSEC CXL Range 1 Size High (Offset D8h)
    # Memory_Size_High1   = Param.UInt32(0, "Memory_Size_High")                                                   #32bit

    # DVSEC_CXL_Range1_Size_high_Offset = Param.UInt16(
    #     0xE8, "Base offset of DVSEC CXL Range 1 Size High"
    # )

    # #DVSEC CXL Range 1 Size Low (Offset DCh)
    # Memory_Info_Valid1              = Param.Bool(0,"Memory_Info_Valid1")                                        #1bit
    # Memory_Active1                  = Param.Bool(0,"Memory_Active1")                                            #1bit
    # Media_Type1                     = Param.UInt8(0, "Media_Type1")  #0: volatile mem                             #3bit
    # Memory_Class1                   = Param.UInt8(0, "Memory_Class1")  #0: Memory Class(norm dram)                #3bit
    # Desired_Interleave1             = Param.UInt8(0, "Desired_Interleave1")                                       #5bit
    # Memory_Active_Timeout1          = Param.UInt8(0, "Memory_Active_Timeout1")                                    #3bit
    # Memory_Active_Degraded1         = Param.Bool(0,"Memory_Active_Degraded1")                                   #1bit
    # Reserved8                       = Param.UInt16(0, "Reserved")                                                  #11bit
    # Memory_Size_Low1                = Param.UInt8(0, "Memory_Size_Low1")                                          #4bit
    
    # DVSEC_CXL_Range1_Size_Low_Offset = Param.UInt8(
    #     0xEC, "Base offset of DVSEC CXL Range 1 Size Low"
    # )

    # #DVSEC CXL Range 1 Base High (Offset E0h)
    # Memory_Base_High1   = Param.Int(0, "Memory_Base_High1")                                                    #32bit
   
    # DVSEC_CXL_Range1_Base_high_Offset = Param.UInt16(
    #     0xE0, "Base offset of DVSEC CXL Range 1 Base High"
    # )

    # #DVSEC CXL Range 1 Base Low (Offset E4h)
    # Reserved9           = Param.UInt32(0, "Reserved")                                                              #28bit
    # Memory_Base_Low1    = Param.UInt8(0, "Memory_Base_Low1")                                                      #4bit
    
    # DVSEC_CXL_Range1_Base_Low_Offset = Param.UInt16(
    #     0xE4, "Base offset of DVSEC CXL Range 1 Base Low"
    # )

    # #DVSEC CXL Range 2 Size High (Offset E8h)
    # Memory_Size_High2   = Param.UInt32(32, "Memory_Size_High")                                                     #32bit

    # DVSEC_CXL_Range2_Size_high_Offset = Param.UInt16(
    #     0xE8, "Base offset of DVSEC CXL Range 2 Size High"
    # )

    # #DVSEC CXL Range 2 Size Low (Offset ECh)
    # Memory_Info_Valid2              = Param.Bool(0,"Memory_Info_Valid2")                                        #1bit
    # Memory_Active2                  = Param.Bool(0,"Memory_Active2")                                            #1bit
    # Media_Type2                     = Param.UInt8(0, "Media_Type2")                                               #3bit
    # Memory_Class2                   = Param.UInt8(0, "Memory_Class2")                                             #3bit
    # Desired_Interleave2             = Param.UInt8(0, "Desired_Interleave2")                                       #5bit
    # Memory_Active_Timeout2          = Param.UInt8(0, "Memory_Active_Timeout2")                                    #3bit
    # Memory_Active_Degraded2         = Param.Bool(0,"Memory_Active_Degraded2")                                   #1bit
    # Reserved10                      = Param.UInt16(0, "Reserved")                                                  #11bit
    # Memory_Size_Low2                = Param.UInt8(0, "Memory_Size_Low2")                                          #4bit

    # DVSEC_CXL_Range2_Size_Low_Offset = Param.UInt8(
    #     0xEC, "Base offset of DVSEC CXL Range 2 Size Low"
    # )

    # #DVSEC CXL Range 2 Base High (Offset F0h)
    # Memory_Base_High2   = Param.UInt32(0, "Memory_Base_High2")                                                     #32bit
   
    # DVSEC_CXL_Range2_Base_high_Offset = Param.UInt16(
    #     0xF0, "Base offset of DVSEC CXL Range 2 Base High"
    # )

    # #DVSEC CXL Range 2 Base Low (Offset F4h)
    # Reserved11          = Param.UInt32(0, "Reserved")                                                              #28bit
    # Memory_Base_Low2    = Param.UInt8(0, "Memory_Base_Low2")                                                      #4bit
    
    # DVSEC_CXL_Range2_Base_Low_Offset = Param.UInt16(
    #     0xFC, "Base offset of DVSEC CXL Range 2 Base Low"
    # )

    # #DVSEC CXL Capability3 (Offset F8h)
    # Default_Volatile_HDM_State_after_Cold_Reset         = Param.Bool(0,"Default_Volatile_HDM_State_after_Cold_Reset")           #1bit
    # Default_Volatile_HDM_State_after_Warm_Reset         = Param.Bool(0,"Default_Volatile_HDM_State_after_Warm_Reset")           #1bit
    # Default_Volatile_HDM_State_after_Hot_Reset          = Param.Bool(0,"Default_Volatile_HDM_State_after_Hot_Reset")            #1bit
    # Volatile_HDM_State_after_Hot_Reset_Configurability  = Param.Bool(0,"Volatile_HDM_State_after_Hot_Reset_Configurability")    #1bit
    # Direc_P2P_Mem_Capable                               = Param.Bool(0,"Direc_P2P_Mem_Capable")                                 #1bit
    # Reserved12                                          = Param.UInt16(0, "Reserved")                                              #11bit

    # DVSEC_CXL_Capability3_Offset = Param.UInt16(
    #     0xF8, "Base offset of DVSEC CXL Capability3"
    # )