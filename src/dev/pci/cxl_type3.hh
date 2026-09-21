#include "base/addr_range.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "params/cxl_type3.hh"
#include "dev/pci/device.hh"

#define CXL_CONFIG_SIZE 0xFF
#define PCIE_CXL_DEVICE_DVSEC_LENGTH 0x3C

#define CXL_VENDOR_ID 0x1e98

#define CXL_DVSEC_HEADER1   0x04
#define CXL_DVSEC_HEADER2   0x08
#define CXL_DVSEC_CAP   0x0A
#define CXL_DVSEC_CTRL  0x0C
#define CXL_DVSEC_STATUS    0x0E
#define CXL_DVSEC_CTRL2     0x10
#define CXL_DVSEC_STATUS2   0x12
#define CXL_DVSEC_LOCK      0x14
#define CXL_DVSEC_CAP2      0x16
#define CXL_DVSEC_RANGE1_SIZE_HI    0x18
#define CXL_DVSEC_RANGE1_SIZE_LO    0x1C
#define CXL_DVSEC_RANGE1_BASE_HI    0x20
#define CXL_DVSEC_RANGE1_BASE_LO    0x24
#define CXL_DVSEC_RANGE2_SIZE_HI    0x28
#define CXL_DVSEC_RANGE2_SIZE_LO    0x2C
#define CXL_DVSEC_RANGE2_BASE_HI    0x30
#define CXL_DVSEC_RANGE2_BASE_LO    0x34


namespace gem5
{
    typedef enum _INTERRUPT_MODE {
        INTERRUPT_PIN,
        INTERRUPT_MSI,
        INTERRUPT_MSIX
    }INTERRUPT_MODE;

    class cxl_type3 : public PciDevice {

        typedef struct DVSECHeader {
                uint32_t cap_hdr;
                uint32_t dv_hdr1;
                uint16_t dv_hdr2;
            }DVSECHeader;

        typedef union CXLDVSECDevice
        {
            uint8_t data[60];
            struct 
            {
                DVSECHeader hdr;
                uint16_t cap;
                uint16_t ctrl;
                uint16_t status;
                uint16_t ctrl2;
                uint16_t status2;
                uint16_t lock;
                uint16_t cap2;
                uint32_t range1_size_hi;
                uint32_t range1_size_lo;
                uint32_t range1_base_hi;
                uint32_t range1_base_lo;
                uint32_t range2_size_hi;
                uint32_t range2_size_lo;
                uint32_t range2_base_hi;
                uint32_t range2_base_lo;
            };
        }CXLDVSECDevice;

        protected:
            CXLDVSECDevice config;

            Tick pioDelay;
            Tick configDelay;
        private:

        Addr registerTableBaseAddress;
        int registerTableSize;

        /* Interrupt logics */
        // Pin based
        uint32_t interruptStatus;
        uint32_t oldInterruptStatus;

        // MSI/MSI-X
        uint16_t vectors;

        // MSI-X
        Addr tableBaseAddress;
        int tableSize;
        Addr pbaBaseAddress;
        int pbaSize;

        // Current Interrupt Mode
        INTERRUPT_MODE mode;

        // Stats
        //EventFunctionWrapper statUpdateEvent;
        statistics::Scalar *pStats;

        public:
            //AddrRangeList getAddrRanges() const override;
            
            Tick writeConfig(PacketPtr pkt) override; 
            Tick readConfig(PacketPtr pkt) override;

            using Params = cxl_type3Params;
            cxl_type3(const Params &p);

    };

} // namespace gem5