#include "dev/cxl_dramsim3.hh"

#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/CXLDRAMsim3.hh"
#include "debug/Drain.hh"
#include "mem/abstract_mem.hh"
#include "mem/protocol/timing.hh"
#include "sim/byteswap.hh"
#include "sim/eventq.hh"
#include "sim/system.hh"

#define CXL_DVSEC_SIZE 0x50

#if defined(__APPLE__) || defined(__FreeBSD__)
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#endif

namespace
{
constexpr int CXL_DEVICE_DVSEC_BASE = 0x100;
constexpr int CXL_DEVICE_DVSEC_SIZE = 0x3c;
constexpr int CXL_REG_LOCATOR_DVSEC_BASE = 0x140;
constexpr int CXL_REG_LOCATOR_DVSEC_SIZE = 0x1c;
constexpr uint32_t CXL_COMPONENT_REG_OFFSET = 0x0;
constexpr uint32_t CXL_MEMDEV_REG_OFFSET = 0x10000;
constexpr uint32_t CXL_COMPONENT_REG_SIZE = 0x10000;
constexpr uint32_t CXL_MEMDEV_REG_SIZE = 0x10000;
constexpr uint32_t CXL_CM_OFFSET = 0x1000;
constexpr uint32_t CXL_CM_HDM_REL_OFFSET = 0x100;
constexpr uint32_t CXL_HDM_OFFSET = CXL_CM_OFFSET + CXL_CM_HDM_REL_OFFSET;
constexpr uint32_t CXL_HDM_GLOBAL_CTRL = CXL_HDM_OFFSET + 0x4;
constexpr uint32_t CXL_HDM_DECODER0_CTRL = CXL_HDM_OFFSET + 0x20;
constexpr uint32_t CXL_HDM_DECODER0_COMMIT = 1u << 9;
constexpr uint32_t CXL_HDM_DECODER0_COMMITTED = 1u << 10;
constexpr uint32_t CXL_HDM_DECODER0_COMMIT_ERROR = 1u << 11;
constexpr uint64_t CXL_MEMDEV_STATUS_MEDIA_READY = 1ULL << 2;
constexpr uint64_t CXL_MEMDEV_STATUS_MBOX_READY = 1ULL << 4;
constexpr uint64_t CXL_MEMDEV_STATUS_READY =
    CXL_MEMDEV_STATUS_MEDIA_READY | CXL_MEMDEV_STATUS_MBOX_READY;
constexpr uint32_t CXLDEV_STATUS_OFFSET = 0x40;
constexpr uint32_t CXLDEV_MBOX_OFFSET = 0x80;
constexpr uint32_t CXLDEV_MEMDEV_OFFSET = 0x400;
constexpr uint32_t CXLDEV_MBOX_CAPS_OFFSET = CXLDEV_MBOX_OFFSET + 0x00;
constexpr uint32_t CXLDEV_MBOX_CTRL_OFFSET = CXLDEV_MBOX_OFFSET + 0x04;
constexpr uint32_t CXLDEV_MBOX_CMD_OFFSET = CXLDEV_MBOX_OFFSET + 0x08;
constexpr uint32_t CXLDEV_MBOX_STATUS_OFFSET = CXLDEV_MBOX_OFFSET + 0x10;
constexpr uint32_t CXLDEV_MBOX_PAYLOAD_OFFSET = CXLDEV_MBOX_OFFSET + 0x20;
constexpr uint32_t CXLDEV_MBOX_PAYLOAD_SIZE = 256;
constexpr uint16_t CXL_MBOX_OP_GET_EVENT_RECORD = 0x0100;
constexpr uint16_t CXL_MBOX_OP_CLEAR_EVENT_RECORD = 0x0101;
constexpr uint16_t CXL_MBOX_OP_GET_EVT_INT_POLICY = 0x0102;
constexpr uint16_t CXL_MBOX_OP_SET_EVT_INT_POLICY = 0x0103;
constexpr uint16_t CXL_MBOX_OP_SET_TIMESTAMP = 0x0301;
constexpr uint16_t CXL_MBOX_OP_GET_SUPPORTED_LOGS = 0x0400;
constexpr uint16_t CXL_MBOX_OP_GET_LOG = 0x0401;
constexpr uint16_t CXL_MBOX_OP_IDENTIFY = 0x4000;
constexpr uint16_t CXL_MBOX_OP_GET_PARTITION_INFO = 0x4100;
constexpr uint16_t CXL_MBOX_OP_GET_LSA = 0x4102;
constexpr uint16_t CXL_MBOX_RC_SUCCESS = 0;
constexpr uint16_t CXL_MBOX_RC_UNSUPPORTED = 3;
constexpr uint16_t CXL_MBOX_RC_INPUT = 2;
constexpr uint16_t CXL_MBOX_RC_LOG = 23;
constexpr uint64_t CXL_CAPACITY_MULTIPLIER = 256ULL * 1024ULL * 1024ULL;
constexpr uint8_t CXL_CEL_UUID[16] = {
    0x0d, 0xa9, 0xc0, 0xb5, 0xbf, 0x41, 0x4b, 0x78,
    0x8f, 0x79, 0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17,
};

uint32_t
makePcieExtCapHeader(uint16_t cap_id, uint8_t version, uint16_t next)
{
    return (uint32_t)cap_id | ((uint32_t)(version & 0xf) << 16) |
        ((uint32_t)(next & 0xfff) << 20);
}

uint32_t
makeDvsecHeader1(uint16_t vendor_id, uint8_t revision, uint16_t length)
{
    return (uint32_t)vendor_id | ((uint32_t)(revision & 0xf) << 16) |
        ((uint32_t)(length & 0xfff) << 20);
}

uint32_t
makeRegLocatorLow(uint8_t bar, uint8_t rbi, uint64_t offset)
{
    return (uint32_t)(bar & 0x7) | ((uint32_t)rbi << 8) |
        ((uint32_t)offset & 0xffff0000);
}

uint32_t
makeRegLocatorHigh(uint64_t offset)
{
    return (uint32_t)(offset >> 32);
}

uint32_t
cxlPciConfigOffset(uint64_t addr)
{
    return addr & PCIE_CONFIG_SIZE;
}

template <size_t N>
void
writeLe(std::array<uint8_t, N> &regs, uint32_t offset, uint64_t value,
        unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        regs[offset + i] = (value >> (8 * i)) & 0xff;
}

template <size_t N>
uint64_t
readLe(const std::array<uint8_t, N> &regs, uint32_t offset, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= (uint64_t)regs[offset + i] << (8 * i);
    return value;
}

template <size_t N>
uint32_t
readLe32(const std::array<uint8_t, N> &regs, uint32_t offset)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)regs[offset + i] << (8 * i);
    return value;
}

void
appendLe(std::vector<uint8_t> &payload, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        payload.push_back((value >> (8 * i)) & 0xff);
}

void
appendZeros(std::vector<uint8_t> &payload, unsigned count)
{
    payload.insert(payload.end(), count, 0);
}
} // anonymous namespace

namespace gem5
{
    /// esj 2024-04-27
    std::mutex mutex;
    std::condition_variable condition;
    ///
    CXLDRAMsim3::CXLDRAMsim3(const Param &p)
        : PciDevice(p), //cxl_port(this),
        mem_(RangeSize(0x0, p.cxl_mem_size), *this),
        latency_(p.latency),
        cxl_mem_latency_(p.cxl_mem_latency),
        cxl_mem_size(p.cxl_mem_size),
        cxl_mem_start(p.cxl_mem_start),
        cxlChbsBase(p.cxl_chbs_base),
        cxlChbsSize(p.cxl_chbs_size),
        cxlLogicalDeviceCount(std::min<uint32_t>(
            Packet::MaxPciRequesterIds,
            std::max<uint32_t>(1, p.cxl_logical_device_count))),
        hostVisibleCxlMemSizes(p.host_visible_cxl_mem_sizes),
        hostCxlDeviceOffsets(p.host_cxl_device_offsets),
        read_cb(std::bind(&CXLDRAMsim3::readComplete, this, 0,
                          std::placeholders::_1)),
        write_cb(std::bind(&CXLDRAMsim3::writeComplete, this, 0,
                           std::placeholders::_1)),
        //esj 2025-02-02
        read_cb_write(std::bind(&CXLDRAMsim3::readComplete_write, this, 1,
                                std::placeholders::_1)),
        write_cb_write(std::bind(&CXLDRAMsim3::writeComplete_write, this, 1,
                                 std::placeholders::_1)),

        wrapper(p.configFile, p.filePath, read_cb, write_cb),
        wrapper_write(p.configFile_write, p.filePath, read_cb_write, write_cb_write),
        retryReq(false), retryResp(false), startTick(0),
        nbrOutstandingReads(0), nbrOutstandingWrites(0),
        sendResponseEvent([this]{ sendResponse(); }, name()),
        tickEvent([this]{ tick(); }, name()),
        stats(*this), //esj 2024-07-26
        _system(p.system),
        switch_mode(p.switch_mode), //esj 2024-10-02
        Pio2(name() + ".Pio2", *this) //esj 2025-06-22
        {
            std::memset(&cxl_config, 0, sizeof(cxl_config));
            std::memset(&cxl_config_locator, 0, sizeof(cxl_config_locator));
            std::memset(&cxl_config_header, 0, sizeof(cxl_config_header));
            vectors = 1;
            mode = INTERRUPT_PIN;
            hostPciConfigs.fill(config);
            for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds;
                 ++host_idx) {
                hostCxlComponentRegs[host_idx].fill(0);
                hostCxlMemdevRegs[host_idx].fill(0);
                hostCxlEventInterruptPolicies[host_idx].fill(0);
                for (std::size_t bar_idx = 0;
                     bar_idx < hostPciBarRanges[host_idx].size();
                     ++bar_idx) {
                    hostPciBarRanges[host_idx][bar_idx] = {};
                }
            }

            //esj cxl fixed mode option 2025-11-24
            // if (p.switch_mode){
            //     trace::cxl_fixed_mode = true;
            // }

            //esj 2025-02-04
            //cxl config
            // cxl_config.hdr.cap_hdr = (uint32_t)(p.DVSECCapId);
            // cxl_config.hdr.cap_hdr |= (uint32_t)(p.DVSECversion) <<16;
            // cxl_config.hdr.cap_hdr |= (uint32_t)(p.DVSECNextCapability) <<24;

            // cxl_config.hdr.dv_hdr1 = (uint32_t)(p.VendorID2);
            // cxl_config.hdr.dv_hdr1 |= (uint32_t)(p.Revision2)<<16;
            // cxl_config.hdr.dv_hdr1 |= (uint32_t)(p.Length)<<20;

            // cxl_config.hdr.dv_hdr2 = (uint16_t)(p.DeviceID);

            cxl_config.cap_hdr = makePcieExtCapHeader(p.DVSECCapId,
                    p.DVSECversion, CXL_REG_LOCATOR_DVSEC_BASE);

            cxl_config.dv_hdr1 = makeDvsecHeader1(p.VendorID2,
                    p.Revision2, CXL_DEVICE_DVSEC_SIZE);

            cxl_config.dv_hdr2 = (uint16_t)(p.DeviceID);


            cxl_config.cap = (uint16_t)(p.Cache_Capable);
            cxl_config.cap |= (uint16_t)(p.IO_Capable)<<1;
            cxl_config.cap |= (uint16_t)(p.Mem_Capable)<<2;
            cxl_config.cap |= (uint16_t)(p.Mem_HwInit_Mode)<<3;
            cxl_config.cap |= (uint16_t)(p.HDM_Count)<<4;
            cxl_config.cap |= (uint16_t)(p.Cache_Writeback_Invalidate_Capable)<<6;
            cxl_config.cap |= (uint16_t)(p.CXL_Reset_Capable)<<7;
            cxl_config.cap |= (uint16_t)(p.CXL_Reset_Timeout)<<8;
            cxl_config.cap |= (uint16_t)(p.CXL_Reset_Mem_Clr_Capable)<<11;
            cxl_config.cap |= (uint16_t)(p.TSP_Capable)<<12;
            cxl_config.cap |= (uint16_t)(p.Multiple_Logical_Device)<<13;
            cxl_config.cap |= (uint16_t)(p.Viral_Capable)<<14;
            cxl_config.cap |= (uint16_t)(p.PM_Init_Completion_Reporting_Capable)<<15;

            cxl_config.ctrl = (uint16_t)(p.Cache_Enable);
            cxl_config.ctrl |= (uint16_t)(p.IO_Enable)<<1;
            cxl_config.ctrl |= (uint16_t)(p.Mem_Enable)<<2;
            cxl_config.ctrl |= (uint16_t)(p.Cache_SF_Coverage)<<3;
            cxl_config.ctrl |= (uint16_t)(p.Cache_SF_Granularity)<<8;
            cxl_config.ctrl |= (uint16_t)(p.Cache_Clean_Eviction)<<11;
            cxl_config.ctrl |= (uint16_t)(p.Direct_P2P_Mem_Enable)<<12;
            cxl_config.ctrl |= (uint16_t)(p.Reserved1)<<13;
            cxl_config.ctrl |= (uint16_t)(p.Viral_Enable)<<14;
            cxl_config.ctrl |= (uint16_t)(p.Reserved2)<<15;

            cxl_config.status = (uint16_t)(p.Reserved3);
            cxl_config.status |= (uint16_t)(p.Viral_Status)<<14;
            cxl_config.status |= (uint16_t)(p.Reserved4)<<15;

            cxl_config.ctrl2 = (uint16_t)(p.Disable_Caching);
            cxl_config.ctrl2 |= (uint16_t)(p.Initiate_Cache_Write_Back_and_Invalidation)<<1;
            cxl_config.ctrl2 |= (uint16_t)(p.Initiate_CXL_Reset)<<2;
            cxl_config.ctrl2 |= (uint16_t)(p.CXL_Reset_Mem_Clr_Enable)<<3;
            cxl_config.ctrl2 |=
                (uint16_t)(p.Desired_Volatile_HDM_State_after_Hot_Reset)<<4;
            cxl_config.ctrl2 |= (uint16_t)(p.Modified_Completion_Enable)<<5;
            cxl_config.ctrl2 |= (uint16_t)(p.Reserved5)<<6;

            cxl_config.status2 = (uint16_t)(p.Cache_Invalid);
            cxl_config.status2 |= (uint16_t)(p.CXL_Reset_Complete)<<1;
            cxl_config.status2 |= (uint16_t)(p.CXL_Reset_Error)<<2;
            cxl_config.status2 |= (uint16_t)(p.Volatile_HDM_Preservation_Error)<<3;
            cxl_config.status2 |= (uint16_t)(p.Reserved6)<<14;
            cxl_config.status2 |= (uint16_t)(p.Power_Management_Initialization_Complete)<<15;

            cxl_config.lock = (uint16_t)(p.CONFIG_LOCK);
            cxl_config.lock |= (uint16_t)(p.Reserved7)<<1;

            cxl_config.cap2 = (uint16_t)(p.Cache_Size_Unit);
            cxl_config.cap2 |= (uint16_t)(p.Fallback_Capability)<<4;
            cxl_config.cap2 |= (uint16_t)(p.Modified_Completion_Capable)<<6;
            cxl_config.cap2 |= (uint16_t)(p.No_Clean_Writeback)<<7;
            cxl_config.cap2 |= (uint16_t)(p.Cache_Size)<<8;

            cxl_config.range1_size_hi = (uint32_t)(p.Memory_Size_High1);

            cxl_config.range1_size_lo = (uint32_t)(p.Memory_Info_Valid1);
            cxl_config.range1_size_lo |= (uint32_t)(p.Memory_Active1)<<1;
            cxl_config.range1_size_lo |= (uint32_t)(p.Media_Type1)<<2;
            cxl_config.range1_size_lo |= (uint32_t)(p.Memory_Class1)<<5;
            cxl_config.range1_size_lo |= (uint32_t)(p.Desired_Interleave1)<<8;
            cxl_config.range1_size_lo |= (uint32_t)(p.Memory_Active_Timeout1)<<13;
            cxl_config.range1_size_lo |= (uint32_t)(p.Memory_Active_Degraded1)<<16;
            cxl_config.range1_size_lo |= (uint32_t)(p.Reserved8)<<17;
            cxl_config.range1_size_lo |= (uint32_t)(p.Memory_Size_Low1)<<28;

            cxl_config.range1_base_hi = (uint32_t)(p.Memory_Base_High1);

            cxl_config.range1_base_lo = (uint32_t)(p.Reserved9);
            cxl_config.range1_base_lo |= (uint32_t)(p.Memory_Base_Low1)<<28;

            if (cxl_mem_size != 0)
                applyCxlDvsecMemRange(cxl_config, cxl_mem_size);

            cxl_config.range2_size_hi = (uint32_t)(p.Memory_Size_High2);

            cxl_config.range2_size_lo = (uint32_t)(p.Memory_Info_Valid2);
            cxl_config.range2_size_lo |= (uint32_t)(p.Memory_Active2)<<1;
            cxl_config.range2_size_lo |= (uint32_t)(p.Media_Type2)<<2;
            cxl_config.range2_size_lo |= (uint32_t)(p.Memory_Class2)<<5;
            cxl_config.range2_size_lo |= (uint32_t)(p.Desired_Interleave2)<<8;
            cxl_config.range2_size_lo |= (uint32_t)(p.Memory_Active_Timeout2)<<13;
            cxl_config.range2_size_lo |= (uint32_t)(p.Memory_Active_Degraded2)<<16;
            cxl_config.range2_size_lo |= (uint32_t)(p.Reserved10)<<17;
            cxl_config.range2_size_lo |= (uint32_t)(p.Memory_Size_Low2)<<28;

            cxl_config.range2_base_hi = (uint32_t)(p.Memory_Base_High2);

            cxl_config.range2_base_lo = (uint32_t)(p.Reserved11);
            cxl_config.range2_base_lo |= (uint32_t)(p.Memory_Base_Low2)<<28;

            cxl_config.cap3 = (uint16_t)(p.Default_Volatile_HDM_State_after_Cold_Reset);
            cxl_config.cap3 |= (uint16_t)(p.Default_Volatile_HDM_State_after_Warm_Reset)<<1;
            cxl_config.cap3 |= (uint16_t)(p.Default_Volatile_HDM_State_after_Hot_Reset)<<2;
            cxl_config.cap3 |= (uint16_t)(p.Volatile_HDM_State_after_Hot_Reset_Configurability)<<3;
            cxl_config.cap3 |= (uint16_t)(p.Direc_P2P_Mem_Capable)<<4;


            //esj 2025-05-01
            cxl_config_locator.cap_hdr = makePcieExtCapHeader(p.DVSECCapId2,
                    p.DVSECversion2, 0);
            cxl_config_locator.dv_hdr1 = makeDvsecHeader1(p.REGBLK_VendorID2,
                    p.REGBLK_Revision2, CXL_REG_LOCATOR_DVSEC_SIZE);
            cxl_config_locator.dv_hdr2 = (uint32_t)(p.REGBLK_DeviceID);
            cxl_config_locator.REGBLK1_LOW = makeRegLocatorLow(0, 1,
                    CXL_COMPONENT_REG_OFFSET);
            cxl_config_locator.REGBLK1_HIGH =
                makeRegLocatorHigh(CXL_COMPONENT_REG_OFFSET);
            cxl_config_locator.REGBLK2_LOW = makeRegLocatorLow(0, 3,
                    CXL_MEMDEV_REG_OFFSET);
            cxl_config_locator.REGBLK2_HIGH =
                makeRegLocatorHigh(CXL_MEMDEV_REG_OFFSET);
            cxl_config_locator.REGBLK3_LOW = (uint32_t)(p.REGBLK3_LOW);
            cxl_config_locator.REGBLK3_HIGH = (uint32_t)(p.REGBLK3_HIGH);

            for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds;
                 ++host_idx) {
                auto &host_caps = hostPciCapabilityStates[host_idx];
                host_caps.cxl_config = cxl_config;
                applyCxlDvsecMemRange(host_caps.cxl_config,
                                      hostVisibleCxlMemSize(host_idx));
                host_caps.cxl_config_locator = cxl_config_locator;
                host_caps.pmcap = pmcap;
                host_caps.msicap = msicap;
                host_caps.msixcap = msixcap;
                host_caps.pxcap = pxcap;
                host_caps.vectors = vectors;
                host_caps.mode = mode;
                initCxlRegisterBlocks(hostCxlComponentRegs[host_idx],
                                      hostCxlMemdevRegs[host_idx]);
            }

            DPRINTF(CXLDRAMsim3,
                    "Instantiated DRAMsim3 clock=%d ns queue=%d\n",
                    wrapper.clockPeriod(), wrapper.queueSize());
            cxl=true;
            // Register a callback to compensate for the destructor not
            // being called. The callback prints the DRAMsim3 stats.
            registerExitCallback([this]() { wrapper.printStats(); });
            //esj 2025-02-01
            registerExitCallback([this]() { wrapper_write.printStats(); });

            auto register_extra_host = [&](PciHost *host_ptr) {
                if (host_ptr) {
                    host_ptr->registerDevice(this, _busAddr,
                                             (PciIntPin)p.InterruptPin);
                }
            };

            if (p.host_second && p.host_second != p.host) {
                register_extra_host(p.host_second);
            }
            if (p.host_third && p.host_third != p.host &&
                p.host_third != p.host_second) {
                register_extra_host(p.host_third);
            }
            if (p.host_fourth && p.host_fourth != p.host &&
                p.host_fourth != p.host_second &&
                p.host_fourth != p.host_third) {
                register_extra_host(p.host_fourth);
            }
        }

    void
    CXLDRAMsim3::Memory::change_addr(PacketPtr pkt){
        if(pkt->cxl_flag){
            Addr temp = pkt->getAddr();
            DPRINTF(CXLDRAMsim3, "%s  after pkt = %0x\n",__func__,pkt->origin_dest);
            pkt->setAddr(pkt->origin_dest);
            pkt->origin_dest = temp;
            DPRINTF(CXLDRAMsim3, "%s  before pkt = %0x\n",__func__,pkt->origin_dest);
        }
    }
    void
    CXLDRAMsim3::change_addr(PacketPtr pkt){
        if(pkt->cxl_flag){
            Addr temp = pkt->getAddr();
            DPRINTF(CXLDRAMsim3, "%s  after pkt = %0x\n",__func__,pkt->origin_dest);
            pkt->setAddr(pkt->origin_dest);
            pkt->origin_dest = temp;
            DPRINTF(CXLDRAMsim3, "%s  before pkt = %0x\n",__func__,pkt->origin_dest);
        }
    }

    uint64_t
    CXLDRAMsim3::hostVisibleCxlMemSize(uint8_t host_idx) const
    {
        host_idx = std::min<uint8_t>(host_idx, Packet::MaxPciRequesterIds - 1);
        if (host_idx >= cxlLogicalDeviceCount)
            return cxl_mem_size;

        if (host_idx < hostVisibleCxlMemSizes.size() &&
            hostVisibleCxlMemSizes[host_idx] != 0)
            return hostVisibleCxlMemSizes[host_idx];

        return cxl_mem_size;
    }

    void
    CXLDRAMsim3::applyCxlDvsecMemRange(CXLDVSECDevice &dvsec,
                                       uint64_t visible_size) const
    {
        if (visible_size == 0)
            visible_size = cxl_mem_size;

        dvsec.range1_size_hi = (uint32_t)(visible_size >> 32);
        dvsec.range1_size_lo &= ~0xf0000000u;
        dvsec.range1_size_lo |= (uint32_t)(visible_size & 0xf0000000u);
        dvsec.range1_base_hi = (uint32_t)(cxl_mem_start >> 32);
        dvsec.range1_base_lo &= ~0xf0000000u;
        dvsec.range1_base_lo |= (uint32_t)(cxl_mem_start & 0xf0000000u);
    }

    void
    CXLDRAMsim3::initCxlRegisterBlocks(CxlRegisterBlock &component_regs,
                                       CxlRegisterBlock &memdev_regs)
    {
        writeLe(component_regs, CXL_CM_OFFSET + 0x0,
                0x1 | (0x1 << 16) | (0x1 << 20) | (0x1 << 24), 4);
        writeLe(component_regs, CXL_CM_OFFSET + 0x4,
                0x5 | (0x1 << 16) | (CXL_CM_HDM_REL_OFFSET << 20), 4);
        writeLe(component_regs, CXL_HDM_OFFSET + 0x0, 0x0, 4);
        writeLe(component_regs, CXL_HDM_GLOBAL_CTRL, 0x0, 4);

        writeLe(memdev_regs, 0x0, 3ULL << 32, 8);
        writeLe(memdev_regs, 0x10, 0x1, 4);
        writeLe(memdev_regs, 0x14, CXLDEV_STATUS_OFFSET, 4);
        writeLe(memdev_regs, 0x18, 0x10, 4);
        writeLe(memdev_regs, 0x20, 0x2, 4);
        writeLe(memdev_regs, 0x24, CXLDEV_MBOX_OFFSET, 4);
        writeLe(memdev_regs, 0x28, CXLDEV_MBOX_PAYLOAD_OFFSET +
                CXLDEV_MBOX_PAYLOAD_SIZE - CXLDEV_MBOX_OFFSET, 4);
        writeLe(memdev_regs, 0x30, 0x4000, 4);
        writeLe(memdev_regs, 0x34, CXLDEV_MEMDEV_OFFSET, 4);
        writeLe(memdev_regs, 0x38, 0x80, 4);
        writeLe(memdev_regs, CXLDEV_MBOX_CAPS_OFFSET, 8, 4);
        writeLe(memdev_regs, CXLDEV_MEMDEV_OFFSET,
                CXL_MEMDEV_STATUS_READY, 8);
    }

    bool
    CXLDRAMsim3::hostBarOffset(uint8_t host_idx, Addr addr, int &bar,
                               Addr &offset) const
    {
        host_idx = std::min<uint8_t>(host_idx, Packet::MaxPciRequesterIds - 1);

        const auto &ranges = hostPciBarRanges[host_idx];
        for (std::size_t bar_idx = 0; bar_idx < ranges.size(); ++bar_idx) {
            const auto &range = ranges[bar_idx];
            if (!range.valid || range.size == 0)
                continue;

            const Addr end = range.base + range.size;
            if (addr >= range.base && addr < end) {
                bar = static_cast<int>(bar_idx);
                offset = addr - range.base;
                return true;
            }
        }

        return false;
    }

    void
    CXLDRAMsim3::snapshotHostBarRanges(uint8_t host_idx)
    {
        host_idx = std::min<uint8_t>(host_idx, Packet::MaxPciRequesterIds - 1);

        for (std::size_t bar_idx = 0; bar_idx < BARs.size(); ++bar_idx) {
            auto &range = hostPciBarRanges[host_idx][bar_idx];
            range = {};

            auto *bar = BARs[bar_idx];
            if (!bar || bar->size() == 0 || bar->addr() == 0)
                continue;

            range.base = bar->addr();
            range.size = bar->size();
            range.valid = true;
        }
    }

    bool
    CXLDRAMsim3::cxlRegisterOffset(uint8_t host_idx, Addr addr, bool &memdev,
                                   Addr &offset)
    {
        int bar = -1;
        Addr bar_offset = 0;
        if (hostBarOffset(host_idx, addr, bar, bar_offset) && bar == 0) {
            if (bar_offset < CXL_COMPONENT_REG_SIZE) {
                memdev = false;
                offset = bar_offset;
                return true;
            }
            if (bar_offset < CXL_COMPONENT_REG_SIZE + CXL_MEMDEV_REG_SIZE) {
                memdev = true;
                offset = bar_offset - CXL_COMPONENT_REG_SIZE;
                return true;
            }
        }

        if (cxlChbsSize != 0 && addr >= cxlChbsBase &&
            addr < cxlChbsBase + cxlChbsSize) {
            memdev = false;
            offset = addr - cxlChbsBase;
            return offset < CXL_COMPONENT_REG_SIZE;
        }

        return false;
    }

    uint8_t
    CXLDRAMsim3::configHostIdx(PacketPtr pkt) const
    {
        if (!pkt)
            return 0;
        if (pkt->source_host_idx >= Packet::MaxPciRequesterIds)
            return Packet::MaxPciRequesterIds - 1;
        return pkt->source_host_idx;
    }

    bool
    CXLDRAMsim3::accessCxlRegisterMmio(PacketPtr pkt)
    {
        if (pkt->cxl_flag && pkt->is_cxl_mem)
            return false;

        const uint8_t host_idx = configHostIdx(pkt);
        bool memdev = false;
        Addr offset = 0;
        if (!cxlRegisterOffset(host_idx, pkt->getAddr(), memdev, offset))
            return false;

        auto &regs = memdev ? hostCxlMemdevRegs[host_idx] :
            hostCxlComponentRegs[host_idx];
        const unsigned size = pkt->getSize();
        std::vector<uint8_t> data(size, 0);

        if (pkt->isRead()) {
            for (unsigned i = 0; i < size && offset + i < regs.size(); ++i)
                data[i] = regs[offset + i];
            pkt->setData(data.data());
        } else if (pkt->isWrite()) {
            pkt->writeData(data.data());
            writeCxlRegisterBytes(host_idx, memdev, offset, data.data(), size);
        } else {
            return false;
        }

        if (pkt->needsResponse())
            pkt->makeResponse();

        return true;
    }

    bool
    CXLDRAMsim3::recvCxlRegisterTimingReq(PacketPtr pkt)
    {
        const bool register_needs_response = pkt->needsResponse();
        if (!accessCxlRegisterMmio(pkt))
            return false;

        if (register_needs_response) {
            responseQueue.push_back(pkt);
            if (!retryResp && !sendResponseEvent.scheduled())
                schedule(sendResponseEvent, latency_ + curTick());
        } else {
            pendingDelete.reset(pkt);
        }

        return true;
    }

    void
    CXLDRAMsim3::writeCxlRegisterBytes(uint8_t host_idx, bool memdev,
                                       Addr offset, const uint8_t *data,
                                       unsigned size)
    {
        host_idx = std::min<uint8_t>(host_idx, Packet::MaxPciRequesterIds - 1);
        auto &component_regs = hostCxlComponentRegs[host_idx];
        auto &memdev_regs = hostCxlMemdevRegs[host_idx];
        auto &regs = memdev ? memdev_regs : component_regs;
        for (unsigned i = 0; i < size && offset + i < regs.size(); ++i)
            regs[offset + i] = data[i];

        if (!memdev) {
            if (offset <= CXL_HDM_DECODER0_CTRL &&
                offset + size > CXL_HDM_DECODER0_CTRL) {
                uint32_t ctrl = readLe32(component_regs,
                                         CXL_HDM_DECODER0_CTRL);
                if (ctrl & CXL_HDM_DECODER0_COMMIT) {
                    ctrl |= CXL_HDM_DECODER0_COMMITTED;
                    ctrl &= ~CXL_HDM_DECODER0_COMMIT_ERROR;
                    writeLe(component_regs, CXL_HDM_DECODER0_CTRL,
                            ctrl, 4);
                }
            }
            return;
        }

        if (offset <= CXLDEV_MBOX_CTRL_OFFSET &&
            offset + size > CXLDEV_MBOX_CTRL_OFFSET &&
            (readLe(memdev_regs, CXLDEV_MBOX_CTRL_OFFSET, 4) & 0x1)) {
            executeCxlMailboxCommand(host_idx);
        }
    }

    void
    CXLDRAMsim3::executeCxlMailboxCommand(uint8_t host_idx)
    {
        host_idx = std::min<uint8_t>(host_idx, Packet::MaxPciRequesterIds - 1);
        auto &memdev_regs = hostCxlMemdevRegs[host_idx];
        auto &event_policy = hostCxlEventInterruptPolicies[host_idx];
        const uint64_t cmd_reg = readLe(memdev_regs,
                                        CXLDEV_MBOX_CMD_OFFSET, 8);
        const uint16_t opcode = cmd_reg & 0xffff;
        const uint32_t in_len = (cmd_reg >> 16) & 0x1fffff;
        std::vector<uint8_t> out;
        uint16_t rc = CXL_MBOX_RC_SUCCESS;

        std::vector<uint8_t> cel;
        auto celEntry = [&cel](uint16_t cel_opcode) {
            appendLe(cel, cel_opcode, 2);
            appendLe(cel, 0, 2);
        };
        celEntry(CXL_MBOX_OP_GET_EVENT_RECORD);
        celEntry(CXL_MBOX_OP_CLEAR_EVENT_RECORD);
        celEntry(CXL_MBOX_OP_GET_EVT_INT_POLICY);
        celEntry(CXL_MBOX_OP_SET_EVT_INT_POLICY);
        celEntry(CXL_MBOX_OP_SET_TIMESTAMP);
        celEntry(CXL_MBOX_OP_GET_SUPPORTED_LOGS);
        celEntry(CXL_MBOX_OP_GET_LOG);
        celEntry(CXL_MBOX_OP_IDENTIFY);
        celEntry(CXL_MBOX_OP_GET_PARTITION_INFO);
        celEntry(CXL_MBOX_OP_GET_LSA);

        switch (opcode) {
          case CXL_MBOX_OP_GET_EVT_INT_POLICY:
            out.insert(out.end(), event_policy.begin(),
                       event_policy.end());
            break;
          case CXL_MBOX_OP_SET_EVT_INT_POLICY:
            if (in_len != event_policy.size()) {
                rc = CXL_MBOX_RC_INPUT;
                break;
            }
            std::copy(memdev_regs.begin() + CXLDEV_MBOX_PAYLOAD_OFFSET,
                      memdev_regs.begin() + CXLDEV_MBOX_PAYLOAD_OFFSET +
                      event_policy.size(),
                      event_policy.begin());
            break;
          case CXL_MBOX_OP_GET_EVENT_RECORD: {
            if (in_len != 1) {
                rc = CXL_MBOX_RC_INPUT;
                break;
            }
            const uint8_t log_type = memdev_regs[CXLDEV_MBOX_PAYLOAD_OFFSET];
            if (log_type > 3) {
                rc = CXL_MBOX_RC_INPUT;
                break;
            }
            appendZeros(out, 32);
            break;
          }
          case CXL_MBOX_OP_CLEAR_EVENT_RECORD:
            break;
          case CXL_MBOX_OP_GET_SUPPORTED_LOGS:
            appendLe(out, 1, 2);
            appendZeros(out, 6);
            out.insert(out.end(), CXL_CEL_UUID,
                       CXL_CEL_UUID + sizeof(CXL_CEL_UUID));
            appendLe(out, cel.size(), 4);
            break;
          case CXL_MBOX_OP_GET_LOG: {
            if (in_len < 24) {
                rc = CXL_MBOX_RC_INPUT;
                break;
            }
            const uint32_t payload = CXLDEV_MBOX_PAYLOAD_OFFSET;
            const bool cel_uuid = std::memcmp(&memdev_regs[payload],
                                              CXL_CEL_UUID,
                                              sizeof(CXL_CEL_UUID)) == 0;
            const uint32_t log_offset =
                readLe(memdev_regs, payload + 16, 4);
            const uint32_t log_length =
                readLe(memdev_regs, payload + 20, 4);
            if (!cel_uuid || log_offset > cel.size()) {
                rc = CXL_MBOX_RC_LOG;
                break;
            }
            const uint32_t copy_len =
                std::min<uint32_t>(log_length, cel.size() - log_offset);
            out.insert(out.end(), cel.begin() + log_offset,
                       cel.begin() + log_offset + copy_len);
            break;
          }
          case CXL_MBOX_OP_IDENTIFY: {
            appendZeros(out, 0x10);
            const char fw[] = "gem5-cxl";
            std::copy(fw, fw + sizeof(fw) - 1, out.begin());
            const uint64_t visible_size = hostVisibleCxlMemSize(host_idx);
            const uint64_t capacity_units =
                visible_size / CXL_CAPACITY_MULTIPLIER;
            appendLe(out, capacity_units, 8);
            appendLe(out, capacity_units, 8);
            appendLe(out, 0, 8);
            appendLe(out, 0, 8);
            appendZeros(out, 8);
            appendLe(out, 0, 4);
            appendZeros(out, 3);
            appendLe(out, 0, 2);
            appendZeros(out, 2);
            break;
          }
          case CXL_MBOX_OP_GET_PARTITION_INFO: {
            const uint64_t visible_size = hostVisibleCxlMemSize(host_idx);
            const uint64_t capacity_units =
                visible_size / CXL_CAPACITY_MULTIPLIER;
            appendLe(out, capacity_units, 8);
            appendLe(out, 0, 8);
            appendLe(out, capacity_units, 8);
            appendLe(out, 0, 8);
            break;
          }
          case CXL_MBOX_OP_GET_LSA: {
            uint32_t requested = 0;
            if (in_len >= 8)
                requested = readLe(memdev_regs,
                                   CXLDEV_MBOX_PAYLOAD_OFFSET + 4, 4);
            appendZeros(out, std::min<uint32_t>(requested,
                                                CXLDEV_MBOX_PAYLOAD_SIZE));
            break;
          }
          case CXL_MBOX_OP_SET_TIMESTAMP:
            break;
          default:
            rc = CXL_MBOX_RC_UNSUPPORTED;
            break;
        }

        if (out.size() > CXLDEV_MBOX_PAYLOAD_SIZE) {
            out.resize(CXLDEV_MBOX_PAYLOAD_SIZE);
            rc = CXL_MBOX_RC_INPUT;
        }

        std::fill(memdev_regs.begin() + CXLDEV_MBOX_PAYLOAD_OFFSET,
                  memdev_regs.begin() + CXLDEV_MBOX_PAYLOAD_OFFSET +
                  CXLDEV_MBOX_PAYLOAD_SIZE, 0);
        std::copy(out.begin(), out.end(),
                  memdev_regs.begin() + CXLDEV_MBOX_PAYLOAD_OFFSET);

        const uint64_t out_cmd_reg = (cmd_reg & ~(0x1fffffULL << 16)) |
            ((uint64_t)out.size() << 16);
        writeLe(memdev_regs, CXLDEV_MBOX_CMD_OFFSET, out_cmd_reg, 8);
        writeLe(memdev_regs, CXLDEV_MBOX_STATUS_OFFSET,
                (uint64_t)rc << 32, 8);
        writeLe(memdev_regs, CXLDEV_MBOX_CTRL_OFFSET,
                readLe(memdev_regs, CXLDEV_MBOX_CTRL_OFFSET, 4) & ~0x1ULL,
                4);
    }

    //esj 2024-10-12
    bool CXLDRAMsim3::recvTimingReq(PacketPtr pkt){
        // warn("esj recvtimingreq cxl");

        if (recvCxlRegisterTimingReq(pkt))
            return true;

        //esj cxl fixed mode option 2025-11-24
        // if (switch_mode){
        //     Tick cxl_latency = resolve_cxl_mem(pkt);
        //     mem_.access(pkt);

        //     if (pkt->needsResponse()) {
        //         assert(pkt->isResponse());
        //         DPRINTF(CXLDRAMsim3, "Queuing response for address %x\n",
        //                 pkt->getAddr());
        //         // queue it to be sent back
        //         responseQueue.push_back(pkt);
        //         DPRINTF(CXLDRAMsim3, "Queued response\n");

        //         // If retry is clear, schedule a response.
        //         if (!retryResp && !sendResponseEvent.scheduled()){
        //             DPRINTF(CXLDRAMsim3, "%s retryResp=%d\n",
        //                     __func__, retryResp);
        //             schedule(sendResponseEvent, cxl_latency + curTick());
        //         }
        //     } else {
        //         // queue the packet for deletion
        //         pendingDelete.reset(pkt);
        //     }
        //     return true;
        // }

        //esj 2024-10-22 return pkt size
        // change_cxl_mem_packet_size(pkt);

        //esj 2024-11-15
        //esj 2025-02-01
        // if(!wrapper.canAccept(pkt->origin_dest, pkt->isWrite())){
        // // if(!wrapper_write.canAccept(pkt->origin_dest, pkt->isWrite()) || !wrapper.canAccept(pkt->origin_dest, pkt->isWrite())){
        //     DPRINTF(CXLDRAMsim3, "%s can't accept retry req\n",__func__);
        //     //esj 2024-12-27
        //     // retryReq = true;
        //     return false;
        // }
        // if(pkt->isWrite()){
        //     if(!wrapper_write.canAccept(pkt->origin_dest, pkt->isWrite())){
        //         DPRINTF(CXLDRAMsim3, "%s can't accept retry req\n",__func__);
        //         return false;
        //     }
        // }
        // else{
        //     if(!wrapper.canAccept(pkt->origin_dest, pkt->isWrite())){
        //         DPRINTF(CXLDRAMsim3, "%s can't accept retry req\n",__func__);
        //         return false;
        //     }
        // }

        // if a cache is responding, sink the packet without further action
        if (pkt->cacheResponding()) {
            pendingDelete.reset(pkt);
            return true;
        }

        // we should not get a new request after committing to retry the
        // current one, but unfortunately the CPU violates this rule, so
        // simply ignore it for now
        if (retryReq)
            return false;

        // Added: split composed parent packet into parent/child subrequests.
        PacketPtr child_pkt = nullptr;
        if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
            child_pkt = pkt->cxlMergedPkt;
            DPRINTF(CXLDRAMsim3,
                    "CXL compose split merge_id=%llu parent id=%llu size=%u "
                    "child id=%llu size=%u transport=%u\n",
                    static_cast<unsigned long long>(pkt->cxlMergeId),
                    static_cast<unsigned long long>(pkt->id), pkt->getSize(),
                    static_cast<unsigned long long>(child_pkt->id),
                    child_pkt->getSize(), pkt->cxlTransportSize);
        }

        // Global queue-capacity guard (restored): account for composed
        // parent+child request pair as two outstanding entries.
        const bool parent_valid_req = pkt->isRead() || pkt->isWrite();
        const bool child_valid_req = (child_pkt != nullptr) &&
            (child_pkt->isRead() || child_pkt->isWrite());
        const unsigned int needed_slots =
            static_cast<unsigned int>(parent_valid_req) +
            static_cast<unsigned int>(child_valid_req);
        bool can_accept = nbrOutstanding() < wrapper.queueSize();
        if (needed_slots > 1) {
            can_accept =
                ((nbrOutstanding() + needed_slots) <= wrapper.queueSize());
        }
        if (!can_accept) {
            DPRINTF(CXLDRAMsim3,
                    "%s cxl dramsim3 queue full (outstanding=%u, need=%u, "
                    "limit=%u)\n",
                    __func__, nbrOutstanding(), needed_slots,
                    wrapper.queueSize());
            retryReq = true;
            return false;
        }

        if ((pkt->isRead() || pkt->isWrite()) &&
            !wrapper.canAccept(pkt->origin_dest, pkt->isWrite())) {
            DPRINTF(CXLDRAMsim3, "%s cxl dramsim3 can't accept retry req\n",
                    __func__);
            retryReq = true;
            return false;
        }

        if ((child_pkt != nullptr) &&
            (child_pkt->isRead() || child_pkt->isWrite()) &&
            !wrapper.canAccept(child_pkt->origin_dest, child_pkt->isWrite())) {
            DPRINTF(CXLDRAMsim3,
                    "CXL compose child enqueue blocked id=%llu\n",
                    static_cast<unsigned long long>(child_pkt->id));
            retryReq = true;
            return false;
        }

        // Enqueue parent and child using original PacketPtrs.
        auto enqueueOne = [this](PacketPtr sub_pkt) {
            const bool is_read = sub_pkt->isRead();
            const bool is_write = sub_pkt->isWrite();

            if (is_read) {
                outstandingReads[sub_pkt->origin_dest].push(sub_pkt);
                ++nbrOutstandingReads;
                assert(wrapper.canAccept(sub_pkt->origin_dest, is_write));
                wrapper.enqueue(sub_pkt->origin_dest, is_write);
                return;
            }
            else if (is_write) {
                outstandingWrites[sub_pkt->origin_dest].push(sub_pkt);
                ++nbrOutstandingWrites;
                sub_pkt->is_cxl_write_resp = true;
                accessAndRespond(sub_pkt);
                assert(wrapper.canAccept(sub_pkt->origin_dest, is_write));
                wrapper.enqueue(sub_pkt->origin_dest, is_write);
                return;
            }

        };

        enqueueOne(pkt);
        if (child_pkt != nullptr) {
            // Added: preserve parent-assigned CXL seq metadata for composed
            // secondary request if it was not initialized on the child.
            child_pkt->cxl_flag = pkt->cxl_flag;
            child_pkt->cxl_pkt.start = pkt->cxl_pkt.start;
            child_pkt->cxl_pkt.retry_req = false;
            child_pkt->cxl_pkt.retry_resp = false;
            child_pkt->cxl_pkt.is_controlflit = false;
            enqueueOne(child_pkt);
            // Split is complete; parent no longer needs the child pointer.
        }

        return true;
    }
    Tick CXLDRAMsim3::read(PacketPtr pkt){
        if (accessCxlRegisterMmio(pkt))
            return latency_;
        Tick cxl_latency = resolve_cxl_mem(pkt);
        mem_.access(pkt);
        return latency_ + cxl_latency;
    }

    Tick CXLDRAMsim3::write(PacketPtr pkt){
        if (accessCxlRegisterMmio(pkt))
            return latency_;
        Tick cxl_latency = resolve_cxl_mem(pkt);
        mem_.access(pkt);
        return latency_ + cxl_latency;
    }


    // Tick CXLDRAMsim3::read(PacketPtr pkt) {
    //     if( (!switch_mode && trace::is_timing_mode) | (switch_mode && trace::change2timingcpu )){ //esj 2024-10-02
    //         // if we cannot accept we need to send a retry once progress can
    //         // be made
    //         bool can_accept = nbrOutstanding() < wrapper.queueSize();
    //         if (can_accept) {
    //             // change_addr(pkt);//esj 2024-06-29
    //             outstandingReads[pkt->getAddr()].push(pkt);

    //             // we count a transaction as outstanding until it has left the
    //             // queue in the controller, and the response has been sent
    //             // back, note that this will differ for reads and writes
    //             ++nbrOutstandingReads;

    //             // we should never have a situation when we think there is space,
    //             // and there isn't
    //             assert(wrapper.canAccept(pkt->getAddr(), pkt->isWrite()));

    //             DPRINTF(CXLDRAMsim3, "read Enqueueing address %x, isResponse: = %d\n", pkt->getAddr(), pkt->isResponse());

    //             // @todo what about the granularity here, implicit assumption that
    //             // a transaction matches the burst size of the memory (which we
    //             // cannot determine without parsing the ini file ourselves)
    //             wrapper.enqueue(pkt->getAddr(), pkt->isWrite());

    //             return resolve_cxl_mem(pkt);
    //         }
    //         else{
    //             retryReq = true;
    //             return resolve_cxl_mem(pkt);
    //         }
    //     }
    //     else{

    //         Tick cxl_latency = resolve_cxl_mem(pkt);
    //         // change_addr(pkt); //esj 2024-06-29
    //         mem_.access(pkt);
    //         return latency_ + cxl_latency;
    //     }

    // }

    // Tick CXLDRAMsim3::write(PacketPtr pkt) {

    //     if((!switch_mode && trace::is_timing_mode) | (switch_mode && trace::change2timingcpu )){ //esj 2024-10-02
    //         // if we cannot accept we need to send a retry once progress can
    //         // be made
    //         bool can_accept = nbrOutstanding() < wrapper.queueSize();

    //         if (can_accept) {
    //             // change_addr(pkt);//esj 2024-06-29
    //             outstandingWrites[pkt->getAddr()].push(pkt);
    //             // 0xC0000~
    //             ++nbrOutstandingWrites;

    //             // // perform the access for writes
    //             // accessAndRespond(pkt);
    //             //0x1000~
    //             // we should never have a situation when we think there is space,
    //             // and there isn't
    //             // warn("esj cxl wrapper addr = %0x, cap = %d\n",pkt->getAddr(),wrapper.getbuffercapacity(pkt->getAddr()));
    //             // warn("esj cxl wrapper addr = %0x, size = %d\n",pkt->getAddr(),wrapper.getbuffersize(pkt->getAddr()));
    //             assert(wrapper.canAccept(pkt->getAddr(), pkt->isWrite()));

    //             DPRINTF(CXLDRAMsim3, "write Enqueueing address %x, isResponse = %d \n", pkt->getAddr(), pkt->isResponse());

    //             // @todo what about the granularity here, implicit assumption that
    //             // a transaction matches the burst size of the memory (which we
    //             // cannot determine without parsing the ini file ourselves)
    //             wrapper.enqueue(pkt->getAddr(), pkt->isWrite());

    //             //esj 2024-06-29
    //             // perform the access for writes
    //             accessAndRespond(pkt);

    //             return resolve_cxl_mem(pkt);
    //         }
    //         else{
    //             retryReq = true;
    //             return 0;
    //         }
    //     }
    //     else{
    //         Tick cxl_latency = resolve_cxl_mem(pkt);
    //         // change_addr(pkt);//esj 2024-06-29
    //         mem_.access(pkt);

    //         return latency_ + cxl_latency;
    //     }

    // }

    AddrRangeList CXLDRAMsim3::getAddrRanges() const {
        DPRINTF(CXLDRAMsim3, "esj getaddress\n");
        AddrRangeList ranges;
        PciCommandRegister command = letoh(config.command);
        for (auto *bar: BARs) {
            if (!bar || bar->size() == 0 || bar->addr() == 0)
                continue;
            if (command.ioSpace && bar->isIo())
                ranges.push_back(bar->range());
            if (command.memorySpace && bar->isMem())
                ranges.push_back(bar->range());
        }
        if (cxlChbsSize != 0)
            ranges.push_back(RangeSize(cxlChbsBase, cxlChbsSize));
        return ranges;
    }

    Tick CXLDRAMsim3::resolve_cxl_mem(PacketPtr pkt) {
        // if (pkt->cmd == MemCmd::M2SReq) {
        if (pkt->cmd == MemCmd::ReadReq) {
            assert(pkt->isRead());
            assert(pkt->needsResponse());
        // } else if (pkt->cmd == MemCmd::M2SRwD) {
        } else if (pkt->cmd == MemCmd::WriteReq) {
            assert(pkt->isWrite());
            assert(pkt->needsResponse());
        }
        return cxl_mem_latency_;
    }

    CXLDRAMsim3::Memory::Memory(const AddrRange& range, CXLDRAMsim3& owner)
        : range(range),
        pmemAddr(NULL),
        owner(owner),
        backdoor(range, nullptr,
             (MemBackdoor::Flags)(writeable ?
                 MemBackdoor::Readable | MemBackdoor::Writeable :
                 MemBackdoor::Readable))
        {
        // mem_ is declared before cxl_mem_size in CXLDRAMsim3, so the owner
        // field is not initialized yet. Use the constructor range from params.
        panic_if(!this->range.size(),
                "%s has zero-sized CXL backing store\n", name());

        ownedPmemAddr = (uint8_t*)mmap(nullptr, this->range.size(),
                PROT_READ | PROT_WRITE,
                MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);

        panic_if(ownedPmemAddr == (uint8_t*)MAP_FAILED,
                "%s could not mmap %llu bytes for CXL backing store: %s\n",
                name(), (unsigned long long)this->range.size(),
                strerror(errno));

        pmemAddr = ownedPmemAddr;
        backdoor.ptr(this->range.interleaved() ? nullptr : pmemAddr);

        DPRINTF(CXLDRAMsim3,
                "initial range start=0x%lx, range size=0x%lx pmem=%p\n",
                this->range.start(), this->range.size(), pmemAddr);
    }

    CXLDRAMsim3::Memory::~Memory()
    {
        if (ownedPmemAddr)
            munmap((char*)ownedPmemAddr, range.size());
    }

    void CXLDRAMsim3::Memory::access(PacketPtr pkt) {
        ////////
        if(gem5::trace::cxl_check==2 && number < 4)
            number+=1;
        ////////
        // change_addr(pkt);//esj 2024-06-29 10-12
        PciBar *bar = owner.BARs[0];
        AddrRange range_bar = RangeSize(bar->addr(), bar->size()); //esj 2024-10-12
        range = RangeSize(0x0, owner.cxl_mem_size); //esj 2024-06-29
        if(gem5::trace::cxl_check==2 && number < 4)
            DPRINTF(CXLDRAMsim3, "final range start=%0x, range size=%0x\n", range.start(), range.size());
        // range = AddrRange(0x100000000, 0x100000000 + 0x100000000); // 0x8000000=128MiB 0x100000000=4GiB
        if (pkt->cacheResponding()) {
            if(gem5::trace::cxl_check==2 && number < 4)
                DPRINTF(CXLDRAMsim3, "Cache responding to %#llx: not responding\n", pkt->getAddr());
            return;
        }

        if (pkt->cmd == MemCmd::CleanEvict || pkt->cmd == MemCmd::WritebackClean) {
            if(gem5::trace::cxl_check==2 && number < 4)
                DPRINTF(CXLDRAMsim3, "CleanEvict  on 0x%x: not responding\n", pkt->getAddr());
            return;
        }

        // assert(pkt->getAddrRange().isSubset(range_bar));

        //////////////////esj
        // Addr offset = 0xB0000000;
        // Addr cureent_pkt_addr = pkt->getAddr();
        // cureent_pkt_addr -= offset;
        // pkt->setAddr(cureent_pkt_addr);

        // uint8_t* host_addr = toHostAddr(cureent_pkt_addr); //esj
        // if (gem5::trace::cxl_check==2 && number < 4)
        //     DPRINTF(CXLDRAMsim3, "esj host_addr = %x",host_addr);
        //////////////////esj

        uint8_t* backing_store = pmemAddr;
        panic_if(!backing_store,
                "%s has no backing store for CXL access to %#llx\n",
                name(), (unsigned long long)pkt->origin_dest);

        panic_if(pkt->origin_dest < range.start(),
                "%s CXL access address %#llx is below backing range %s\n",
                name(), (unsigned long long)pkt->origin_dest,
                range.to_string().c_str());

        const Addr mem_offset = pkt->origin_dest - range.start();
        panic_if(mem_offset > range.size() ||
                pkt->getSize() > range.size() - mem_offset,
                "%s CXL access address %#llx size %u exceeds backing "
                "range %s\n",
                name(), (unsigned long long)pkt->origin_dest,
                pkt->getSize(), range.to_string().c_str());

        // CXL.mem packets no longer pass through MemCtrl, so pkt->pmemaddr is
        // not a valid source of backing storage. Keep the HPA-derived offset,
        // but source bytes from this device's own backing store.
        uint8_t* host_addr = backing_store + mem_offset;

        DPRINTF(CXLDRAMsim3, "esj addr=%x range.start()=%x\n",
                pkt->getAddr(), range.start());
        // change_addr(pkt);//esj 2024-06-29 10-12

        // DPRINTF(CxlMemory, "host_addr=%p pkt=%#lx pmemAddr=%p\n",
            // *host_addr, pkt->getAddr(), *pmemAddr, range.start());
        if (pkt->cmd == MemCmd::SwapReq) {
            if (gem5::trace::cxl_check == 2 && number < 4)
                DPRINTF(CXLDRAMsim3, "MemCmd::SwapReq\n");

            if (pkt->isAtomicOp()) {
                pkt->setData(host_addr);
                (*(pkt->getAtomicOp()))(host_addr);
            } else {
                std::vector<uint8_t> overwrite_val(pkt->getSize());
                uint64_t condition_val64;
                uint32_t condition_val32;

                panic_if(!backing_store,
                        "Swap only works if there is real memory "
                        "(i.e. null=False)");

                bool overwrite_mem = true;
                // keep a copy of our possible write value, and copy what is at the
                // memory address into the packet
                // Write packet data into the vector.
                pkt->writeData(&overwrite_val[0]);
                // Return host memory data in the packet.
                pkt->setData(host_addr);

                if (pkt->req->isCondSwap()) {
                    if (pkt->getSize() == sizeof(uint64_t)) {
                        condition_val64 = pkt->req->getExtraData();
                        overwrite_mem =
                            !std::memcmp(&condition_val64, host_addr,
                                         sizeof(uint64_t));
                    } else if (pkt->getSize() == sizeof(uint32_t)) {
                        condition_val32 = (uint32_t)pkt->req->getExtraData();
                        overwrite_mem =
                            !std::memcmp(&condition_val32, host_addr,
                                         sizeof(uint32_t));
                    } else
                        panic("Invalid size for conditional read/write\n");
                }

                if (overwrite_mem)
                    std::memcpy(host_addr, &overwrite_val[0], pkt->getSize());

                assert(!pkt->req->isInstFetch());

                //esj 2024-07-26
                owner.stats.numOther[pkt->req->requestorId()]++;

            }
        } else if (pkt->isRead()) {
            DPRINTF(CXLDRAMsim3, "pkt->isRead()\n");

            assert(!pkt->isWrite());

            //esj 2025-01-31
            if (pkt->isLLSC()) {
                assert(!pkt->fromCache());
                // if the packet is not coming from a cache then we have
                // to do the LL/SC tracking here
                trackLoadLocked(pkt);
            }
            //

            DPRINTF(CXLDRAMsim3, "DEXTER|| isRead() & pmemAddr\n");
            pkt->setData(host_addr);
            DPRINTF(CXLDRAMsim3, "%s read due to %s\n",
                    __func__, pkt->print());

            //esj 2024-07-26
            owner.stats.numReads[pkt->req->requestorId()]++;
            owner.stats.bytesRead[pkt->req->requestorId()] += pkt->getSize();
            if (pkt->req->isInstFetch())
                owner.stats.bytesInstRead[pkt->req->requestorId()] +=
                    pkt->getSize();
            //////

        } else if (pkt->isInvalidate() || pkt->isClean()) {
            assert(!pkt->isWrite());
            // in a fastmem system invalidating and/or cleaning packets
            // can be seen due to cache maintenance requests

            // no need to do anything
        } else if (pkt->isWrite()) {
            //esj 2025-01-31
            if (writeOK(pkt)) {
                pkt->writeData(host_addr);
                DPRINTF(CXLDRAMsim3, "%s write due to %s\n",
                        __func__, pkt->print());
                assert(!pkt->req->isInstFetch());
                owner.stats.numWrites[pkt->req->requestorId()]++;
                owner.stats.bytesWritten[pkt->req->requestorId()] += pkt->getSize();
            }

        } else {
            panic("Unexpected packet %s\n", pkt->print());
        }
        if (pkt->needsResponse()) {
            ///////////////////////
            //esj 2024-04-24
            /*Addr offset = 0xB0000000;
            Addr cureent_pkt_addr = pkt->getAddr();

            cureent_pkt_addr -= offset;
            pkt->setAddr(cureent_pkt_addr);*/
            /////////////////////////

            //esj 2025-05-07
            // Addr curent_pkt_addr = pkt->getAddr(); //esj 2025-04-06
            //esj 2025-09-09 no_controller
            // pkt->setAddr(pkt->origin_addr); //esj 2024-08-08
            // pkt->origin_addr = curent_pkt_addr; //esj 2025-04-06

            //esj cxl fixed mode option 2025-11-24
            // if (gem5::trace::cxl_fixed_mode)
            //     pkt->setAddr(pkt->origin_addr);

            if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                DPRINTF(CXLDRAMsim3, "remove child pktptr from parent pkt\n");
                pkt->cxlMergedPkt = nullptr;
            }

            pkt->makeResponse();

            DPRINTF(CXLDRAMsim3, "DEXTER|| makeResponse()\n");
            //DPRINTF(CXLDRAMsim3, "DEXTER || response cxl current addr = %08x after addr = %08x write? = %d read? = %d data? = %d request? = %08x flags = %x\n",current_pkt_addr,pkt->getAddr(),pkt->isWrite(),pkt->isRead(), pkt->hasData(), pkt->isRequest(),pkt->cpy_flags);

        }
    }

    CXLDRAMsim3::CXLStats::CXLStats(CXLDRAMsim3 &_mem)
        : statistics::Group(&_mem), mem(_mem),
        ADD_STAT(bytesRead, statistics::units::Byte::get(),
                "Number of bytes read from this memory"),
        ADD_STAT(bytesInstRead, statistics::units::Byte::get(),
                "Number of instructions bytes read from this memory"),
        ADD_STAT(bytesWritten, statistics::units::Byte::get(),
                "Number of bytes written to this memory"),
        ADD_STAT(numReads, statistics::units::Count::get(),
                "Number of read requests responded to by this memory"),
        ADD_STAT(numWrites, statistics::units::Count::get(),
                "Number of write requests responded to by this memory"),
        ADD_STAT(numOther, statistics::units::Count::get(),
                "Number of other requests responded to by this memory"),
        ADD_STAT(bwRead, statistics::units::Rate<
                    statistics::units::Byte, statistics::units::Second>::get(),
                "Total read bandwidth from this memory"),
        ADD_STAT(bwInstRead,
                statistics::units::Rate<
                    statistics::units::Byte, statistics::units::Second>::get(),
                "Instruction read bandwidth from this memory"),
        ADD_STAT(bwWrite, statistics::units::Rate<
                    statistics::units::Byte, statistics::units::Second>::get(),
                "Write bandwidth from this memory"),
        ADD_STAT(bwTotal, statistics::units::Rate<
                    statistics::units::Byte, statistics::units::Second>::get(),
                "Total bandwidth to/from this memory")
    {
    }

    void
    CXLDRAMsim3::CXLStats::regStats()
    {

        using namespace statistics;

        statistics::Group::regStats();

        System *sys = mem.system();
        assert(sys);
        const auto max_requestors = sys->maxRequestors();

        bytesRead
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bytesRead.subname(i, sys->getRequestorName(i));
        }

        bytesInstRead
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bytesInstRead.subname(i, sys->getRequestorName(i));
        }

        bytesWritten
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bytesWritten.subname(i, sys->getRequestorName(i));
        }

        numReads
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            numReads.subname(i, sys->getRequestorName(i));
        }

        numWrites
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            numWrites.subname(i, sys->getRequestorName(i));
        }

        numOther
            .init(max_requestors)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            numOther.subname(i, sys->getRequestorName(i));
        }

        bwRead
            .precision(0)
            .prereq(bytesRead)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bwRead.subname(i, sys->getRequestorName(i));
        }

        bwInstRead
            .precision(0)
            .prereq(bytesInstRead)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bwInstRead.subname(i, sys->getRequestorName(i));
        }

        bwWrite
            .precision(0)
            .prereq(bytesWritten)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bwWrite.subname(i, sys->getRequestorName(i));
        }

        bwTotal
            .precision(0)
            .prereq(bwTotal)
            .flags(total | nozero | nonan)
            ;
        for (int i = 0; i < max_requestors; i++) {
            bwTotal.subname(i, sys->getRequestorName(i));
        }

        bwRead = bytesRead / simSeconds;
        bwInstRead = bytesInstRead / simSeconds;
        bwWrite = bytesWritten / simSeconds;
        bwTotal = (bytesRead + bytesWritten) / simSeconds;
    }


    Tick CXLDRAMsim3::readConfig(PacketPtr pkt) {
        //esj 2025-04-30
        // int offset = pkt->getAddr() & PCI_CONFIG_SIZE;
        int offset = cxlPciConfigOffset(pkt->getAddr());
        int size = pkt->getSize();

        DPRINTF(CXLDRAMsim3, "cxl_meory read config offset = %#x\n", offset);
        if (offset < PCI_DEVICE_SPECIFIC) {
            const uint8_t host_idx = configHostIdx(pkt);
            config = hostPciConfigs[host_idx];
            Tick delay = PciDevice::readConfig(pkt);
            hostPciConfigs[host_idx] = config;
            config = hostPciConfigs[0];
            return delay;
        }
        //esj 2025-02-05
        // else{
        else if (offset >= PCI_DEVICE_SPECIFIC &&
                 offset <= PCIE_CONFIG_SIZE) {
            // Read on PCI capabilities
            const uint8_t host_idx = configHostIdx(pkt);
            const auto &host_caps = hostPciCapabilityStates[host_idx];
            uint32_t val = 0;
            for (int i = 0; i < size; i++) {
                if (offset + i >= CXL_DEVICE_DVSEC_BASE &&
                    offset + i <
                        CXL_DEVICE_DVSEC_BASE + CXL_DEVICE_DVSEC_SIZE) {
                    val |= (uint32_t)host_caps.cxl_config.data[
                        offset + i - CXL_DEVICE_DVSEC_BASE] << (i * 8);
                    DPRINTF(CXLDRAMsim3,
                            "CXL Device DVSEC @%x data[%d] = %x\n",
                            CXL_DEVICE_DVSEC_BASE,
                            offset + i - CXL_DEVICE_DVSEC_BASE, val);
                }
                else if (offset + i >= CXL_REG_LOCATOR_DVSEC_BASE &&
                        offset + i < CXL_REG_LOCATOR_DVSEC_BASE +
                        CXL_REG_LOCATOR_DVSEC_SIZE) {
                    val |= (uint32_t)host_caps.cxl_config_locator.data[
                        offset + i - CXL_REG_LOCATOR_DVSEC_BASE] << (i * 8);
                    DPRINTF(CXLDRAMsim3,
                            "CXL Register Locator DVSEC @%x data[%d] = %x\n",
                            CXL_REG_LOCATOR_DVSEC_BASE,
                            offset + i - CXL_REG_LOCATOR_DVSEC_BASE, val);
                }
                else if (offset + i >= PMCAP_BASE &&
                         offset + i < PMCAP_BASE + PMCAP_SIZE) {
                    val |= (uint32_t)host_caps.pmcap.data[
                        offset + i - PMCAP_BASE] << (i * 8);
                }
                else if (offset + i >= MSICAP_BASE &&
                        offset + i < MSICAP_BASE + MSICAP_SIZE) {
                    val |= (uint32_t)host_caps.msicap.data[
                        offset + i - MSICAP_BASE] << (i * 8);
                }
                else if (offset + i >= MSIXCAP_BASE &&
                        offset + i < MSIXCAP_BASE + MSIXCAP_SIZE) {
                    val |= (uint32_t)host_caps.msixcap.data[
                        offset + i - MSIXCAP_BASE] << (i * 8);
                }
                else if (offset + i >= PXCAP_BASE &&
                        offset + i < PXCAP_BASE + PXCAP_SIZE) {
                    val |= (uint32_t)host_caps.pxcap.data[
                        offset + i - PXCAP_BASE] << (i * 8);
                    DPRINTF(CXLDRAMsim3,
                            "PXCAP_BASE=%x pxcap.data[%d]=%x\n",
                            PXCAP_BASE, offset + i - PXCAP_BASE, val);
                }
                else if (offset + i >= 0xB0 &&
                         offset + i < 0xB0 + CXL_DEVICE_DVSEC_SIZE) {
                    val |= (uint32_t)host_caps.cxl_config.data[
                        offset + i - 0xB0] << (i * 8);
                    DPRINTF(CXLDRAMsim3,
                            "cxl_config=%x cxl_config.data[%d]=%x\n",
                            0xB0, offset + i - 0xB0, val);
                }
                else if (offset + i >= 0x40 &&
                         offset + i < 0x40 + CXL_REG_LOCATOR_DVSEC_SIZE) {
                    val |= (uint32_t)host_caps.cxl_config_locator.data[
                        offset + i - 0x40] << (i * 8);
                    DPRINTF(CXLDRAMsim3,
                            "cxl_config=%x cxl_config_locator.data[%d]=%x\n",
                            0x40, offset + i - 0x40, val);
                }

                else {
                    DPRINTF(CXLDRAMsim3,
                            "CXL type3 invalid config read offset=%#x\n",
                            offset);
                }

            }
            // uint16_t val32 = ((uint32_t)cxl_config.data[11] << 8)|(uint32_t)cxl_config.data[10];
            // DPRINTF(CXLDRAMsim3, "16bit: reg offset = %x, cxl_config.pxid = %x, val = %x\n", offset, cxl_config.cap, val32);
            // DPRINTF(CXLDRAMsim3, "cxl_config.data[11] = %x, cxl_config.data[10] = %x\n", cxl_config.data[11], cxl_config.data[10]);

            switch (size) {
            case sizeof(uint8_t):
                pkt->setLE<uint8_t>(val);
                DPRINTF(CXLDRAMsim3, "8bit offset=%x data=%x\n",
                        offset, val);
                break;
            case sizeof(uint16_t):
                pkt->setLE<uint16_t>(val);
                DPRINTF(CXLDRAMsim3, "16bit offset=%x data=%x\n",
                        offset, val);
                break;
            case sizeof(uint32_t):
                pkt->setLE<uint32_t>(val);
                DPRINTF(CXLDRAMsim3, "32bit offset=%x data=%x\n",
                        offset, val);
                break;
            default:
                DPRINTF(CXLDRAMsim3, "CXL type3 invalid config read size=%d\n",
                        size);
                break;
            }

            // Legacy per-capability config read path retained in git history.
            pkt->makeAtomicResponse();
        }
        // else{
        //     uint32_t val = 0;
        //     // //esj 2025-02-04
        //     if(offset  >= 0xB0 && offset  < 0xB4){
        //         val = (uint32_t)cxl_config.hdr.cap_hdr;
        //     }
        //     else if(offset >= 0xB4 && offset < 0xB8){
        //         val = (uint32_t)cxl_config.hdr.dv_hdr1;
        //     }
        //     else if(offset >= 0xB8 && offset < 0xBA){
        //         val = (uint32_t)cxl_config.hdr.dv_hdr2;
        //     }
        //     else if(offset >= 0xBA && offset < 0xBC){
        //         val = (uint32_t)cxl_config.cap;
        //     }
        //     else if(offset >= 0xBC && offset < 0xBE){
        //         val = (uint32_t)cxl_config.ctrl;
        //     }
        //     else if(offset >= 0xBE && offset < 0xC0){
        //         val = (uint32_t)cxl_config.status;
        //     }
        //     else if(offset >= 0xC0 && offset < 0xC2){
        //         val = (uint32_t)cxl_config.ctrl2;
        //     }
        //     else if(offset >= 0xC2 && offset < 0xC4){
        //         val = (uint32_t)cxl_config.status2;
        //     }
        //     else if(offset >= 0xC4 && offset < 0xC6){
        //         val = (uint32_t)cxl_config.lock;
        //     }
        //     else if(offset >= 0xC6 && offset < 0xC8){
        //         val = (uint32_t)cxl_config.cap2;
        //     }
        //     else if(offset >= 0xC8 && offset < 0xCC){
        //         val = (uint32_t)cxl_config.range1_size_hi;
        //     }
        //     else if(offset >= 0xCC && offset < 0xD0){
        //         val = (uint32_t)cxl_config.range1_size_lo;
        //     }
        //     else if(offset >= 0xD0 && offset < 0xD4){
        //         val = (uint32_t)cxl_config.range1_base_hi;
        //     }
        //     else if(offset >= 0xD4 && offset < 0xD8){
        //         val = (uint32_t)cxl_config.range1_base_lo;
        //     }
        //     else if(offset >= 0xD8 && offset < 0xDC){
        //         val = (uint32_t)cxl_config.range2_size_hi;
        //     }
        //     else if(offset >= 0xDC && offset < 0xE0){
        //         val = (uint32_t)cxl_config.range2_size_lo;
        //     }
        //     else if(offset >= 0xE0 && offset < 0xE4){
        //         val = (uint32_t)cxl_config.range2_base_hi;
        //     }
        //     else if(offset >= 0xE4 && offset < 0xE8){
        //         val = (uint32_t)cxl_config.range2_base_lo;
        //     }
        //     else if(offset >= 0xE8 && offset < 0xEA){
        //         val = (uint32_t)cxl_config.cap3;
        //     }
        //     else if(offset >= 0xEA && offset < 0xEF){
        //         val = (uint32_t)0;
        //     }

        //     switch (size) {
        //     case sizeof(uint8_t):
        //         pkt->setLE<uint8_t>(val);
        //         break;
        //     case sizeof(uint16_t):
        //         pkt->setLE<uint16_t>(val);
        //         break;
        //     case sizeof(uint32_t):
        //         pkt->setLE<uint32_t>(val);
        //         break;
        //     default:
        //         DPRINTF(CXLDRAMsim3,"CXL_type3_interface: Invalid PCI config read size: %d\n",size);
        //         break;
        //     }

        //     pkt->makeAtomicResponse();
        // }
        return configDelay;
    }

    Tick CXLDRAMsim3::writeConfig(PacketPtr pkt) {
        // warn("write config cmd=%x, id= %d flag = %x",pkt->cmdString(),pkt->id, pkt);
    // int offset = pkt->getAddr() & PCI_CONFIG_SIZE;
    int offset = cxlPciConfigOffset(pkt->getAddr());
    int size = pkt->getSize();
    uint32_t val = 0;
    DPRINTF(CXLDRAMsim3, "esj pcie offset  = %x\n",offset);
    if (offset < PCI_DEVICE_SPECIFIC) {
        //DPRINTF(CXLDRAMsim3, "cxl_meory write config");
        const uint8_t host_idx = configHostIdx(pkt);
        config = hostPciConfigs[host_idx];
        PciDevice::writeConfig(pkt);
        hostPciConfigs[host_idx] = config;
        snapshotHostBarRanges(host_idx);
        config = hostPciConfigs[0];
        // DPRINTF(CXLDRAMsim3, "cxl check");
        // gem5::trace::cxl_check = 1; //esj 2024-03-06
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
        //DPRINTF(CXLDRAMsim3, "esj cxl memory bar0 = %x size = %x",registerTableBaseAddress,registerTableSize);
    }


    else {

       // DPRINTF(CXLDRAMsim3, "esj pcie offset  = %x ",offset);
        // Write on PCI capabilities
        const uint8_t host_idx = configHostIdx(pkt);
        auto &host_caps = hostPciCapabilityStates[host_idx];
        if (offset >= CXL_DEVICE_DVSEC_BASE &&
            offset + size <= CXL_DEVICE_DVSEC_BASE + CXL_DEVICE_DVSEC_SIZE) {
            std::vector<uint8_t> data(size);
            pkt->writeData(data.data());
            std::memcpy(host_caps.cxl_config.data +
                        offset - CXL_DEVICE_DVSEC_BASE, data.data(), size);
        }
        else if (offset == PMCAP_BASE + 4 && size == sizeof(uint16_t)) {
            // PMCAP Control Status
            val = pkt->getLE<uint16_t>();

            if (val & 0x8000) {
                host_caps.pmcap.pmcs &= 0x7F00;  // Clear PMES
            }
            host_caps.pmcap.pmcs &= ~0x1F03;
            host_caps.pmcap.pmcs |= (val & 0x1F03);
        }
        else if (offset == MSICAP_BASE + 2 && size == sizeof(uint16_t)) {  // MSICAP Message Control
            val = pkt->getLE<uint16_t>();

            host_caps.mode = (val & 0x0001) ? INTERRUPT_MSI : INTERRUPT_PIN;

            host_caps.msicap.mc &= ~0x0071;
            host_caps.msicap.mc |= (val & 0x0071);

            host_caps.vectors =
                (uint16_t)powf(2, (host_caps.msicap.mc & 0x0070) >> 4);

            DPRINTF(CXLDRAMsim3,"INTR    | MSI %s | %d vectors\n",
                    host_caps.mode == INTERRUPT_PIN ? "disabled" : "enabled",
                    host_caps.vectors);
        }
        else if (offset == MSICAP_BASE + 4 && size == sizeof(uint32_t)) {  // MSICAP Message Address
            host_caps.msicap.ma = pkt->getLE<uint32_t>() & 0xFFFFFFFC;
        }
        else if (offset == MSICAP_BASE + 8 && size == sizeof(uint32_t)) {  // MSICAP Message Upper Address
            host_caps.msicap.mua = pkt->getLE<uint32_t>();
        }
        else if (offset == MSICAP_BASE + 12 && size >= sizeof(uint16_t)) {  // MSICAP Message Data
            host_caps.msicap.md = pkt->getLE<uint16_t>();
        }
        else if (offset == MSICAP_BASE + 16 && size == sizeof(uint32_t)) {  // MSICAP Interrupt Mask Bits
            host_caps.msicap.mmask = pkt->getLE<uint32_t>();
        }
        else if (offset == MSICAP_BASE + 20 && size == sizeof(uint32_t)) {  // MSICAP Interrupt Pending Bits
            host_caps.msicap.mpend = pkt->getLE<uint32_t>();
        }
        else if (offset == MSIXCAP_BASE && size == sizeof(uint16_t)) {  // MSIXCAP cap
            host_caps.msicap.mpend = pkt->getLE<uint16_t>();
        }
        else if (offset == MSIXCAP_BASE + 2 && size == sizeof(uint16_t)) {  // MSIXCAP Message Control
            val = pkt->getLE<uint16_t>();

            host_caps.mode = (val & 0x8000) ? INTERRUPT_MSIX : INTERRUPT_PIN;

            host_caps.msixcap.mxc &= ~0xC000;
            host_caps.msixcap.mxc |= (val & 0xC000);

            host_caps.vectors = (host_caps.msixcap.mxc & 0x07FF) + 1;

            DPRINTF(CXLDRAMsim3,"INTR    | MSI-X %s | %d vectors\n",
                    host_caps.mode == INTERRUPT_PIN ? "disabled" : "enabled",
                    host_caps.vectors);
        }
        // else if(offset == PXCAP_BASE && size == sizeof(uint16_t)){  // PXCAP Device id, next cap
        //     pxcap.pxid = pkt->getLE<uint16_t>();
        //     //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxid);
        // }
        // else if(offset == PXCAP_BASE + 2 && size == sizeof(uint16_t)){ // PXCAP capability
        //     pxcap.pxcap = pkt->getLE<uint16_t>();
        //    // DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxcap);
        // }
        // else if (offset == PXCAP_BASE + 4 && size == sizeof(uint16_t)) {  // PXCAP Device Capabilities
        //     pxcap.pxdcap = pkt->getLE<uint16_t>();
        //     //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxdc);
        // }
        else if (offset == PXCAP_BASE + 8 && size == sizeof(uint16_t)) {  // PXCAP Device control
            host_caps.pxcap.pxdc = pkt->getLE<uint16_t>();
            //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxdc);
        }
        else if (offset == PXCAP_BASE + 10 && size == sizeof(uint16_t)) {  // PXCAP Device Status
            val = pkt->getLE<uint16_t>();

            if (val & 0x0001) {
                host_caps.pxcap.pxds &= 0xFFFE;
            }
            if (val & 0x0002) {
                host_caps.pxcap.pxds &= 0xFFFD;
            }
            if (val & 0x0004) {
                host_caps.pxcap.pxds &= 0xFFFB;
            }
            if (val & 0x0008) {
                host_caps.pxcap.pxds &= 0xFFF7;
            }
            //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, val, pxcap.pxds);
        }
        // else if (offset == PXCAP_BASE + 12 && size == sizeof(uint32_t)) {  // PXCAP Link Capabilities
        //     pxcap.pxlcap = pkt->getLE<uint16_t>();
        //     //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxlcap);
        // }
        else if (offset == PXCAP_BASE + 16 && size == sizeof(uint16_t)) {  // PXCAP Link Control
            host_caps.pxcap.pxlc = pkt->getLE<uint16_t>();
            //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint16_t>(), pxcap.pxlc);
        }
        else if (offset == PXCAP_BASE + 18 && size == sizeof(uint16_t)) {  // PXCAP Link Status
            val = pkt->getLE<uint16_t>();

            if (val & 0x4000) {
                host_caps.pxcap.pxls &= 0xBFFF;
            }
            if (val & 0x8000) {
                host_caps.pxcap.pxls &= 0x7FFF;
            }
            //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, val, pxcap.pxls);
        }
        // else if (offset == PXCAP_BASE + 36 && size == sizeof(uint32_t)) {  // PXCAP Device Capabilities2
        //     pxcap.pxdcap2 = pkt->getLE<uint32_t>();
        //     //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint32_t>(), pxcap.pxdcap2);
        // }
        else if (offset == PXCAP_BASE + 40 && size == sizeof(uint32_t)) {  // PXCAP Device Control 2
            host_caps.pxcap.pxdc2 = pkt->getLE<uint32_t>();
            //DPRINTF(CXLDRAMsim3, "esj pcie offset = %x val= %lx, data = %lx",offset, pkt->getLE<uint32_t>(), pxcap.pxdc2);
        }
        else {
            // assert(0);
            warn("CXL_type3_interface: Invalid PCI config write offset: %#x size: %d\n",offset, size);
            DPRINTF(CXLDRAMsim3,"CXL_type3_interface: Invalid PCI config write offset: %#x size: %d\n",offset, size);
        }
        pkt->makeAtomicResponse();
    }


    return configDelay;
    }


    void CXLDRAMsim3::getVendorID(uint16_t &vid, uint16_t &ssvid) {
        vid = config.vendor;
        ssvid = config.subsystemVendorID;
    }

    void CXLDRAMsim3::regStats() {
        PciDevice::regStats();
        // stats.regStats();
    }

    void CXLDRAMsim3::resetStats() {
        PciDevice::resetStats();
        wrapper.resetStats();
        //esj 2025-02-01
        wrapper_write.resetStats();
    }


    void CXLDRAMsim3::startup()
    {
        startTick = curTick();
        // Tick dd = curTick();
        // Tick start = getTick(tickEvent);
        // DPRINTF(CXLDRAMsim3, "esj start tick = %d, curtick = %d\n",start,dd);
        // startTick = start;
        // kick off the clock ticks
        // schedule(tickEvent, clockEdge());
        schedule(tickEvent, clockEdge());
    }


    void CXLDRAMsim3::tick()
    {
        //esj 2025-01-31
        if(trace::is_timing_mode && trace::cxl_check != 0){
        // if(trace::cxl_check != 0){
            wrapper.tick();
            //esj 2025-02-01
            wrapper_write.tick();

            // is the connected port waiting for a retry, if so check the
            // state and send a retry if conditions have changed
            if (retryReq && nbrOutstanding() < wrapper.queueSize()) {
                DPRINTF(CXLDRAMsim3, "Tick(), retry == true\n");
                retryReq = false;
                pioPort.sendRetryReq();
                // cxl_port.sendRetryReq();
            }
        }

        schedule(tickEvent,
                 curTick() + wrapper.clockPeriod() * sim_clock::as_int::ns);
        // schedule(tickEvent,_curTick() + wrapper.clockPeriod() * sim_clock::as_int::ns);
    }
    void CXLDRAMsim3::sendResponse()
    {
        assert(!retryResp);
        assert(!responseQueue.empty());

        DPRINTF(CXLDRAMsim3, "Attempting to send response\n");
        PacketPtr pkts = responseQueue.front();
        DPRINTF(CXLDRAMsim3, "esj sendResponse, pkt add= %x, pkt isresponse = %d\n",pkts->getAddr(),pkts->isResponse());


        //esj 2025-05-19
        // bool success = pioPort.sendTimingResp(responseQueue.front());


        // esj 2025-05-20
        // Port& peer = pioPort.getPeer();
        // auto* req_port = dynamic_cast<gem5::RequestPort*>(&peer);
        // bool success = false;
        // if (req_port) {
        //     success = req_port->recvTimingResp(pkts);
        // }
        //

        //esj 2025-06-22
        bool success = Pio2.sendTimingResp(responseQueue.front());


        // bool success = cxl_port.sendTimingResp(responseQueue.front());

        if (success) {
            responseQueue.pop_front();

            DPRINTF(CXLDRAMsim3, "Have %d read, %d write, %d responses outstanding\n",
                    nbrOutstandingReads, nbrOutstandingWrites,
                    responseQueue.size());

            if (!responseQueue.empty() && !sendResponseEvent.scheduled()){
                DPRINTF(CXLDRAMsim3, "esj curtick : %d ",curTick());
                // DPRINTF(CXLDRAMsim3, "esj curtick : %d ",_curTick());
                int debug_check_dump = 1;
                schedule(sendResponseEvent, curTick());
                // schedule(sendResponseEvent, _curTick());
            }

            // esj 2025-05-20
            // if (nbrOutstanding() == 0)
            //     signalDrainDone();
        } else {
            retryResp = true;

            DPRINTF(CXLDRAMsim3, "Waiting for response retry\n");

            assert(!sendResponseEvent.scheduled());
        }
    }

    unsigned int CXLDRAMsim3::nbrOutstanding() const
    {
        return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
    }

    void CXLDRAMsim3::recvRespRetry()
    {
        DPRINTF(CXLDRAMsim3, "Retrying\n");

        assert(retryResp);
        retryResp = false;
        sendResponse();
    }

    void
    CXLDRAMsim3::change_cxl_mem_packet_size(PacketPtr pkt){
        if(pkt->is_cxl_mem && pkt->isRequest()){
            DPRINTF(CXLDRAMsim3, "%s before pkt size = %d\n",
                    __func__, pkt->getSize());
            pkt->setSize(pkt->origin_size);
            DPRINTF(CXLDRAMsim3, "%s after pkt size = %d\n",
                    __func__, pkt->getSize());
        }
        else if(pkt->is_cxl_mem && pkt->isResponse()){

            pkt->origin_size = pkt->getSize();
            unsigned int data_size = pkt->getSize() - pkt->req->getSize();

            DPRINTF(CXLDRAMsim3, "%s pkt size = %d, data size = %d\n",
                    __func__, pkt->getSize(), data_size);
            if(pkt->isRead()){
                pkt->setSize(data_size + cxl_mem_DRS_resp_size);
                DPRINTF(CXLDRAMsim3,
                        "%s read pkt size = %d, is read = %d\n",
                        __func__, pkt->getSize(), pkt->isRead());
            }
            else{
                pkt->setSize(data_size + cxl_mem_NDR_resp_size);
                DPRINTF(CXLDRAMsim3,
                        "%s read pkt size = %d, is write = %d\n",
                        __func__, pkt->getSize(), pkt->isWrite());
            }
        }
    }


    void CXLDRAMsim3::accessAndRespond(PacketPtr pkt)
    {
        DPRINTF(CXLDRAMsim3, "Access for address %x\n", pkt->getAddr());

        bool needsResponse = pkt->needsResponse();

        // do the actual memory access which also turns the packet into a
        // response
        DPRINTF(CXLDRAMsim3, "esj accessAndRespond, before access pkt add= %x, pkt isresponse = %d\n",pkt->getAddr(),pkt->isResponse());
        mem_.access(pkt);


        //esj 2024-10-22 set pkt size
        // change_cxl_mem_packet_size(pkt);


        DPRINTF(CXLDRAMsim3, "esj accessAndRespond, after access pkt add= %x, pkt isresponse = %d\n",pkt->getAddr(),pkt->isResponse());
        // turn packet around to go back to requestor if response expected
        if (needsResponse) {
            // access already turned the packet into a response
            assert(pkt->isResponse());
            // Here we pay for xbar additional delay and to process the payload
            // of the packet.
            Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
            // Tick time = _curTick() + pkt->headerDelay + pkt->payloadDelay;
            // DPRINTF(CXLDRAMsim3, "esj CXLDRAMsim3::accessAndRespond _curTick= %d header =%d, payloaddelay = %d, = %d\n",_curTick(),pkt->headerDelay,pkt->payloadDelay,time);
            DPRINTF(CXLDRAMsim3, "esj CXLDRAMsim3::accessAndRespond _curTick= %d header =%d, payloaddelay = %d, = %d\n",curTick(),pkt->headerDelay,pkt->payloadDelay,time);
            // Reset the timings of the packet
            pkt->headerDelay = pkt->payloadDelay = 0;

            DPRINTF(CXLDRAMsim3, "Queuing response for address %x\n",
                    pkt->getAddr());

            // queue it to be sent back
            responseQueue.push_back(pkt);
            DPRINTF(CXLDRAMsim3, "esj accessAndRespond, responseQueue pkt add= %x, pkt isresponse = %d\n",pkt->getAddr(),pkt->isResponse());

            // if we are not already waiting for a retry, or are scheduled
            // to send a response, schedule an event
            if (!retryResp && !sendResponseEvent.scheduled()){
                DPRINTF(CXLDRAMsim3, "%s pkt  = %s, retryResp = %d\n",__func__,pkt->print(),retryResp);
                schedule(sendResponseEvent, time);
            }

        } else {
            // queue the packet for deletion
            pendingDelete.reset(pkt);
        }
    }

    void CXLDRAMsim3::readComplete(unsigned id, uint64_t addr)
    {
        Addr offset = 0xB0000000;
        DPRINTF(CXLDRAMsim3, "[%s] Read to address %x complete\n", __func__ ,addr);

        // get the outstanding reads for the address in question
        auto p = outstandingReads.find(addr);
        assert(p != outstandingReads.end());

        // first in first out, which is not necessarily true, but it is
        // the best we can do at this point
        PacketPtr pkt = p->second.front();
        p->second.pop();

        if (p->second.empty())
            outstandingReads.erase(p);

        // no need to check for drain here as the next call will add a
        // response to the response queue straight away
        assert(nbrOutstandingReads != 0);
        --nbrOutstandingReads;

        // perform the actual memory access
        accessAndRespond(pkt);

    }

    void CXLDRAMsim3::writeComplete(unsigned id, uint64_t addr)
    {
        Addr offset = 0xB0000000;
        DPRINTF(CXLDRAMsim3, "[%s] Write to address %x complete\n",__func__ ,addr);

        // get the outstanding reads for the address in question
        //auto p = outstandingWrites.find(addr+offset); //esj 2024-04-24
        auto p = outstandingWrites.find(addr);
        assert(p != outstandingWrites.end());

        PacketPtr pkt = p->second.front();
        // we have already responded, and this is only to keep track of
        // what is outstanding
        p->second.pop();
        if (p->second.empty())
            outstandingWrites.erase(p);

        assert(nbrOutstandingWrites != 0);
        --nbrOutstandingWrites;

        // esj 2025-05-20
        // if (nbrOutstanding() == 0)
        //     signalDrainDone();

        trace::write_check = true;
    }

    void CXLDRAMsim3::readComplete_write(unsigned id, uint64_t addr)
    {
        Addr offset = 0xB0000000;
        DPRINTF(CXLDRAMsim3, "[%s] Read to address %x complete\n", __func__ ,addr);

        // get the outstanding reads for the address in question
        auto p = outstandingReads.find(addr);
        assert(p != outstandingReads.end());

        // first in first out, which is not necessarily true, but it is
        // the best we can do at this point
        PacketPtr pkt = p->second.front();
        p->second.pop();

        if (p->second.empty())
            outstandingReads.erase(p);

        // no need to check for drain here as the next call will add a
        // response to the response queue straight away
        assert(nbrOutstandingReads != 0);
        --nbrOutstandingReads;

        // perform the actual memory access
        accessAndRespond(pkt);

    }

    void CXLDRAMsim3::writeComplete_write(unsigned id, uint64_t addr)
    {
        Addr offset = 0xB0000000;
        DPRINTF(CXLDRAMsim3, "[%s] Write to address %x complete\n",__func__ ,addr);

        // get the outstanding reads for the address in question
        //auto p = outstandingWrites.find(addr+offset); //esj 2024-04-24
        auto p = outstandingWrites.find(addr);
        assert(p != outstandingWrites.end());

        PacketPtr pkt = p->second.front();
        // we have already responded, and this is only to keep track of
        // what is outstanding
        p->second.pop();
        if (p->second.empty())
            outstandingWrites.erase(p);

        assert(nbrOutstandingWrites != 0);
        --nbrOutstandingWrites;

        // esj 2025-05-20
        // if (nbrOutstanding() == 0)
        //     signalDrainDone();

        trace::write_check = true;
    }


    //esj 2025-01-31
    DrainState
    CXLDRAMsim3::drain()
    {
        // check our outstanding reads and writes and if any they need to
        // drain
        DPRINTF(CXLDRAMsim3, "esj CXLDRAMsim3::drain, nbrOutstanding() = %d\n",nbrOutstanding());
        return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
    }

    // Add load-locked to tracking list.  Should only be called if the
    // operation is a load and the LLSC flag is set.
    void
    CXLDRAMsim3::Memory::trackLoadLocked(PacketPtr pkt)
    {
        const RequestPtr &req = pkt->req;
        Addr paddr = LockedAddr::mask(req->getPaddr());

        // first we check if we already have a locked addr for this
        // xc.  Since each xc only gets one, we just update the
        // existing record with the new address.
        std::list<LockedAddr>::iterator i;

        for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
            if (i->matchesContext(req)) {
                DPRINTF(CXLDRAMsim3, "Modifying lock record: context %d addr %#x\n",
                        req->contextId(), paddr);
                i->addr = paddr;
                return;
            }
        }

        // no record for this xc: need to allocate a new one
        DPRINTF(CXLDRAMsim3, "Adding lock record: context %d addr %#x\n",
                req->contextId(), paddr);
        lockedAddrList.push_front(LockedAddr(req));
        backdoor.invalidate();
    }


    // Called on *writes* only... both regular stores and
    // store-conditional operations.  Check for conventional stores which
    // conflict with locked addresses, and for success/failure of store
    // conditionals.
    bool
    CXLDRAMsim3::Memory::checkLockedAddrList(PacketPtr pkt)
    {
        const RequestPtr &req = pkt->req;
        Addr paddr = LockedAddr::mask(req->getPaddr());
        bool isLLSC = pkt->isLLSC();

        // Initialize return value.  Non-conditional stores always
        // succeed.  Assume conditional stores will fail until proven
        // otherwise.
        bool allowStore = !isLLSC;

        // Iterate over list.  Note that there could be multiple matching records,
        // as more than one context could have done a load locked to this location.
        // Only remove records when we succeed in finding a record for (xc, addr);
        // then, remove all records with this address.  Failed store-conditionals do
        // not blow unrelated reservations.
        std::list<LockedAddr>::iterator i = lockedAddrList.begin();

        if (isLLSC) {
            while (i != lockedAddrList.end()) {
                if (i->addr == paddr && i->matchesContext(req)) {
                    // it's a store conditional, and as far as the memory system can
                    // tell, the requesting context's lock is still valid.
                    DPRINTF(CXLDRAMsim3, "StCond success: context %d addr %#x\n",
                            req->contextId(), paddr);
                    allowStore = true;
                    break;
                }
                // If we didn't find a match, keep searching!  Someone else may well
                // have a reservation on this line here but we may find ours in just
                // a little while.
                i++;
            }
            req->setExtraData(allowStore ? 1 : 0);
        }
        // LLSCs that succeeded AND non-LLSC stores both fall into here:
        if (allowStore) {
            // We write address paddr.  However, there may be several entries with a
            // reservation on this address (for other contextIds) and they must all
            // be removed.
            i = lockedAddrList.begin();
            while (i != lockedAddrList.end()) {
                if (i->addr == paddr) {
                    DPRINTF(CXLDRAMsim3, "Erasing lock record: context %d addr %#x\n",
                            i->contextId, paddr);
                    ContextID owner_cid = i->contextId;
                    assert(owner_cid != InvalidContextID);
                    ContextID requestor_cid = req->hasContextId() ?
                                            req->contextId() :
                                            InvalidContextID;
                    if (owner_cid != requestor_cid) {
                        ThreadContext* ctx = owner.system()->threads[owner_cid];
                        ctx->getIsaPtr()->globalClearExclusive();
                    }
                    i = lockedAddrList.erase(i);
                } else {
                    i++;
                }
            }
        }

        return allowStore;
    }

    void
    CXLDRAMsim3::Memory::setBackingStore(uint8_t* pmem_addr)
    {
        // If there was an existing backdoor, let everybody know it's going away.
        if (backdoor.ptr())
            backdoor.invalidate();

        uint8_t* backing_store = pmem_addr ? pmem_addr : ownedPmemAddr;

        // The back door can't handle interleaved memory.
        backdoor.ptr(range.interleaved() ? nullptr : backing_store);

        pmemAddr = backing_store;
    }

    Port &
    CXLDRAMsim3::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "pio") {
            return pioPort;
        }
        if (if_name == "Pio2") {
            return Pio2;
        }
        if (if_name == "dma") {
            return dmaPort;
        }
        return ClockedObject::getPort(if_name, idx);
    }

    //esj 2025-06-22

    CXLDRAMsim3::CXL_resp_port::CXL_resp_port(const std::string& _name,
                                            CXLDRAMsim3& _ctrl)
        : ResponsePort(_name), ctrl(_ctrl)
    {}
    CXLDRAMsim3::CXL_resp_port::~CXL_resp_port() = default;



    //esj 2025-04-30
    void
    CXLDRAMsim3::serialize(CheckpointOut &cp) const
    {
        DPRINTF(CXLDRAMsim3, "CXLDRAMsim3::serialize\n");
        PciDevice::serialize(cp);

        // SERIALIZE_ARRAY(config.data, sizeof(config.data) / sizeof(...));
        // SERIALIZE_ARRAY(msicap.data, sizeof(msicap.data) / sizeof(msicap.data[0]));
        // SERIALIZE_ARRAY(msixcap.data, sizeof(msixcap.data) / sizeof(msixcap.data[0]));
        // SERIALIZE_ARRAY(pxcap.data, sizeof(pxcap.data) / sizeof(pxcap.data[0]));



        // SERIALIZE_ARRAY(cxl_config.data, sizeof(cxl_config.data)/sizeof(cxl_config.data[0])) ;
        // SERIALIZE_SCALAR(cxl_config.hdr);
        // SERIALIZE_SCALAR(cxl_config.cap);
        // SERIALIZE_SCALAR(cxl_config.ctrl);
        // SERIALIZE_SCALAR(cxl_config.status);
        // SERIALIZE_SCALAR(cxl_config.ctrl2);
        // SERIALIZE_SCALAR(cxl_config.status2);
        // SERIALIZE_SCALAR(cxl_config.lock);
        // SERIALIZE_SCALAR(cxl_config.cap2);
        // SERIALIZE_SCALAR(cxl_config.cap3);
        // SERIALIZE_SCALAR(cxl_config.range1_size_hi);
        // SERIALIZE_SCALAR(cxl_config.range1_size_lo);
        // SERIALIZE_SCALAR(cxl_config.range1_base_hi);
        // SERIALIZE_SCALAR(cxl_config.range1_base_lo);
        // SERIALIZE_SCALAR(cxl_config.range2_size_hi);
        // SERIALIZE_SCALAR(cxl_config.range2_size_lo);
        // SERIALIZE_SCALAR(cxl_config.range2_base_hi);
        // SERIALIZE_SCALAR(cxl_config.range2_base_lo);


    }
    void
    CXLDRAMsim3::unserialize(CheckpointIn &cp)
    {
        DPRINTF(CXLDRAMsim3, "CXLDRAMsim3::unserialize\n");
        PciDevice::unserialize(cp);
        // UNSERIALIZE_ARRAY(config.data, sizeof(config.data) / sizeof(config.data[0]));
        // UNSERIALIZE_ARRAY(msicap.data, sizeof(msicap.data) / sizeof(msicap.data[0]));
        // UNSERIALIZE_ARRAY(msixcap.data, sizeof(msixcap.data) / sizeof(msixcap.data[0]));
        // UNSERIALIZE_ARRAY(pxcap.data, sizeof(pxcap.data) / sizeof(pxcap.data[0]));

        // UNSERIALIZE_ARRAY(cxl_config.data, sizeof(cxl_config.data)/sizeof(cxl_config.data[0])) ;
        // UNSERIALIZE_SCALAR(cxl_config.hdr);
        // UNSERIALIZE_SCALAR(cxl_config.cap);
        // UNSERIALIZE_SCALAR(cxl_config.ctrl);
        // UNSERIALIZE_SCALAR(cxl_config.status);
        // UNSERIALIZE_SCALAR(cxl_config.ctrl2);
        // UNSERIALIZE_SCALAR(cxl_config.status2);
        // UNSERIALIZE_SCALAR(cxl_config.lock);
        // UNSERIALIZE_SCALAR(cxl_config.cap2);
        // UNSERIALIZE_SCALAR(cxl_config.cap3);
        // UNSERIALIZE_SCALAR(cxl_config.range1_size_hi);
        // UNSERIALIZE_SCALAR(cxl_config.range1_size_lo);
        // UNSERIALIZE_SCALAR(cxl_config.range1_base_hi);
        // UNSERIALIZE_SCALAR(cxl_config.range1_base_lo);
        // UNSERIALIZE_SCALAR(cxl_config.range2_size_hi);
        // UNSERIALIZE_SCALAR(cxl_config.range2_size_lo);
        // UNSERIALIZE_SCALAR(cxl_config.range2_base_hi);
        // UNSERIALIZE_SCALAR(cxl_config.range2_base_lo);
    }

} // namespace gem5
