#include "base/trace.hh"
#include "dev/pci/cxl_type3.hh"
#include "debug/cxl_type3.hh"

namespace gem5
{
    cxl_type3::cxl_type3(const Params &p)
    : PciDevice(p)
    {
        //cxl config 
        config.hdr.cap_hdr = p.DVSECCapId;
        config.hdr.cap_hdr |= p.DVSECversion <<8;
        config.hdr.cap_hdr |= p.DVSECNextCapability <<16;

        config.hdr.dv_hdr1 = p.VendorID;
        config.hdr.dv_hdr1 |= p.Revision<<16;
        config.hdr.dv_hdr1 |= p.Length<<20;

        config.hdr.dv_hdr2 = htole(p.DeviceID);

        config.cap = p.Cache_Capable;
        config.cap |= p.IO_Capable<<1;
        config.cap |= p.Mem_Capable<<2;
        config.cap |= p.Mem_HwInit_Mode<<3;
        config.cap |= p.HDM_Count<<4;
        config.cap |= p.Cache_Writeback_Invalidate_Capable<<5;
        config.cap |= p.CXL_Reset_Capable<<6;
        config.cap |= p.CXL_Reset_Timeout<<7;
        config.cap |= p.CXL_Reset_Mem_Clr_Capable<<11;
        config.cap |= p.TSP_Capable<<12;
        config.cap |= p.Multiple_Logical_Device<<13;
        config.cap |= p.Viral_Capable<<14;
        config.cap |= p.PM_Init_Completion_Reporting_Capable<<15;

        config.ctrl = p.Cache_Enable;
        config.ctrl |= p.IO_Enable<<1;
        config.ctrl |= p.Mem_Enable<<2;
        config.ctrl |= p.Cache_SF_Coverage<<3;
        config.ctrl |= p.Cache_SF_Granularity<<8;
        config.ctrl |= p.Cache_Clean_Eviction<<11;
        config.ctrl |= p.Direct_P2P_Mem_Enable<<12;
        config.ctrl |= p.Reserved1<<13;
        config.ctrl |= p.Viral_Enable<<14;
        config.ctrl |= p.Reserved2<<15;

        config.status = p.Reserved3;
        config.status |= p.Viral_Status<<14;
        config.status |= p.Reserved4<<15;

        config.ctrl2 = p.Disable_Caching;
        config.ctrl2 |= p.Initiate_Cache_Write_Back_and_Invalidation<<1;
        config.ctrl2 |= p.Initiate_CXL_Reset<<2; 
        config.ctrl2 |= p.CXL_Reset_Mem_Clr_Enable<<3; 
        config.ctrl2 |= p.Desired_Volatile_HDM_State_after_Hot_Reset<<4; 
        config.ctrl2 |= p.Modified_Completion_Enable<<5; 
        config.ctrl2 |= p.Reserved5<<6; 

        config.status2 = p.Cache_Invalid;
        config.status2 |= p.CXL_Reset_Complete<<1;
        config.status2 |= p.CXL_Reset_Error<<2;
        config.status2 |= p.Volatile_HDM_Preservation_Error<<3;
        config.status2 |= p.Reserved6<<14;
        config.status2 |= p.Power_Management_Initialization_Complete<<15;

        config.lock = p.CONFIG_LOCK;
        config.lock |= p.Reserved7<<1;

        config.cap2 = p.Cache_Size_Unit;
        config.cap2 |= p.Fallback_Capability<<4;
        config.cap2 |= p.Modified_Completion_Capable<<6;
        config.cap2 |= p.No_Clean_Writeback<<7;
        config.cap2 |= p.Cache_Size<<8;

        config.range1_size_hi = htole(p.Memory_Size_High1);

        config.range1_size_lo = p.Memory_Info_Valid1;
        config.range1_size_lo |= p.Memory_Active1<<1;
        config.range1_size_lo |= p.Media_Type1<<2;
        config.range1_size_lo |= p.Memory_Class1<<5;
        config.range1_size_lo |= p.Desired_Interleave1<<8;
        config.range1_size_lo |= p.Memory_Active_Timeout1<<13;
        config.range1_size_lo |= p.Memory_Active_Degraded1<<16;
        config.range1_size_lo |= p.Reserved8<<17;
        config.range1_size_lo |= p.Memory_Size_Low1<<28;

        config.range1_base_hi = htole(p.Memory_Base_High1);

        config.range1_base_lo = p.Reserved9;
        config.range1_base_lo |= p.Memory_Base_Low1<<28;
        
        config.range2_size_hi = htole(p.Memory_Size_High2);

        config.range2_size_lo = p.Memory_Info_Valid2;
        config.range2_size_lo |= p.Memory_Active2<<1;
        config.range2_size_lo |= p.Media_Type2<<2;
        config.range2_size_lo |= p.Memory_Class2<<5;
        config.range2_size_lo |= p.Desired_Interleave2<<8;
        config.range2_size_lo |= p.Memory_Active_Timeout2<<13;
        config.range2_size_lo |= p.Memory_Active_Degraded2<<16;
        config.range2_size_lo |= p.Reserved10<<17;
        config.range2_size_lo |= p.Memory_Size_Low2<<28;

        config.range2_base_hi = htole(p.Memory_Base_High2);

        config.range2_base_lo = p.Reserved11;
        config.range2_base_lo |= p.Memory_Base_Low2<<28;
    }

    Tick cxl_type3::readConfig(PacketPtr pkt){
        int offset = pkt->getAddr() & CXL_CONFIG_SIZE;
        int size = pkt->getSize();

        if (offset < PCI_DEVICE_SPECIFIC) {
            return PciDevice::readConfig(pkt);
        }
        else {
            // Read on PCI capabilities
            uint32_t val = 0;
            for (int i = 0; i < size; i++) {
            if (offset + i >= PMCAP_BASE && offset + i < PMCAP_BASE + PMCAP_SIZE) {
                val |= (uint32_t)pmcap.data[offset + i - PMCAP_BASE] << (i * 8);
            }
            else if (offset + i >= MSICAP_BASE &&
                    offset + i < MSICAP_BASE + MSICAP_SIZE) {
                val |= (uint32_t)msicap.data[offset + i - MSICAP_BASE] << (i * 8);
            }
            else if (offset + i >= MSIXCAP_BASE &&
                    offset + i < MSIXCAP_BASE + MSIXCAP_SIZE) {
                val |= (uint32_t)msixcap.data[offset + i - MSIXCAP_BASE] << (i * 8);
            }
            else if (offset + i >= PXCAP_BASE &&
                    offset + i < PXCAP_BASE + PXCAP_SIZE) {
                val |= (uint32_t)pxcap.data[offset + i - PXCAP_BASE] << (i * 8);
            }
            else {
                DPRINTF(cxl_type3,"CXL_type3_interface: Invalid PCI config read offset: %#x",offset);
            }
            }

            switch (size) {
            case sizeof(uint8_t):
                pkt->setLE<uint8_t>(val);
                break;
            case sizeof(uint16_t):
                pkt->setLE<uint16_t>(val);
                break;
            case sizeof(uint32_t):
                pkt->setLE<uint32_t>(val);
                break;
            default:
                DPRINTF(cxl_type3,"CXL_type3_interface: Invalid PCI config read size: %d",size);
                break;
            }

            pkt->makeAtomicResponse();
        }
        return configDelay;
    }

    Tick cxl_type3::writeConfig(PacketPtr pkt){
        int offset = pkt->getAddr() & PCI_CONFIG_SIZE;
        int size = pkt->getSize();
        uint32_t val = 0;

        if (offset < PCI_DEVICE_SPECIFIC) {
            PciDevice::writeConfig(pkt);
        
            // Updates on BAR0/1 address
            if (offset == PCI0_BASE_ADDR0 || offset == PCI0_BASE_ADDR1) {
                registerTableBaseAddress = (uint64_t)BARs[0] | ((uint64_t)BARs[1] << 32);
                registerTableSize = (uint64_t)BARs[0];
            }
            else if (offset == PCI0_BASE_ADDR4) {
                tableBaseAddress = (uint64_t)BARs[4];
                tableSize = (uint64_t)BARs[4];
            }
            else if (offset == PCI0_BASE_ADDR5) {
                pbaBaseAddress = (uint64_t)BARs[5];
                pbaSize = (uint64_t)BARs[5];
            }
        }
        else {
            // Write on PCI capabilities
            if (offset == PMCAP_BASE + 4 && size == sizeof(uint16_t)) {  // PMCAP Control Status
                val = pkt->getLE<uint16_t>();

                if (val & 0x8000) {
                    pmcap.pmcs &= 0x7F00;  // Clear PMES
                }
                pmcap.pmcs &= ~0x1F03;
                pmcap.pmcs |= (val & 0x1F03);
            }
            else if (offset == MSICAP_BASE + 2 && size == sizeof(uint16_t)) {  // MSICAP Message Control
                val = pkt->getLE<uint16_t>();

                mode = (val & 0x0001) ? INTERRUPT_MSI : INTERRUPT_PIN;

                msicap.mc &= ~0x0071;
                msicap.mc |= (val & 0x0071);

                vectors = (uint16_t)powf(2, (msicap.mc & 0x0070) >> 4);

                DPRINTF(cxl_type3,"INTR    | MSI %s | %d vectors",mode == INTERRUPT_PIN ? "disabled" : "enabled", vectors);
            }
            else if (offset == MSICAP_BASE + 4 && size == sizeof(uint32_t)) {  // MSICAP Message Address
                msicap.ma = pkt->getLE<uint32_t>() & 0xFFFFFFFC;
            }
            else if (offset == MSICAP_BASE + 8 && size == sizeof(uint32_t)) {  // MSICAP Message Upper Address
                msicap.mua = pkt->getLE<uint32_t>();
            }
            else if (offset == MSICAP_BASE + 12 && size >= sizeof(uint16_t)) {  // MSICAP Message Data
                msicap.md = pkt->getLE<uint16_t>();
            }
            else if (offset == MSICAP_BASE + 16 && size == sizeof(uint32_t)) {  // MSICAP Interrupt Mask Bits
                msicap.mmask = pkt->getLE<uint32_t>();
            }
            else if (offset == MSICAP_BASE + 20 && size == sizeof(uint32_t)) {  // MSICAP Interrupt Pending Bits
                msicap.mpend = pkt->getLE<uint32_t>();
            }
            else if (offset == MSIXCAP_BASE + 2 && size == sizeof(uint16_t)) {  // MSIXCAP Message Control
                val = pkt->getLE<uint16_t>();

                mode = (val & 0x8000) ? INTERRUPT_MSIX : INTERRUPT_PIN;

                msixcap.mxc &= ~0xC000;
                msixcap.mxc |= (val & 0xC000);

                vectors = (msixcap.mxc & 0x07FF) + 1;

                DPRINTF(cxl_type3,"INTR    | MSI-X %s | %d vectors",mode == INTERRUPT_PIN ? "disabled" : "enabled", vectors);
            }
            else if (offset == PXCAP_BASE + 8 && size == sizeof(uint16_t)) {  // PXCAP Device Capabilities
                pxcap.pxdc = pkt->getLE<uint16_t>();
            }
            else if (offset == PXCAP_BASE + 10 && size == sizeof(uint16_t)) {  // PXCAP Device Status
                val = pkt->getLE<uint16_t>();

                if (val & 0x0001) {
                    pxcap.pxds &= 0xFFFE;
                }
                if (val & 0x0002) {
                    pxcap.pxds &= 0xFFFD;
                }
                if (val & 0x0004) {
                    pxcap.pxds &= 0xFFFB;
                }
                if (val & 0x0008) {
                    pxcap.pxds &= 0xFFF7;
                }
            }
            else if (offset == PXCAP_BASE + 16 && size == sizeof(uint16_t)) {  // PXCAP Link Control
                pxcap.pxlc = pkt->getLE<uint16_t>();
            }
            else if (offset == PXCAP_BASE + 18 && size == sizeof(uint16_t)) {  // PXCAP Link Status
                val = pkt->getLE<uint16_t>();

                if (val & 0x4000) {
                    pxcap.pxls &= 0xBFFF;
                }
                if (val & 0x8000) {
                    pxcap.pxls &= 0x7FFF;
                }
            }
            else if (offset == PXCAP_BASE + 40 && size == sizeof(uint32_t)) {  // PXCAP Device Control 2
                pxcap.pxdc2 = pkt->getLE<uint32_t>();
            }





            else {
                assert(0);
            DPRINTF(cxl_type3,"CXL_type3_interface: Invalid PCI config write offset: %#x size: %d",offset, size);
            }

            pkt->makeAtomicResponse();
        }

        return configDelay;
    }

}

