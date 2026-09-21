#include "dev/PciBridge2.hh"

#include <algorithm>
#include <sstream>
#include <vector>

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/PciBridge2.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "params/PciBridge2.hh"
#include "sim/byteswap.hh"
#include "sim/core.hh"

namespace gem5
{
namespace
{

constexpr uint8_t PciBridgeStatsHostCount = 4;

uint8_t
clampStatsHostIdx(uint8_t host_idx)
{
    if (host_idx >= PciBridgeStatsHostCount) {
        return PciBridgeStatsHostCount - 1;
    }
    return host_idx;
}

uint8_t
packetStatsHostIdx(const PacketPtr pkt)
{
    return clampStatsHostIdx(pkt->source_host_idx);
}

uint64_t
ticksToNs(Tick ticks)
{
    return ticks / sim_clock::as_int::ns;
}

constexpr uint32_t CXL_CM_OFFSET = 0x1000;
constexpr uint32_t CXL_CM_HDM_REL_OFFSET = 0x100;
constexpr uint32_t CXL_HDM_OFFSET = CXL_CM_OFFSET + CXL_CM_HDM_REL_OFFSET;
constexpr uint32_t CXL_HDM_GLOBAL_CTRL = CXL_HDM_OFFSET + 0x4;
constexpr uint32_t CXL_HDM_DECODER0_BASE_LOW = CXL_HDM_OFFSET + 0x10;
constexpr uint32_t CXL_HDM_DECODER0_BASE_HIGH = CXL_HDM_OFFSET + 0x14;
constexpr uint32_t CXL_HDM_DECODER0_SIZE_LOW = CXL_HDM_OFFSET + 0x18;
constexpr uint32_t CXL_HDM_DECODER0_SIZE_HIGH = CXL_HDM_OFFSET + 0x1c;
constexpr uint32_t CXL_HDM_DECODER0_CTRL = CXL_HDM_OFFSET + 0x20;
constexpr uint32_t CXL_HDM_DECODER0_TL_LOW = CXL_HDM_OFFSET + 0x24;
constexpr uint32_t CXL_HDM_DECODER0_TL_HIGH = CXL_HDM_OFFSET + 0x28;
constexpr uint32_t CXL_HDM_TARGET_COUNT_ONE = 1 << 4;
constexpr uint32_t CXL_HDM_DECODER_ENABLE = 1 << 1;
constexpr uint32_t CXL_HDM_DECODER0_CTRL_IG_4K = 4;
constexpr uint32_t CXL_HDM_DECODER0_CTRL_COMMIT = 1 << 9;
constexpr uint32_t CXL_HDM_DECODER0_CTRL_COMMITTED = 1 << 10;
constexpr uint32_t CXL_HDM_DECODER0_CTRL_HOSTONLY = 1 << 12;

template <std::size_t N>
void
writeLe(std::array<uint8_t, N> &regs, uint32_t offset, uint64_t value,
        unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        regs[offset + i] = (value >> (8 * i)) & 0xff;
}

std::array<uint8_t, 4>
parsePriorityHostRatiosCsv(const std::string &csv)
{
    std::array<uint8_t, 4> ratios{{10, 0, 0, 0}};
    if (csv.empty()) {
        return ratios;
    }

    std::array<uint8_t, 4> parsed{{0, 0, 0, 0}};
    std::stringstream ss(csv);
    std::string token;
    int idx = 0;
    while (std::getline(ss, token, ',')) {
        if (idx >= 4) {
            warn("priority_host_ratios '%s' has more than 4 values; extra values are ignored",
                 csv);
            break;
        }

        try {
            int value = std::stoi(token);
            if (value < 0) {
                warn("priority_host_ratios has negative value %d at index %d; clamped to 0",
                     value, idx);
                value = 0;
            }
            if (value > 64) {
                warn("priority_host_ratios value %d at index %d is too large; clamped to 64",
                     value, idx);
                value = 64;
            }
            parsed[idx] = static_cast<uint8_t>(value);
        } catch (...) {
            warn("priority_host_ratios token '%s' is invalid; treated as 0", token);
            parsed[idx] = 0;
        }
        ++idx;
    }

    int sum = 0;
    for (auto v : parsed) {
        sum += v;
    }
    if (sum == 0) {
        warn("priority_host_ratios '%s' resolved to all zeros; using default 10,0,0,0", csv);
        return ratios;
    }

    return parsed;
}

} // namespace

PciBridge2::BridgeStats::BridgeStats(PciBridge2 &_bridge)
    : statistics::Group(&_bridge),
      bridge(_bridge),
      ADD_STAT(request_enqueue_count, statistics::units::Count::get(),
               "Requests enqueued into the bridge request queues per host"),
      ADD_STAT(request_grant_count, statistics::units::Count::get(),
               "Requests selected by bridge arbitration and sent per host"),
      ADD_STAT(request_dequeue_count, statistics::units::Count::get(),
               "Requests removed from the bridge request queues per host"),
      ADD_STAT(request_queue_full_count, statistics::units::Count::get(),
               "Request queue full stalls per source host"),
      ADD_STAT(request_send_blocked_count, statistics::units::Count::get(),
               "Request sends blocked by downstream backpressure per host"),
      ADD_STAT(request_wait_ns, statistics::units::ns::get(),
               "Total request queue wait time in ns per host"),
      ADD_STAT(request_wait_cycles, statistics::units::Cycle::get(),
               "Total request queue wait time in bridge cycles per host"),
      ADD_STAT(request_wait_avg_ns, statistics::units::Rate<
                   statistics::units::ns, statistics::units::Count>::get(),
               "Average request queue wait time in ns per host"),
      ADD_STAT(request_wait_avg_cycles, statistics::units::Ratio::get(),
               "Average request queue wait time in bridge cycles per host"),
      ADD_STAT(request_queue_occupancy_total, statistics::units::Count::get(),
               "Sum of request queue occupancy samples per host"),
      ADD_STAT(request_queue_occupancy_samples,
               statistics::units::Count::get(),
               "Number of request queue occupancy samples per host"),
      ADD_STAT(request_queue_occupancy_max, statistics::units::Count::get(),
               "Maximum request queue occupancy observed per host"),
      ADD_STAT(request_queue_occupancy_avg, statistics::units::Ratio::get(),
               "Average request queue occupancy per host"),
      ADD_STAT(request_wait_ns_hist,
               "Distribution of bridge request queue wait time in ns"),
      ADD_STAT(request_queue_occupancy_hist,
               "Distribution of bridge request queue occupancy"),
      ADD_STAT(response_enqueue_count, statistics::units::Count::get(),
               "Responses enqueued into the bridge response queues per host"),
      ADD_STAT(response_dequeue_count, statistics::units::Count::get(),
               "Responses removed from the bridge response queues per host"),
      ADD_STAT(response_queue_full_count, statistics::units::Count::get(),
               "Response reservation queue full stalls per source host"),
      ADD_STAT(response_send_blocked_count, statistics::units::Count::get(),
               "Response sends blocked by upstream backpressure per host"),
      ADD_STAT(response_wait_ns, statistics::units::ns::get(),
               "Total response queue wait time in ns per host"),
      ADD_STAT(response_wait_cycles, statistics::units::Cycle::get(),
               "Total response queue wait time in bridge cycles per host"),
      ADD_STAT(response_wait_avg_ns, statistics::units::Rate<
                   statistics::units::ns, statistics::units::Count>::get(),
               "Average response queue wait time in ns per host"),
      ADD_STAT(response_wait_avg_cycles, statistics::units::Ratio::get(),
               "Average response queue wait time in bridge cycles per host"),
      ADD_STAT(response_queue_occupancy_total, statistics::units::Count::get(),
               "Sum of response queue occupancy samples per host"),
      ADD_STAT(response_queue_occupancy_samples,
               statistics::units::Count::get(),
               "Number of response queue occupancy samples per host"),
      ADD_STAT(response_queue_occupancy_max, statistics::units::Count::get(),
               "Maximum response queue occupancy observed per host"),
      ADD_STAT(response_queue_occupancy_avg, statistics::units::Ratio::get(),
               "Average response queue occupancy per host"),
      ADD_STAT(response_wait_ns_hist,
               "Distribution of bridge response queue wait time in ns"),
      ADD_STAT(response_queue_occupancy_hist,
               "Distribution of bridge response queue occupancy")
{
}

void
PciBridge2::BridgeStats::regStats()
{
    using namespace statistics;

    const char *host_names[PciBridgeStatsHostCount] = {
        "host0", "host1", "host2", "host3"
    };

    auto init_host_vector = [&](statistics::Vector &stat) {
        stat.init(PciBridgeStatsHostCount).flags(nozero | nonan);
        for (uint8_t i = 0; i < PciBridgeStatsHostCount; ++i) {
            stat.subname(i, host_names[i]);
        }
    };

    init_host_vector(request_enqueue_count);
    init_host_vector(request_grant_count);
    init_host_vector(request_dequeue_count);
    init_host_vector(request_queue_full_count);
    init_host_vector(request_send_blocked_count);
    init_host_vector(request_wait_ns);
    init_host_vector(request_wait_cycles);
    init_host_vector(request_queue_occupancy_total);
    init_host_vector(request_queue_occupancy_samples);
    init_host_vector(request_queue_occupancy_max);

    init_host_vector(response_enqueue_count);
    init_host_vector(response_dequeue_count);
    init_host_vector(response_queue_full_count);
    init_host_vector(response_send_blocked_count);
    init_host_vector(response_wait_ns);
    init_host_vector(response_wait_cycles);
    init_host_vector(response_queue_occupancy_total);
    init_host_vector(response_queue_occupancy_samples);
    init_host_vector(response_queue_occupancy_max);

    request_wait_avg_ns
        .flags(nonan)
        .precision(2);
    request_wait_avg_cycles
        .flags(nonan)
        .precision(2);
    request_queue_occupancy_avg
        .flags(nonan)
        .precision(2);
    response_wait_avg_ns
        .flags(nonan)
        .precision(2);
    response_wait_avg_cycles
        .flags(nonan)
        .precision(2);
    response_queue_occupancy_avg
        .flags(nonan)
        .precision(2);

    for (uint8_t i = 0; i < PciBridgeStatsHostCount; ++i) {
        request_wait_avg_ns.subname(i, host_names[i]);
        request_wait_avg_cycles.subname(i, host_names[i]);
        request_queue_occupancy_avg.subname(i, host_names[i]);
        response_wait_avg_ns.subname(i, host_names[i]);
        response_wait_avg_cycles.subname(i, host_names[i]);
        response_queue_occupancy_avg.subname(i, host_names[i]);
    }

    request_wait_ns_hist
        .init(100)
        .flags(nozero | pdf | oneline);
    request_queue_occupancy_hist
        .init(100)
        .flags(nozero | pdf | oneline);
    response_wait_ns_hist
        .init(100)
        .flags(nozero | pdf | oneline);
    response_queue_occupancy_hist
        .init(100)
        .flags(nozero | pdf | oneline);

    request_wait_avg_ns = request_wait_ns / request_dequeue_count;
    request_wait_avg_cycles = request_wait_cycles / request_dequeue_count;
    request_queue_occupancy_avg =
        request_queue_occupancy_total / request_queue_occupancy_samples;
    response_wait_avg_ns = response_wait_ns / response_dequeue_count;
    response_wait_avg_cycles = response_wait_cycles / response_dequeue_count;
    response_queue_occupancy_avg =
        response_queue_occupancy_total / response_queue_occupancy_samples;
}

    PciBridge2::PciBridgeResponsePort::PciBridgeResponsePort(const std::string& _name,
                                         PciBridge2& _bridge,
                                         bool upstream ,
                                         uint8_t _respID ,
                                         uint8_t _rcid ,
                                         Cycles _delay,
                                        //  Tick _delay,
                                         int _resp_limit
                                          )
    : ResponsePort(_name, &_bridge), bridge(_bridge),
      delay(_delay),
      outstandingResponses(0), retryReq(false), respQueueLimit(_resp_limit),
      sendEvent([this]{ trySendTiming(); }, _name)
{
  is_upstream = upstream ; // this is an upstream resp port used to accept requests from CPU.
  respID = _respID ;
  rcid = _rcid ;
  // resp ID : 0 for upstream root port , 1 for downstream root port 1 , 2 for downstream root port 2 , 3 for downstream root port 3 .
}

PciBridge2::PciBridgeRequestPort::PciBridgeRequestPort(const std::string& _name,
                                           PciBridge2& _bridge, uint8_t _rcid ,
                                           Cycles _delay,
                                        //    Tick _delay,
                                           int _req_limit)
    : RequestPort(_name, &_bridge), bridge(_bridge), totalCount(0) ,
      delay(_delay), reqQueueLimit(_req_limit),
      sendEvent([this]{ trySendTiming(); }, _name) ,
      countEvent([this]{ incCount() ; } , _name ),
      next_transaction_list_index(0)
{
rcid = _rcid ;
bridge.schedule(countEvent, curTick() + (uint64_t)1000000000000) ;

}

PciBridge2::~PciBridge2()
{
  destroyHostDownstreamStorage();
  delete storage_ptr1 ;
  delete storage_ptr2 ;
  delete storage_ptr3 ;
  delete storage_ptr4 ;
  //esj 2025-04-21
  delete storage_ptr5 ;
  delete storage_ptr6 ;
  delete storage_ptr7 ;
}

uint8_t
PciBridge2::clampHostIdx(uint8_t host_idx) const
{
    return clampStatsHostIdx(host_idx);
}

config_class *
PciBridge2::cloneStorage(const config_class *storage_ptr)
{
    auto *clone = new config_class(*storage_ptr);
    clone->bridge = this;
    return clone;
}

void
PciBridge2::initHostDownstreamStorage()
{
    for (auto &slots : hostDownstreamStorage) {
        slots.fill(nullptr);
    }

    hostDownstreamStorage[0][0] = storage_ptr1;
    hostDownstreamStorage[0][1] = storage_ptr2;
    hostDownstreamStorage[0][2] = storage_ptr3;

    for (uint8_t host_idx = 1; host_idx < Packet::MaxPciRequesterIds;
         ++host_idx) {
        hostDownstreamStorage[host_idx][0] = cloneStorage(storage_ptr1);
        hostDownstreamStorage[host_idx][1] = cloneStorage(storage_ptr2);
        hostDownstreamStorage[host_idx][2] = cloneStorage(storage_ptr3);
    }
}

void
PciBridge2::destroyHostDownstreamStorage()
{
    for (uint8_t host_idx = 1; host_idx < Packet::MaxPciRequesterIds;
         ++host_idx) {
        for (auto *storage_ptr : hostDownstreamStorage[host_idx]) {
            delete storage_ptr;
        }
        hostDownstreamStorage[host_idx].fill(nullptr);
    }
    hostDownstreamStorage[0].fill(nullptr);
}

config_class *
PciBridge2::downstreamStorage(uint8_t port_idx, uint8_t host_idx) const
{
    if (port_idx >= hostDownstreamStorage[0].size()) {
        port_idx = hostDownstreamStorage[0].size() - 1;
    }

    host_idx = clampHostIdx(host_idx);
    auto *storage_ptr = hostDownstreamStorage[host_idx][port_idx];
    return storage_ptr ? storage_ptr : hostDownstreamStorage[0][port_idx];
}

config_class *
PciBridge2::upstreamStorage(uint8_t host_idx) const
{
    switch (clampHostIdx(host_idx)) {
      case 0:
        return storage_ptr4;
      case 1:
        return storage_ptr5;
      case 2:
        return storage_ptr6;
      default:
        return storage_ptr7;
    }
}

void PciBridge2::initialize_ports ( config_class * storage_ptr , const PciBridge2Params &p)
{
  storage_ptr->valid_io_limit = false ;
  storage_ptr->valid_io_base = false ;
  storage_ptr->valid_memory_base = false ;
  storage_ptr->valid_memory_limit = false ;
  storage_ptr->valid_prefetchable_memory_base = false ;
  storage_ptr->valid_prefetchable_memory_limit = false ;
  storage_ptr->is_switch = p.is_switch ;
  storage_ptr->storage1.VendorId = htole(p.VendorId) ;
  storage_ptr->storage1.Command = htole(p.Command) ;
  storage_ptr->storage1.Status = htole(p.Status) ;
  storage_ptr->storage1.ClassCode = htole(p.ClassCode) ;
  storage_ptr->storage1.SubClassCode = htole(p.SubClassCode) ;
  storage_ptr->storage1.ProgIF = htole(p.ProgIF) ;
  storage_ptr->storage1.Revision = htole(p.Revision) ;
  storage_ptr->storage1.BIST = htole(p.BIST) ;
  storage_ptr->storage1.HeaderType = htole(p.HeaderType) ;
  storage_ptr->storage1.LatencyTimer = htole(p.LatencyTimer) ;
  storage_ptr->storage1.CacheLineSize = htole(p.CacheLineSize) ;
  storage_ptr->storage1.Bar0 = htole(p.Bar0) ;
  storage_ptr->storage1.Bar1 = htole(p.Bar1) ;
  storage_ptr->storage1.SecondaryLatencyTimer = htole(p.SecondaryLatencyTimer) ;
  storage_ptr->storage1.MemoryLimit = htole(p.MemoryLimit) ;
  storage_ptr->storage1.MemoryBase = htole(p.MemoryBase) ;
  storage_ptr->storage1.PrefetchableMemoryLimit = htole(p.PrefetchableMemoryLimit) ;
  storage_ptr->storage1.PrefetchableMemoryBase = htole(p.PrefetchableMemoryBase) ;
  storage_ptr->storage1.PrefetchableMemoryLimitUpper = htole(p.PrefetchableMemoryLimitUpper) ;
  storage_ptr->storage1.PrefetchableMemoryBaseUpper = htole(p.PrefetchableMemoryBaseUpper) ;
  storage_ptr->storage1.IOLimit = htole(p.IOLimit) ;
  storage_ptr->storage1.IOBase = htole(p.IOBase) ;
  storage_ptr->storage1.IOLimitUpper = htole(p.IOLimitUpper) ;
  storage_ptr->storage1.IOBaseUpper = htole(p.IOBaseUpper) ;
  storage_ptr->storage1.CapabilityPointer = htole(p.CapabilityPointer) ;
  storage_ptr->storage1.ExpansionROMBaseAddress = htole(p.ExpansionROMBaseAddress) ;
  storage_ptr->storage1.InterruptPin = htole(p.InterruptPin);
  storage_ptr->storage1.InterruptLine = htole(p.InterruptLine) ;
  storage_ptr->storage1.BridgeControl = htole(p.BridgeControl) ;
  storage_ptr->storage1.SecondaryStatus = htole(p.SecondaryStatus);
  storage_ptr->storage1.PrimaryBusNumber = htole(p.PrimaryBusNumber) ;
  storage_ptr->storage1.SecondaryBusNumber = htole(p.SecondaryBusNumber) ;
  storage_ptr->storage1.SubordinateBusNumber = htole(p.SubordinateBusNumber) ;
  for (int i = 0; i<3; i++) storage_ptr->storage1.reserved[i] = 0 ;  // set the reserved values according to PCI bridge header specifications to 0

  // Now need to configure the PCIe capability field , which is implemented for every PCIe device, including a root port
  storage_ptr->storage2.PXCAPNextCapability = htole(p.PXCAPNextCapability) ;
  storage_ptr->storage2.PXCAPCapId = htole(p.PXCAPCapId) ;
 // storage_ptr->storage2.PXCAPCapabilities = htole(p.PXCAPCapabilities) ;
  storage_ptr->storage2.PXCAPDevCapabilities = htole(p.PXCAPDevCapabilities) ;
  storage_ptr->storage2.PXCAPDevCtrl = htole(p.PXCAPDevCtrl) ;
  storage_ptr->storage2.PXCAPDevStatus = htole(p.PXCAPDevStatus) ;
  storage_ptr->storage2.PXCAPLinkCap = htole(p.PXCAPLinkCap) ;
  storage_ptr->storage2.PXCAPLinkCtrl = htole(p.PXCAPLinkCtrl) ;
  storage_ptr->storage2.PXCAPLinkStatus = htole(p.PXCAPLinkStatus) ;
  storage_ptr->storage2.PXCAPRootStatus = htole(p.PXCAPRootStatus) ;
  storage_ptr->storage2.PXCAPRootControl = htole(p.PXCAPRootControl) ;

  storage_ptr->storage2.PXCAPSlotCapabilities = 0 ;
  storage_ptr->storage2.PXCAPSlotControl = 0 ;
  storage_ptr->storage2.PXCAPSlotStatus = 0 ;

  storage_ptr->configDelay = p.config_latency ; // Latency for configuration space accesses. Copied over from PCI device configuration access time.
  storage_ptr->barsize[0] = p.BAR0Size ;
  storage_ptr->barsize[1] = p.BAR1Size ; // indicates whether BAR for the bridges are implemented or not.
  storage_ptr->barflags[0] = 0 ;
  storage_ptr->barflags[1] = 0 ;
  storage_ptr->PXCAPBaseOffset = p.PXCAPBaseOffset ;
  storage_ptr->is_valid = 0 ;

    //////////////esj 2024-05-20
    storage_ptr->MSICAP_BASE = p.MSICAPBaseOffset;
    storage_ptr->MSIXCAP_BASE = (p.MSIXCAPBaseOffset);
    storage_ptr->MSIXCAP_ID_OFFSET = (p.MSIXCAPBaseOffset+MSIXCAP_ID);
    storage_ptr->MSIXCAP_MXC_OFFSET = (p.MSIXCAPBaseOffset+MSIXCAP_MXC);
    storage_ptr->MSIXCAP_MTAB_OFFSET = (p.MSIXCAPBaseOffset+MSIXCAP_MTAB);
    storage_ptr->MSIXCAP_MPBA_OFFSET = (p.MSIXCAPBaseOffset+MSIXCAP_MPBA);
   // MSICAP
    storage_ptr->msicap.mid = (uint16_t)p.MSICAPCapId; //mid.cid
    storage_ptr->msicap.mid |= (uint16_t)p.MSICAPNextCapability << 8; //mid.next
    storage_ptr->msicap.mc = p.MSICAPMsgCtrl;
    storage_ptr->msicap.ma = p.MSICAPMsgAddr;
    storage_ptr->msicap.mua = p.MSICAPMsgUpperAddr;
    storage_ptr->msicap.md = p.MSICAPMsgData;
    storage_ptr->msicap.mmask = p.MSICAPMaskBits;
    storage_ptr->msicap.mpend = p.MSICAPPendingBits;

    // MSIXCAP
    storage_ptr->msixcap.mxid = (uint16_t)p.MSIXCAPCapId; //mxid.cid
    storage_ptr->msixcap.mxid |= (uint16_t)p.MSIXCAPNextCapability << 8; //mxid.next
    storage_ptr->msixcap.mxc = p.MSIXMsgCtrl;
    storage_ptr->msixcap.mtab = p.MSIXTableOffset;
    storage_ptr->msixcap.mpba = p.MSIXPbaOffset;

    // allocate MSIX structures if MSIXCAP_BASE
    // indicates the MSIXCAP is being used by having a
    // non-zero base address.
    // The MSIX tables are stored by the guest in
    // little endian byte-order as according the
    // PCIe specification.  Make sure to take the proper
    // actions when manipulating these tables on the host
    uint16_t msixcap_mxc_ts = storage_ptr->msixcap.mxc & 0x07ff;
    if (storage_ptr->MSIXCAP_BASE != 0x0) {
        int msix_vecs = msixcap_mxc_ts + 1;
        MSIXTable tmp1 = {{0UL,0UL,0UL,0UL}};
        storage_ptr->msix_table.resize(msix_vecs, tmp1);

        MSIXPbaEntry tmp2 = {0};
        int pba_size = msix_vecs / MSIXVECS_PER_PBA;
        if ((msix_vecs % MSIXVECS_PER_PBA) > 0) {
            pba_size++;
        }
        storage_ptr->msix_pba.resize(pba_size, tmp2);
    }
    storage_ptr->MSIX_TABLE_OFFSET = storage_ptr->msixcap.mtab & 0xfffffffc;
    storage_ptr->MSIX_TABLE_END = storage_ptr->MSIX_TABLE_OFFSET +
                     (msixcap_mxc_ts + 1) * sizeof(MSIXTable);
    storage_ptr->MSIX_PBA_OFFSET = storage_ptr->msixcap.mpba & 0xfffffffc;
    storage_ptr->MSIX_PBA_END = storage_ptr->MSIX_PBA_OFFSET +
                   ((msixcap_mxc_ts + 1) / MSIXVECS_PER_PBA)
                   * sizeof(MSIXPbaEntry);
    if (((msixcap_mxc_ts + 1) % MSIXVECS_PER_PBA) > 0) {
        storage_ptr->MSIX_PBA_END += sizeof(MSIXPbaEntry);
    }
  /////////////////

}

PciBridge2::PciBridge2(const PciBridge2Params &p)
    /*: SimObject(p),

      responsePort(p.name + ".response", *this, true,0, p.rc_id , p.delay, p.resp_size),
      responsePort_DMA1( p.name + ".response_dma1" , *this , false, 1 , p.rc_id , (p.delay) , p.resp_size) ,
      responsePort_DMA2(p.name + ".response_dma2" , *this , false ,2 , p.rc_id ,  (p.delay) , p.resp_size) ,
      responsePort_DMA3(p.name + ".response_dma3" , *this , false, 3, p.rc_id , (p.delay) , p.resp_size) ,
      requestPort_DMA(p.name + ".request_dma" , *this ,p.rc_id ,  (p.delay) , p.req_size) ,
      requestPort1(p.name + ".request1", *this,p.rc_id , (p.delay), p.req_size) ,
      requestPort2(p.name + ".request2" , *this , p.rc_id ,  (p.delay) , p.req_size) ,
      requestPort3(p.name + ".request3" , *this , p.rc_id , (p.delay) , p.req_size)
    */
    :ClockedObject(p),
    //esj 2025-04-21
    // responsePort(p.name + ".response", *this, true,0, p.rc_id , ticksToCycles(p.delay), p.resp_size),
    responsePort1(p.name + ".response1", *this, true,0, p.rc_id , ticksToCycles(p.delay), p.resp_size),
    responsePort2(p.name + ".response2", *this, true,4, p.rc_id_second , ticksToCycles(p.delay), p.resp_size),

    responsePort_DMA1(p.name + ".response_dma1" , *this , false, 1 , p.rc_id , ticksToCycles(p.delay) , p.resp_size) ,
    responsePort_DMA2(p.name + ".response_dma2" , *this , false ,2 , p.rc_id ,  ticksToCycles(p.delay) , p.resp_size) ,
    responsePort_DMA3(p.name + ".response_dma3" , *this , false, 3, p.rc_id , ticksToCycles(p.delay) , p.resp_size) ,

    //esj 2025-04-21
    // requestPort_DMA(p.name + ".request_dma" , *this ,p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort_DMA1(p.name + ".request_dma1" , *this ,p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort_DMA2(p.name + ".request_dma2" , *this ,p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort_DMA3(p.name + ".request_dma3" , *this ,p.rc_id_third ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort_DMA4(p.name + ".request_dma4" , *this ,p.rc_id_fourth ,  ticksToCycles(p.delay) , p.req_size) ,


    requestPort1(p.name + ".request1", *this,p.rc_id , ticksToCycles(p.delay), p.req_size) ,
    requestPort2(p.name + ".request2" , *this , p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort3(p.name + ".request3" , *this , p.rc_id , ticksToCycles(p.delay) , p.req_size),

    //esj 2025-06-22
    requestPort1_1(p.name + ".request1_1", *this,p.rc_id , ticksToCycles(p.delay), p.req_size) ,
    requestPort2_1(p.name + ".request2_1" , *this , p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort3_1(p.name + ".request3_1" , *this , p.rc_id , ticksToCycles(p.delay) , p.req_size),
    responsePort1_1(p.name + ".response1_1" , *this , true, 5, p.rc_id , ticksToCycles(p.delay) , p.resp_size),
    responsePort2_1(p.name + ".response2_1" , *this , true, 6, p.rc_id_second , ticksToCycles(p.delay) , p.resp_size),
    requestPort1_2(p.name + ".request1_2", *this,p.rc_id , ticksToCycles(p.delay), p.req_size) ,
    requestPort2_2(p.name + ".request2_2" , *this , p.rc_id ,  ticksToCycles(p.delay) , p.req_size) ,
    requestPort3_2(p.name + ".request3_2" , *this , p.rc_id , ticksToCycles(p.delay) , p.req_size),
    responsePort3(p.name + ".response3" , *this , true, 7, p.rc_id_third , ticksToCycles(p.delay) , p.resp_size),
    responsePort4(p.name + ".response4" , *this , true, 8, p.rc_id_fourth , ticksToCycles(p.delay) , p.resp_size),
    responsePort3_1(p.name + ".response3_1" , *this , true, 9, p.rc_id_third , ticksToCycles(p.delay) , p.resp_size),
    responsePort4_1(p.name + ".response4_1" , *this , true, 10, p.rc_id_fourth , ticksToCycles(p.delay) , p.resp_size),
    stats(*this)
{
  // cout<<"PCI Bridge name"<<p.name<<"\n"  ;
    is_transmit = p.is_transmit ;
    cxlChbsBase = p.cxl_chbs_base;
    cxlChbsSize = p.cxl_chbs_size;
    cxlHdmBase = p.cxl_hdm_base;
    cxlHdmSize = p.cxl_hdm_size;
    const auto &host_hdm_sizes = p.host_cxl_hdm_sizes;
    for (size_t host_idx = 0;
         host_idx < cxlHostBridgeRegsByHost.size(); ++host_idx) {
        const uint64_t hdm_size = host_idx < host_hdm_sizes.size() ?
            host_hdm_sizes[host_idx] : cxlHdmSize;
        auto &regs = cxlHostBridgeRegsByHost[host_idx];
        initCxlHostBridgeRegs(regs, hdm_size);
    }
    int pci_bus = (p.is_switch) ? p.pci_bus + 1 : p.pci_bus ;
    storage_ptr1 = new config_class (pci_bus , p.pci_dev1, p.pci_func1, 0) ;   // allocate a new config class to provide PCI functionality to bridge corresponding to first root port
    storage_ptr2 = new config_class (pci_bus , p.pci_dev2 , p.pci_func2,0) ;  // config structure for second root port
    storage_ptr3 = new config_class (pci_bus , p.pci_dev3 , p.pci_func3, 0) ;  // config structure for third root port
    storage_ptr4 = new config_class (p.pci_bus , p.pci_upstream_dev , p.pci_upstream_func, 0) ;
    //esj 2025-04-21
    storage_ptr5 = new config_class (p.pci_bus , p.pci_upstream_dev2 , p.pci_upstream_func2, 0) ;
    storage_ptr6 = new config_class (p.pci_bus , p.pci_upstream_dev3 , p.pci_upstream_func3, 0) ;
    storage_ptr7 = new config_class (p.pci_bus , p.pci_upstream_dev4 , p.pci_upstream_func4, 0) ;
    storage_ptr1->storage1.DeviceId = htole(p.DeviceId1) ;   // Assign values depending on the PCI-PCI Bridge header configured in python
    storage_ptr2->storage1.DeviceId = htole(p.DeviceId2) ;
    storage_ptr3->storage1.DeviceId = htole(p.DeviceId3) ;
    storage_ptr4->storage1.DeviceId = htole(p.DeviceId_upstream) ;
    //esj 2025-04-21
    storage_ptr5->storage1.DeviceId = htole(p.DeviceId_upstream) ;
    storage_ptr6->storage1.DeviceId = htole(p.DeviceId_upstream) ;
    storage_ptr7->storage1.DeviceId = htole(p.DeviceId_upstream) ;

    initialize_ports(storage_ptr1 , p) ;
    initialize_ports(storage_ptr2, p) ;
    initialize_ports(storage_ptr3, p) ;
    initialize_ports(storage_ptr4, p) ;
    //esj 2025-04-21
    initialize_ports(storage_ptr5, p) ;
    initialize_ports(storage_ptr6, p) ;
    initialize_ports(storage_ptr7, p) ;


    uint16_t capabilities = (is_switch) ? p.PXCAPCapabilities_downstream : p.PXCAPCapabilities ;
    storage_ptr4->storage2.PXCAPCapabilities = htole(p.PXCAPCapabilities_upstream) ;
    //esj 2025-04-21
    storage_ptr5->storage2.PXCAPCapabilities = htole(p.PXCAPCapabilities_upstream) ;
    storage_ptr6->storage2.PXCAPCapabilities = htole(p.PXCAPCapabilities_upstream) ;
    storage_ptr7->storage2.PXCAPCapabilities = htole(p.PXCAPCapabilities_upstream) ;

    storage_ptr1->storage2.PXCAPCapabilities = htole(capabilities) ;
    storage_ptr2->storage2.PXCAPCapabilities = htole(capabilities) ;
    storage_ptr3->storage2.PXCAPCapabilities = htole(capabilities) ;


    storage_ptr1->pci_bus = pci_bus ; // should be the same as the Primary Bus Number of the Bridge (Upstream bus).
    storage_ptr1->pci_dev = p.pci_dev1 ;
    storage_ptr1->pci_func = p.pci_func1 ; // should be 0 , because it is configured to be a single function device
    storage_ptr2->pci_bus = pci_bus ;
    storage_ptr2->pci_dev = p.pci_dev2 ;
    storage_ptr2->pci_func = p.pci_func2 ;
    storage_ptr3->pci_bus = pci_bus ;
    storage_ptr3->pci_dev = p.pci_dev3 ;
    storage_ptr3->pci_func = p.pci_func3 ;
    storage_ptr4->pci_bus = p.pci_bus ;
    storage_ptr4->pci_dev = p.pci_upstream_dev ;
    storage_ptr4->pci_func = p.pci_upstream_func ;
    //esj 2025-04-21
    storage_ptr5->pci_bus = p.pci_bus ;
    storage_ptr5->pci_dev = p.pci_upstream_dev2 ;
    storage_ptr5->pci_func = p.pci_upstream_func2 ;
    storage_ptr6->pci_bus = p.pci_bus ;
    storage_ptr6->pci_dev = p.pci_upstream_dev3 ;
    storage_ptr6->pci_func = p.pci_upstream_func3 ;
    storage_ptr7->pci_bus = p.pci_bus ;
    storage_ptr7->pci_dev = p.pci_upstream_dev4 ;
    storage_ptr7->pci_func = p.pci_upstream_func4 ;
    //

    storage_ptr1->id = 1 ;
    storage_ptr2->id = 2 ;
    storage_ptr3->id = 3 ;
    storage_ptr4->id = 0 ;
    //esj 2025-04-21
    storage_ptr5->id = 4 ;
    storage_ptr6->id = 7 ;
    storage_ptr7->id = 8 ;

    storage_ptr1->bridge = this ; // Assign the Port being used as the port that is being configured.
    storage_ptr2->bridge = this ;
    storage_ptr3->bridge = this ;
    storage_ptr4->bridge = this ;
    //esj 2025-04-21
    storage_ptr5->bridge = this ;
    storage_ptr6->bridge = this ;
    storage_ptr7->bridge = this ;
    initHostDownstreamStorage();

    p.host->registerBridge(downstreamStorage(0, 0),
                           downstreamStorage(0, 0)->BridgeAddr) ;
    p.host->registerBridge(downstreamStorage(1, 0),
                           downstreamStorage(1, 0)->BridgeAddr) ;
    p.host->registerBridge(downstreamStorage(2, 0),
                           downstreamStorage(2, 0)->BridgeAddr) ;
    if(p.is_switch) {
        p.host->registerBridge(storage_ptr4 , storage_ptr4->BridgeAddr) ;
        //esj 2025-04-21
        if (p.host_second) {
            p.host_second->registerBridge(storage_ptr5 , storage_ptr5->BridgeAddr) ;
            p.host_second->registerBridge(downstreamStorage(0, 1),
                                          downstreamStorage(0, 1)->BridgeAddr) ;
            p.host_second->registerBridge(downstreamStorage(1, 1),
                                          downstreamStorage(1, 1)->BridgeAddr) ;
            p.host_second->registerBridge(downstreamStorage(2, 1),
                                          downstreamStorage(2, 1)->BridgeAddr) ;
        }
        if (p.host_third) {
            p.host_third->registerBridge(storage_ptr6 , storage_ptr6->BridgeAddr) ;
            p.host_third->registerBridge(downstreamStorage(0, 2),
                                         downstreamStorage(0, 2)->BridgeAddr) ;
            p.host_third->registerBridge(downstreamStorage(1, 2),
                                         downstreamStorage(1, 2)->BridgeAddr) ;
            p.host_third->registerBridge(downstreamStorage(2, 2),
                                         downstreamStorage(2, 2)->BridgeAddr) ;
        }
        if (p.host_fourth) {
            p.host_fourth->registerBridge(storage_ptr7 , storage_ptr7->BridgeAddr) ;
            p.host_fourth->registerBridge(downstreamStorage(0, 3),
                                          downstreamStorage(0, 3)->BridgeAddr) ;
            p.host_fourth->registerBridge(downstreamStorage(1, 3),
                                          downstreamStorage(1, 3)->BridgeAddr) ;
            p.host_fourth->registerBridge(downstreamStorage(2, 3),
                                          downstreamStorage(2, 3)->BridgeAddr) ;
        }
    }
    else {
        p.host->registerBridge(storage_ptr4 , storage_ptr4->BridgeAddr) ;
        //esj 2025-04-21
        if (p.host_second) {
            p.host_second->registerBridge(storage_ptr5 , storage_ptr5->BridgeAddr) ;
        }
        if (p.host_third) {
            p.host_third->registerBridge(storage_ptr6 , storage_ptr6->BridgeAddr) ;
        }
        if (p.host_fourth) {
            p.host_fourth->registerBridge(storage_ptr7 , storage_ptr7->BridgeAddr) ;
        }
    }
    rc_id = p.rc_id ;
    //esj 2025-04-23
    rc_id_second = p.rc_id_second ;
    rc_id_third = p.rc_id_third ;
    rc_id_fourth = p.rc_id_fourth ;
    is_switch = p.is_switch ;

    //esj 2026-02-11
    if(p.routing_mode_type == "default"){
        routing_mode = DEFAULT;
    }
    else if(p.routing_mode_type == "round-robin"){
        routing_mode = ROUND_ROBIN;
    }
    else if(p.routing_mode_type == "priority"){
        routing_mode = PRIORITY;
    }
    else{
        warn("Invalid routing mode type: %s, using default", p.routing_mode_type.c_str());
        routing_mode = DEFAULT;
    }

    priority_limit = p.priority_limit;
    priority_host_ratios = parsePriorityHostRatiosCsv(p.priority_host_ratios);

}

void
PciBridge2::initCxlHostBridgeRegs(
    std::array<uint8_t, CxlHostBridgeRegSize> &regs,
    uint64_t hdm_size)
{
    regs.fill(0);
    writeLe(regs, CXL_CM_OFFSET + 0x0,
            0x1 | (0x1 << 16) | (0x1 << 20) | (0x1 << 24), 4);
    writeLe(regs, CXL_CM_OFFSET + 0x4,
            0x5 | (0x1 << 16) | (CXL_CM_HDM_REL_OFFSET << 20), 4);
    writeLe(regs, CXL_HDM_OFFSET + 0x0,
            CXL_HDM_TARGET_COUNT_ONE, 4);
    writeLe(regs, CXL_HDM_GLOBAL_CTRL,
            CXL_HDM_DECODER_ENABLE, 4);

    if (hdm_size != 0) {
        writeLe(regs, CXL_HDM_DECODER0_BASE_LOW,
                (uint32_t)cxlHdmBase, 4);
        writeLe(regs, CXL_HDM_DECODER0_BASE_HIGH,
                (uint32_t)(cxlHdmBase >> 32), 4);
        writeLe(regs, CXL_HDM_DECODER0_SIZE_LOW,
                (uint32_t)hdm_size, 4);
        writeLe(regs, CXL_HDM_DECODER0_SIZE_HIGH,
                (uint32_t)(hdm_size >> 32), 4);
        writeLe(regs, CXL_HDM_DECODER0_CTRL,
                CXL_HDM_DECODER0_CTRL_IG_4K |
                CXL_HDM_DECODER0_CTRL_COMMIT |
                CXL_HDM_DECODER0_CTRL_COMMITTED |
                CXL_HDM_DECODER0_CTRL_HOSTONLY, 4);
        writeLe(regs, CXL_HDM_DECODER0_TL_LOW, 0x1, 4);
        writeLe(regs, CXL_HDM_DECODER0_TL_HIGH, 0x0, 4);
    }
}

bool
PciBridge2::cxlHostBridgeOffset(Addr addr, Addr &offset) const
{
    if (cxlChbsSize == 0 || addr < cxlChbsBase)
        return false;

    const Addr relative = addr - cxlChbsBase;
    if (relative >= cxlChbsSize || relative >= CxlHostBridgeRegSize)
        return false;

    offset = relative;
    return true;
}

bool
PciBridge2::accessCxlHostBridgeMmio(PacketPtr pkt, uint8_t host_idx)
{
    Addr offset = 0;
    if (!cxlHostBridgeOffset(pkt->getAddr(), offset))
        return false;

    host_idx = clampHostIdx(host_idx);
    auto &regs = cxlHostBridgeRegsByHost[host_idx];
    const bool is_read = pkt->isRead();
    const bool is_write = pkt->isWrite();
    const unsigned size = pkt->getSize();
    std::vector<uint8_t> data(size, 0);

    if (is_read) {
        for (unsigned i = 0; i < size && offset + i < regs.size();
             ++i)
            data[i] = regs[offset + i];
        pkt->setData(data.data());
    } else if (is_write) {
        pkt->writeData(data.data());
        for (unsigned i = 0; i < size && offset + i < regs.size();
             ++i)
            regs[offset + i] = data[i];
    } else {
        return false;
    }

    if (pkt->needsResponse())
        pkt->makeResponse();

    return true;
}

Addr config_class::getBar0()
{
    //esj 2024-02-16
    uint32_t bar0 = letoh(storage1.Bar0) ;   // Assume it is only a 32-bit BAR and never a 64 bit one
    //uint64_t bar0 = letoh(storage1.Bar0) ;   // Assume it is only a 32-bit BAR and never a 64 bit one
    //DPRINTF(PciBridge2, "esj getbar0 before storage1.bar0 = %x/n",storage1.bar0);
    bar0 = bar0 & ~(barsize[0] - 1) ;
    //DPRINTF(PciBridge2, "esj getbar0 bar0 = %x/n",bar0);
    return (uint64_t)bar0 ;
}

Addr config_class::getBar1()
{
    //esj 2024-02-16
    uint32_t bar1 = letoh(storage1.Bar1) ;
    //uint64_t bar1 = letoh(storage1.Bar1) ;
    //DPRINTF(PciBridge2, "esj getbar1 before storage1.bar1 = %x/n",storage1.Bar1);
    bar1 = bar1 & ~(barsize[1] - 1 ) ;
    //DPRINTF(PciBridge2, "esj getbar1 bar1 = %x/n",bar1);
    return (uint64_t)bar1 ;
}

Addr config_class::getIOBase()
{
    uint8_t stored_base = letoh(storage1.IOBase) ;
    uint16_t stored_base_upper = letoh(storage1.IOBaseUpper) ;
    // if(id == 2 ) printf("Stored Base : %02x , Stored Base upper : %04x\n" , stored_base , stored_base_upper) ;
    uint32_t base_val = 0 ;
    base_val = (uint32_t)stored_base_upper ;
    base_val = base_val << 16 ;
    stored_base = stored_base >> 4 ;
    base_val += stored_base * IO_BASE_SHIFT ; // value of 32 bit IO Base
    return (Addr)base_val ;
    // Addr val = 0x2000000000000000;
    // return val ;
}

Addr config_class::getIOLimit()
{
    uint8_t stored_limit = letoh(storage1.IOLimit) ;
    uint16_t stored_limit_upper = letoh(storage1.IOLimitUpper) ;
    uint32_t limit_val = 0 ;
    limit_val = (uint32_t)stored_limit_upper ;
    limit_val = limit_val << 16 ;
    stored_limit = stored_limit >> 4 ;
    limit_val += stored_limit * IO_BASE_SHIFT ; // value of 32 bit IO Limit . Still need to make the lower bits all 1s to satisfy alignment.
    limit_val |= 0x00000FFF ;
    return (Addr)limit_val ;
    // Addr val = 0xF000000000000000;
    // return val ;
}
Addr config_class::getMemoryBase()
{
    uint16_t stored_base = letoh(storage1.MemoryBase) ;
    stored_base = stored_base >> 4 ;
    uint32_t base_val = (uint32_t)stored_base ;
    base_val = base_val << 20 ;
    return (Addr)base_val ;
    // Addr val = letoh(storage1.MemoryBase); //esj 2024-02-17
    // return val <<32 ;  //esj 2024-02-17
    // return (Addr)0xC000000000000000 ;
}

Addr config_class::getMemoryLimit()
{
    uint16_t stored_limit = letoh(storage1.MemoryLimit) ;
    stored_limit = stored_limit >> 4 ;
    uint32_t limit_val = (uint32_t)stored_limit ;
    limit_val = limit_val <<20 ;
    limit_val |= 0x000FFFFF ;   // make the botton 20 bits of memory limit all 1's
    return (Addr)limit_val ;
    // Addr val = letoh(storage1.MemoryLimit); //esj 2024-02-17
    // return val <<32;  //esj 2024-02-17
    // return (Addr)0xF000000000000000 ;
}

Addr config_class::getPrefetchableMemoryBase()
{
    uint16_t stored_base = letoh(storage1.PrefetchableMemoryBase) ;
    uint32_t stored_base_upper = letoh(storage1.PrefetchableMemoryBaseUpper) ;
    uint64_t base_val = 0 ;
    base_val = (uint64_t)stored_base_upper ;
    base_val = base_val << 32 ;
    stored_base = stored_base >> 4 ;
    base_val += stored_base * PREFETCH_BASE_SHIFT ;

    // base_val |= ((uint64_t)0x0000000000000001); //esj 2024-06-26

    return base_val ;
}

Addr config_class::getPrefetchableMemoryLimit()
{
    uint16_t stored_limit = letoh(storage1.PrefetchableMemoryLimit) ;
    uint16_t stored_limit_upper = letoh(storage1.PrefetchableMemoryLimitUpper) ;
    uint64_t limit_val = 0 ;
    limit_val = (uint64_t)stored_limit_upper ;
    limit_val = limit_val<<32 ;
    stored_limit = stored_limit >> 4 ;
    limit_val += stored_limit * PREFETCH_BASE_SHIFT ;
    limit_val |=((uint64_t)0x000FFFFF) ;
    return limit_val ;
}

PciBridge2::PciBridgeRequestPort*
PciBridge2::getRequestPort(Addr address, uint8_t source_host_idx)
{
    config_class *port1 = downstreamStorage(0, source_host_idx);
    config_class *port2 = downstreamStorage(1, source_host_idx);
    config_class *port3 = downstreamStorage(2, source_host_idx);

    DPRINTF(PciBridge2, "Request port for address %x\n", address) ;
    DPRINTF(PciBridge2, "storage_ptr1 membase = [%x - %x]\n", port1->getMemoryBase(),port1->getMemoryLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr2 membase = [%x - %x]\n", port2->getMemoryBase(),port2->getMemoryLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr3 membase = [%x - %x]\n", port3->getMemoryBase(),port3->getMemoryLimit()) ;

    DPRINTF(PciBridge2, "storage_ptr1 iobase = [%x - %x]\n", port1->getIOBase(),port1->getIOLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr2 iobase = [%x - %x]\n", port2->getIOBase(),port2->getIOLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr3 iobase = [%x - %x]\n", port3->getIOBase(),port3->getIOLimit()) ;

    DPRINTF(PciBridge2, "storage_ptr1 prefbase = [%x - %x]\n", port1->getPrefetchableMemoryBase(),port1->getPrefetchableMemoryLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr2 prefbase = [%x - %x]\n", port2->getPrefetchableMemoryBase(),port2->getPrefetchableMemoryLimit()) ;
    DPRINTF(PciBridge2, "storage_ptr3 prefbase = [%x - %x]\n", port3->getPrefetchableMemoryBase(),port3->getPrefetchableMemoryLimit()) ;

    if((address >= port1->getMemoryBase()) && (address <= port1->getMemoryLimit()) && (port1->getMemoryBase()!=0) &&(port1->getMemoryLimit()!=0)){
        DPRINTF(PciBridge2,"Request port 1 is being used\n") ;
        return (PciBridge2::PciBridgeRequestPort*) &requestPort1 ;
    }


    if((address >= port2->getMemoryBase()) && (address <= port2->getMemoryLimit()) && (port2->getMemoryBase()!=0) &&(port2->getMemoryLimit()!=0)){
        DPRINTF(PciBridge2,"Request port 2 is being used\n") ;
        return (PciBridge2::PciBridgeRequestPort*) &requestPort2 ;
    }


    if((address >= port3->getMemoryBase()) && (address <= port3->getMemoryLimit()) && (port3->getMemoryBase()!=0) &&(port3->getMemoryLimit()!=0)){
        DPRINTF(PciBridge2,"Request port 3 is being used\n") ;
        return (PciBridge2::PciBridgeRequestPort*) &requestPort3 ;
    }



    if((address >= port1->getPrefetchableMemoryBase()) && (address <= port1->getPrefetchableMemoryLimit()) && (port1->getPrefetchableMemoryBase()!=0) &&(port1->getPrefetchableMemoryLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort1 ;
    if((address >= port1->getIOBase()) && (address <= port1->getIOLimit()) && (port1->getIOBase()!=0) &&(port1->getIOLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort1 ;


    if((address >= port2->getPrefetchableMemoryBase()) && (address <= port2->getPrefetchableMemoryLimit()) && (port2->getPrefetchableMemoryBase()!=0) &&(port2->getPrefetchableMemoryLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort2 ;
    if((address >= port2->getIOBase()) && (address <= port2->getIOLimit()) && (port2->getIOBase()!=0) &&(port2->getIOLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort2 ;



    if((address >= port3->getPrefetchableMemoryBase()) && (address <= port3->getPrefetchableMemoryLimit()) && (port3->getPrefetchableMemoryBase()!=0) &&(port3->getPrefetchableMemoryLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort3 ;
    if((address >= port3->getIOBase()) && (address <= port3->getIOLimit()) && (port3->getIOBase()!=0) &&(port3->getIOLimit()!=0))
        return (PciBridge2::PciBridgeRequestPort*) &requestPort3 ;

    //printf("Forwarding packet to request port dma \n") ;
    //esj 2025-05-08
    if (source_host_idx == 0)
        return (PciBridge2::PciBridgeRequestPort*) &requestPort_DMA1;
    else if (source_host_idx == 1)
        return (PciBridge2::PciBridgeRequestPort*) &requestPort_DMA2 ;
    else if (source_host_idx == 2)
        return (PciBridge2::PciBridgeRequestPort*) &requestPort_DMA3 ;
    else
        return (PciBridge2::PciBridgeRequestPort*) &requestPort_DMA4 ;
    // If no downstream ports are willing to handle the request, route to DMA.
}

PciBridge2::PciBridgeResponsePort*

PciBridge2::getResponsePort(int bus_num)
{
    if ( (bus_num >= storage_ptr1->storage1.SecondaryBusNumber) && (bus_num <=storage_ptr1->storage1.SubordinateBusNumber) && (storage_ptr1->is_valid == 1))
        return (PciBridgeResponsePort*)&responsePort_DMA1 ;
    else if ( (bus_num >= storage_ptr2->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr2->storage1.SubordinateBusNumber) && (storage_ptr2->is_valid ==1))
    {
        //printf("Responseport 2 has primary bus number %02x , Subordinate Bus numberr %02x, given : %d\n", (uint8_t)storage_ptr2->storage1.PrimaryBusNumber, (uint8_t)storage_ptr2->storage1.SubordinateBusNumber, bus_num) ;
        return (PciBridgeResponsePort*) &responsePort_DMA2 ;
    }
    else if ( (bus_num >= storage_ptr3->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr3->storage1.SubordinateBusNumber) && (storage_ptr3->is_valid == 1))
        return (PciBridgeResponsePort*) &responsePort_DMA3 ;

    //printf("Returning upstream responsePort for bus num %d\n" , bus_num) ;

    //esj 2025-04-21
    else if ( (bus_num >= storage_ptr4->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr4->storage1.SubordinateBusNumber) && (storage_ptr4->is_valid == 1))
    //esj 2025-06-22
    // return (PciBridgeResponsePort*) &responsePort1 ;
        return (PciBridgeResponsePort*) &responsePort1_1 ;
    else if ( (bus_num >= storage_ptr5->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr5->storage1.SubordinateBusNumber) && (storage_ptr5->is_valid == 1))
        return (PciBridgeResponsePort*)&responsePort2_1 ;
    else if ( (bus_num >= storage_ptr6->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr6->storage1.SubordinateBusNumber) && (storage_ptr6->is_valid == 1))
        return (PciBridgeResponsePort*)&responsePort3_1 ;
    else if ( (bus_num >= storage_ptr7->storage1.SecondaryBusNumber) && (bus_num <= storage_ptr7->storage1.SubordinateBusNumber) && (storage_ptr7->is_valid == 1))
        return (PciBridgeResponsePort*)&responsePort4_1 ;
    // return (PciBridgeResponsePort*)&responsePort2 ;
    return (PciBridgeResponsePort*)&responsePort4_1 ;
    // return (PciBridgeResponsePort*)&responsePort ;
}
// Accepts an offset and decides if this location can be written to. Use the PCI bridge specification to decide what registers are RO or R/W.

bool config_class::isWritable(int offset)
{
  if (offset == PCI_VENDOR_ID || offset==PCI_DEVICE_ID || offset == PCI_REVISION_ID || offset ==PCI_CLASS_CODE || offset == PCI_SUB_CLASS_CODE || offset == PCI_BASE_CLASS_CODE || offset == PCI_CACHE_LINE_SIZE || offset == PCI_LATENCY_TIMER || offset == PCI_HEADER_TYPE || offset == PCI1_SEC_LAT_TIMER || offset == PCI1_RESERVED || offset == PCI1_INTR_PIN) return false ;

 return true ;
}




Tick config_class::readConfig(PacketPtr pkt)
{
  int offset = pkt->getAddr() & PCIE_CONFIG_SIZE ;
//   DPRINTF(PciBridge2, "esj read pkt->getSize()pkt->getSize()pkt->getSize() = %d\n", pkt->getSize());
  if (offset < PCIE_HEADER_SIZE)
  {
     // we now know that it is an access to standard PCI-PCI bridge header
     switch(pkt->getSize())
     {
       case sizeof(uint8_t): pkt->set<uint8_t>(*((uint8_t*)&storage1.data[offset]),ByteOrder::little) ; /*printf("Bridge offset %x data %x\n" , offset , *((uint8_t*)&storage1.data[offset])) ;*/ break ;
       case sizeof(uint16_t): pkt->set<uint16_t>(*((uint16_t*)&storage1.data[offset]),ByteOrder::little) ; /*printf("Bridge offset %x data %x\n" , offset ,*((uint16_t*)&storage1.data[offset])) ;*/break ;
       case sizeof(uint32_t): pkt->set<uint32_t>(*((uint32_t*)&storage1.data[offset]),ByteOrder::little) ; /*printf("Bridge offset %x data %x\n" , offset , *((uint32_t*)&storage1.data[offset])); */break ;
       default: panic("invalid access size\n") ;

     }
   }
  else if (offset >= PXCAPBaseOffset && offset < PXCAPBaseOffset + PXCAPSIZE)
  {
     switch(pkt->getSize())
     {
        case sizeof(uint8_t): pkt->set<uint8_t>(*((uint8_t*)&storage2.data[offset - PXCAPBaseOffset]),ByteOrder::little) ; break ;
        case sizeof(uint16_t): pkt->set<uint16_t>(*((uint16_t*)&storage2.data[offset - PXCAPBaseOffset]),ByteOrder::little) ; break ;
        case sizeof(uint32_t): pkt->set<uint32_t>(*((uint32_t*)&storage2.data[offset - PXCAPBaseOffset]),ByteOrder::little) ; break ;
        default:  panic("invalid access size\n") ;

     }
  }
  else
  {
    uint32_t val = 0;
        if(offset >= MSICAP_BASE && offset <= MSICAP_BASE + MSICAP_SIZE){
            for (int i = 0; i < pkt->getSize(); i++) {
            if (offset + i >= MSICAP_BASE &&
                    offset + i < MSICAP_BASE + MSICAP_SIZE) {
                val |= (uint32_t)msicap.data[offset + i - MSICAP_BASE] << (i * 8);
                }
            }
        }
        else if(offset >= MSIXCAP_BASE && offset <= MSIXCAP_BASE + MSIXCAP_SIZE){
            for (int i = 0; i < pkt->getSize(); i++) {
            if (offset + i >= MSIXCAP_BASE &&
                    offset + i < MSIXCAP_BASE + MSIXCAP_SIZE) {
                val |= (uint32_t)msixcap.data[offset + i - MSIXCAP_BASE] << (i * 8);
                }
            }
        }

    switch(pkt->getSize())
    {
       case sizeof(uint8_t): pkt->set<uint8_t>(val,ByteOrder::little) ; break ;
       case sizeof(uint16_t): pkt->set<uint16_t>(val,ByteOrder::little); break ;
       case sizeof(uint32_t):pkt->set<uint32_t>(val,ByteOrder::little); break ;
       default:  panic("invalid access size\n")  ;
    }
   }
  pkt->makeAtomicResponse() ;

  return configDelay ;
}

Tick config_class::writeConfig(PacketPtr pkt)
{
    uint16_t val = 0;
    int offset = pkt->getAddr() & PCIE_CONFIG_SIZE ;
    //   DPRINTF(PciBridge2, "esj pkt->getSize()pkt->getSize()pkt->getSize() = %d\n", pkt->getSize());
    // if((offset == PCI1_SUB_BUS_NUM) &&(id==3)) printf("Subordinate bus num written with size %d\n" , (int)pkt->getSize()) ;
    // if((offset == PCI1_SEC_BUS_NUM) &&(id==3)) printf("Secondary bus num written with size %d\n" , (int)pkt->getSize()) ;
    // if((offset == PCI1_PRI_BUS_NUM) &&(id==3)) printf("Primary bus num written with size %d\n" , (int)pkt->getSize()) ;
    //DPRINTF(PciBridge2, "LEEGI : offset = %d, PCIE_HEADER_SIZE = %d\n",offset,PCIE_HEADER_SIZE);

    int flag = 0 ;  // set flag if the IO Base/Limit or Mem Base/Limit or BARs are modified.
    if( offset < PCIE_HEADER_SIZE )
    {
        if(!isWritable(offset))  // if we can't write to this location
        {
            pkt->makeAtomicResponse() ;
            return configDelay ;
        }
        else
        {
            switch(pkt->getSize())
            {
                case sizeof(uint8_t):  DPRINTF(PciBridge2,"%s dev %#x reg %#x 1 bytes: data = %#x\n",__func__,storage1.DeviceId, offset,(uint8_t)pkt->get<uint8_t>(ByteOrder::little));
                    if(offset == PCI1_PRI_BUS_NUM){  // Must be writing to one of the 1 B RW registers : Primary Bus Num,Secondary Bus Num,Subordinate Bus Num, IO Limit, IO Base,Int line,BIST
                        storage1.PrimaryBusNumber = pkt->get<uint8_t>(ByteOrder::little) ;
                        }
                    else if (offset == PCI1_SEC_BUS_NUM){
                        storage1.SecondaryBusNumber = pkt->get<uint8_t>(ByteOrder::little) ; is_valid = 1 ;
                        }
                    else if (offset == PCI1_SUB_BUS_NUM){
                        storage1.SubordinateBusNumber = pkt->get<uint8_t>(ByteOrder::little) ;

                    }
                    else if (offset ==PCI1_IO_BASE)                                   // For the IO Base register, only the top 4 bits are writable by software. The hex value represents the most significant
                                                                                    // Hex Digit in 16 Bit IO address. Aligned in 4 KB granularity since bottom 12 address bits are always 0.
                    {
                        uint8_t temp = letoh(storage1.IOBase) ;
                        uint8_t val  = letoh(pkt->get<uint8_t>(ByteOrder::little)) ;
                        temp = (val & ~IO_BASE_MASK) | (temp & IO_BASE_MASK) ;
                    // printf("Val is %02x , Temp is %02x\n" , val , temp) ;
                        storage1.IOBase = htole(temp) ;
                        flag = 1 ; // Ranges change
                        valid_io_base = true ;
                    }

                    else if (offset == PCI1_IO_LIMIT)
                    {
                        uint8_t temp = letoh(storage1.IOLimit) ;                         // Only top 4 bits of IO Limit register are writable by S.W. This hex value represents the most significanr 4 bits
                                                                                        // of 16 bit IO address. Bottom 12 bits are all assumed to be 1 . E.G. IO_Limit register [ 7:4] = 0x4 indicates
                                                                                        // that the IO Limit is 0x4FFF . This makes sense since IO Bases are aligned on  4 KB boundaries.
                        uint8_t val  = letoh(pkt->get<uint8_t>(ByteOrder::little)) ;
                        temp = (val & ~IO_LIMIT_MASK) | (temp & IO_LIMIT_MASK) ;
                        storage1.IOLimit = htole(temp) ;
                        flag = 1 ;   // Ranges change
                        valid_io_limit = true ;
                    }
                    else if (offset == PCI1_INTR_LINE)
                        storage1.InterruptLine = pkt->get<uint8_t>(ByteOrder::little) ;
                    else if (offset == PCI_BIST)
                        storage1.BIST = pkt->get<uint8_t>(ByteOrder::little) ;
                    break ;
                case sizeof(uint16_t): DPRINTF(PciBridge2,"%s: dev %#x reg %#x 16bit: data = %#x\n",__func__,storage1.DeviceId, offset,(uint16_t)pkt->get<uint16_t>(ByteOrder::little));
                    // Writing to 2B RW config regs. Command , Status , Secondary Status, Mem base, Mem limit, Prefetch mem base, prefetch mem limit, IO limit
                                                    // upper , IO Base Upper , Bridge Control
                    if(offset == PCI_COMMAND)
                    storage1.Command = htole(letoh(pkt->get<uint8_t>(ByteOrder::little)) | 0x0400)  ;  // disable intx interrupts at all times
                    else if (offset == PCI_STATUS)
                    storage1.Status = pkt->get<uint8_t>(ByteOrder::little) ;
                    else if (offset == PCI1_SECONDARY_STATUS)
                    storage1.SecondaryStatus = pkt->get<uint8_t>(ByteOrder::little) ;
                    else if (offset == PCI1_MEM_BASE)
                    {
                        uint16_t temp = letoh(storage1.MemoryBase) ;                // Upper 12 bits of this register are the upper 12 bits of 32 bit memory base address. The lower 20 bits are assumed to be
                                                                                    //0 . Thus memory base addresses are aligned at 1 MB boundary.
                        uint16_t val = letoh(pkt->get<uint16_t>(ByteOrder::little)) ;
                        temp = (val & ~MMIO_BASE_MASK) | (temp & MMIO_BASE_MASK) ;
                        storage1.MemoryBase = htole(temp) ;
                        //DPRINTF(PciBridge2, "esj 16bit pcibridge memory base = %x\n",storage1.MemoryBase);
                        flag =1 ;
                        valid_memory_base = true ;
                    }
                    else if (offset == PCI1_MEM_LIMIT)
                    {
                        uint16_t temp = letoh ( storage1.MemoryLimit) ;
                        uint16_t val = letoh(pkt->get<uint16_t>(ByteOrder::little)) ;
                        temp = (val & ~MMIO_LIMIT_MASK) | (temp & MMIO_LIMIT_MASK) ;  //  Upper 12 bits of this register are writable and are upper 12 bits of 32 bit memory limit. The lower 20 bits are 1.
                        storage1.MemoryLimit = htole(temp) ;
                        // DPRINTF(PciBridge2, "esj 16bit pcibridge Memory Limit = %x\n",storage1.MemoryLimit);
                        flag = 1 ;
                        valid_memory_limit = true ;
                        }


                    else if (offset == PCI1_PRF_MEM_BASE)
                    {
                        uint16_t temp = letoh(storage1.PrefetchableMemoryBase) ;                   // Upper 12 bits of this register are writable and are the upper 12 bits of 32 bit Prefetch Mem Base address.
                                                                                                    // The lower 20 bits are assumed to be 0 , so the pretchable mem base is aligned at 1 MB.
                        uint16_t val = letoh(pkt->get<uint16_t>(ByteOrder::little)) ;
                        temp = (val & ~PREFETCH_MEM_BASE_MASK) | (temp & PREFETCH_MEM_BASE_MASK) ;
                        storage1.PrefetchableMemoryBase = htole(temp) ;
                        flag = 1 ;
                        valid_prefetchable_memory_base = true ;
                    }
                    else if (offset == PCI1_PRF_MEM_LIMIT)
                    {
                        uint16_t temp = letoh(storage1.PrefetchableMemoryLimit) ;                       // Upper 12 bits are writable and are the upper 12 bits of 32 bit Prefetch Mem Limit. the Lower 20 bits
                                                                                                        // are all 1 , so each limit ends with FFFFF.
                        uint16_t val = letoh( pkt->get<uint16_t>(ByteOrder::little)) ;
                        temp = (val & ~PREFETCH_MEM_LIMIT_MASK) | (temp & PREFETCH_MEM_BASE_MASK) ;
                        storage1.PrefetchableMemoryLimit = htole(temp) ;
                        flag = 1 ;
                        valid_prefetchable_memory_limit = true ;
                    }


                    else if (offset == PCI1_IO_BASE_UPPER)                 // Upper 16 bits of 32 bit IO Base. Valid if bit 0 of IO Base register is set.
                    {
                //         printf("trying to store %04x in IO Base upper\n" , pkt->get<uint16_t>()) ;
                    //storage1.IOBaseUpper = pkt->get<uint16_t>(ByteOrder::little) ; //comment this out //esj 2024
                    flag = 1 ;
                    valid_io_base = true ;
                    }
                    else if (offset == PCI1_IO_LIMIT_UPPER)                // Upper 16 bits of 32 bit IO Limit. Valid if bit 0 of IO Limit register is set.
                    {
                        //  storage1.IOLimitUpper = pkt->get<uint16_t>() ; //comment this out
                //      printf("trying to store %04x in IO limit upper\n" , pkt->get<uint16_t>()) ;
                        flag = 1 ;
                        valid_io_limit = true ;
                    }
                    else if (offset == PCI1_BRIDGE_CTRL)
                    storage1.BridgeControl = pkt->get<uint16_t>(ByteOrder::little) ;
                    else if (offset == PCI1_IO_BASE)
                    {
                    //   printf("Assigning io base and limit\n") ;
                        uint16_t temp = letoh(pkt->get<uint16_t>(ByteOrder::little)) ;

                        flag = 1 ;
                        valid_io_base = true ; valid_io_limit = true ;
                        uint8_t t = (uint8_t)(temp & 0x00FF);
                        uint8_t stored_val = letoh(storage1.IOBase) ;
                        stored_val = (t & ~IO_BASE_MASK) | (stored_val & IO_BASE_MASK) ;
                        storage1.IOBase = htole(stored_val) ;
                        temp = temp >> 8 ;
                        stored_val = letoh(storage1.IOLimit) ;
                        t = (uint8_t)temp ;
                        stored_val = (t & ~IO_LIMIT_MASK) | (stored_val & IO_LIMIT_MASK) ;
                        storage1.IOLimit = htole(stored_val) ;
                    }

                    break ;


                case sizeof(uint32_t): DPRINTF(PciBridge2,"%s: dev %#x reg %#x 32bit: data = %#x\n",__func__,storage1.DeviceId, offset,(uint32_t)pkt->get<uint32_t>(ByteOrder::little));
                // Can be expansion ROM Base Address , BAR0 , BAR1 or PrefetchableBase/Limit Upper

                    if(offset == PCI1_ROM_BASE_ADDR)
                            //storage1.ExpansionROMBaseAddress = pkt->get<uint32_t>() ;
                            storage1.ExpansionROMBaseAddress = htole((uint32_t)0) ;
                    else if (offset == PCI1_PRF_BASE_UPPER)                            // Upper 32 bits of 64 bit Prefetchable Memory base
                        {
                            valid_prefetchable_memory_base = true ;
                            storage1.PrefetchableMemoryBaseUpper = pkt->get<uint32_t>(ByteOrder::little) ;
                            flag = 1 ;
                        }
                    else if (offset == PCI1_PRF_LIMIT_UPPER)                             // Upper 32 bits of 64 bit Prefetchable Memory Limit
                        {
                            valid_prefetchable_memory_limit = true ;
                            storage1.PrefetchableMemoryLimitUpper = pkt->get<uint32_t>(ByteOrder::little) ;
                            flag = 1 ;
                        }
                    else if (offset == PCI1_BASE_ADDR0)
                    {
                        int val = letoh(pkt->get<uint32_t>(ByteOrder::little)) ;
                        //DPRINTF(PciBridge2, "esj PCI1_BASE_ADDR0 = %x\n",val);
                        uint32_t Bar0 = letoh(storage1.Bar0) ; // BAR's are stored in Little Endian order ( may be different from host order depending on ISA)
                        if(val == 0xFFFFFFFF) //writing into BAR initially to determine memory size requested.
                        {
                            if(Bar0 & 1) // LSB is set , indicating IO BAR
                            {
                                Bar0 = (~(barsize[0] ) - 1 )  | (Bar0 & IO_MASK)  ;  // LSB always has to remain set , otherwise it becomes a memory BAR. Minimum size is 4 B.
                            // printf("esj 0xFFFFFFFF IO_MASK Bar0 = %0x\n",Bar0);
                            }
                            else
                            {
                                Bar0 = ~( barsize[0] - 1 )  | (Bar0 & MEM_MASK); // minimum Bar Size is 128 B for a memory BAr , according to PCI Express spec. Can't set it lower than that. Want to preserve size
                            // printf("esj 0xFFFFFFFF MEM_MASK Bar0 = %0x\n",Bar0);                                                   //and prefetchable bit values in the memory BAR
                            }
                        }
                        else
                        {
                            barflags[0] = 1 ;
                            flag = 1 ;
                            // Base address field for MEM BAR is [31:7] .
                            if(Bar0 & 1) // IO BAr
                            {
                                Bar0 = (val & ~(barsize[0] - 1)) | (Bar0  &IO_MASK) ;
                                //printf("esj xx IO_MASK Bar0 = %0x\n",Bar0);
                            }
                            else
                            {
                                Bar0 = (val & ~(barsize[0] - 1)) | (Bar0 & MEM_MASK) ; // Again, the BAR Size requested has to be >= 128 B for a memory BAR , and the base address has to be aligned to the Bar Size.
                                //printf("esj xx MEM_MASK Bar0 = %0x\n",Bar0);
                            }
                        }
                        storage1.Bar0 = htole(Bar0) ; // convert from host to little endian
                    }
                    else if (offset == PCI1_BASE_ADDR1)
                    {

                        int val = letoh(pkt->get<uint32_t>(ByteOrder::little)) ;
                        //DPRINTF(PciBridge2, "esj PCI1_BASE_ADDR1 = %x\n",val);
                        uint32_t Bar1 = letoh(storage1.Bar1) ;

                        if(val == 0xFFFFFFFF) //writing into BAR initially to determine memory size requested.
                        {
                            if(Bar1 & 1) // LSB is set , indicating IO BAR
                            {
                                    Bar1 = (~(barsize[0] ) - 1 )  | (Bar1 & IO_MASK)  ; //esj fjkdlsa;jfkla;j;
                                //   Bar1 = ~(barsize[1] - 1 )  | (Bar1 & IO_MASK)  ;  // LSB always has to remain set , otherwise it becomes a memory BAR. Minimum size is 4 B.
                            }
                            else
                            {
                                Bar1 = ~( barsize[1] - 1 )   | (Bar1 & MEM_MASK) ; // minimum Bar Size is 128 B for a memory BAr , according to PCI Express spec. Can't set it lower than that.
                            }
                        }
                        else
                        {
                            flag = 1 ;
                            barflags[1] = 1 ;
                            // Base address field is [31:7] .
                            if(Bar1 & 1) // IO BAr
                            {
                                Bar1 = (val & ~(barsize[1] - 1)) | (Bar1 & IO_MASK) ;
                            }
                            else
                            {

                                Bar1 = (val & ~(barsize[1] - 1)) | (Bar1 & MEM_MASK) ; // Again, the BAR Size requested has to be >= 128 B for a memory BAR , and the base address has to be aligned to the Bar Size.
                            }
                        }
                        storage1.Bar1 = htole(Bar1) ;
                    }

                    else if (offset == PCI1_MEM_BASE)
                    {
                        valid_memory_base = true ; valid_memory_limit = true ;
                        uint32_t val = pkt->get<uint32_t>(ByteOrder::little) ;
                        uint16_t base_val = (uint16_t)(val & 0x0000FFFF) ;
                        uint16_t cur_base_val = letoh(storage1.MemoryBase) ;
                        uint16_t limit_val = (uint16_t)(val >>16) ;
                        uint16_t cur_limit_val = letoh(storage1.MemoryLimit) ;
                        cur_base_val = (base_val & ~MMIO_BASE_MASK) | (cur_base_val & MMIO_LIMIT_MASK) ;
                        cur_limit_val = (limit_val & ~MMIO_LIMIT_MASK) | (cur_limit_val & MMIO_LIMIT_MASK) ;
                        storage1.MemoryBase = htole(cur_base_val) ;
                        storage1.MemoryLimit = htole(cur_limit_val) ;
                        flag = 1 ;
                    }
                    else if (offset == PCI1_IO_BASE_UPPER)
                    {
                        valid_io_base = true ; valid_io_limit = true ;
                        printf("trying to store %08x in IO Base upper, IO Limit upper \n" , pkt->get<uint32_t>(ByteOrder::little)) ;
                        /*uint32_t val = pkt->get<uint32_t>(ByteOrder::little) ;               // Comment out from here //esj 2024
                        printf("Val for IOBase upper is %08x\n" , val) ;
                        storage1.IOBaseUpper = (uint16_t)(val & 0x0000FFFF) ;
                        storage1.IOLimitUpper = (uint16_t)(val >> 16) ;*/     // To here
                        flag = 1 ;
                    }
                    else if (offset == PCI1_PRF_MEM_BASE)
                    {
                        valid_prefetchable_memory_base = true ; valid_prefetchable_memory_limit = true ;
                        //  uint32_t val = pkt->get<uint32_t>(ByteOrder::little) ;
                        //  uint16_t base_val = (uint16_t)(val & 0x0000FFFF) ;
                        //  uint16_t cur_base_val = letoh(storage1.PrefetchableMemoryBase) ;
                        //  uint16_t limit_val = (uint16_t)(val >> 16) ;
                        //  uint16_t cur_limit_val = letoh(storage1.PrefetchableMemoryLimit) ;
                        //  cur_base_val = (base_val & ~PREFETCH_MEM_BASE_MASK) | (cur_base_val & PREFETCH_MEM_BASE_MASK) ;
                        //  cur_limit_val = (limit_val & ~PREFETCH_MEM_LIMIT_MASK) | (cur_limit_val & PREFETCH_MEM_BASE_MASK)  ;
                        //  storage1.PrefetchableMemoryBase = htole(cur_base_val) ;
                        //  storage1.PrefetchableMemoryLimit = htole(cur_limit_val) ;
                        flag = 1 ;
                    }
                    else if (offset == PCI_COMMAND)
                    {
                        uint32_t val = pkt->get<uint32_t>(ByteOrder::little) ;
                        storage1.Command = (uint16_t)(val & 0x0000FFFF) ;
                        storage1.Status = (uint16_t)(val >>16) ;
                    }
                    else if (offset == PCI1_PRI_BUS_NUM)
                    {
                        *(uint32_t*)&storage1.data[PCI1_PRI_BUS_NUM] = pkt->get<uint32_t>(ByteOrder::little) ;
                        is_valid = 1 ;
                    //  if(id == 3)printf ("Pri bus num , Sec bus num , sub bus num = %d,%d,%d\n" , (int)storage1.PrimaryBusNumber , (int)storage1.SecondaryBusNumber, (int)storage1.SubordinateBusNumber) ;
                    }

                    break ;
                default: panic("invalid access size\n") ;
            }  // end of switch


        } // end of else

    }  // end of if offset < PCIE_HEADER_SIZE

    else if (offset >= PXCAPBaseOffset && offset < PXCAPBaseOffset + PXCAPSIZE) // writing to PCIe capability structure. Ignore writes to slot registers, not implemented.
    {
        offset -= PXCAPBaseOffset ; // get the offset from the base of the PCIe cap register set
        if ((offset < SLOT_REG_BASE) && (offset > SLOT_REG_LIMIT)) // ignoring writes to slot registers
        {
            switch(pkt->getSize())
            {
                case sizeof(uint8_t): *(uint8_t*)&storage2.data[offset] = pkt->get<uint8_t>(ByteOrder::little) ; break ;
                case sizeof(uint16_t): *(uint16_t*)&storage2.data[offset] = pkt->get<uint16_t>(ByteOrder::little) ; break ;
                case sizeof(uint32_t): *(uint32_t*)&storage2.data[offset] = pkt->get<uint32_t>(ByteOrder::little) ; break ;
                default: panic("invalid access size\n") ;
            }
        }
    }
    // else if(offset == MSICAP_BASE ){
    //     msicap.mid = pkt->get<uint16_t>(ByteOrder::little);
    // }
    else if (offset == MSICAP_BASE + 2 ) {  // MSICAP Message Control
        val = pkt->get<uint16_t>(ByteOrder::little);
        msicap.mc &= ~0x0071;
        msicap.mc |= (val & 0x0071);
    }
    else if (offset == MSICAP_BASE + 4 ) {  // MSICAP Message Address
        msicap.ma = pkt->get<uint32_t>(ByteOrder::little) & 0xFFFFFFFC;
    }
    else if (offset == MSICAP_BASE + 8 ) {  // MSICAP Message Upper Address
        msicap.mua = pkt->get<uint32_t>(ByteOrder::little);
    }
    else if (offset == MSICAP_BASE + 12 ) {  // MSICAP Message Data
        msicap.md = pkt->get<uint16_t>(ByteOrder::little);
    }
    else if (offset == MSICAP_BASE + 16 ) {  // MSICAP Interrupt Mask Bits
        msicap.mmask = pkt->get<uint32_t>(ByteOrder::little);
    }
    else if (offset == MSICAP_BASE + 20 ) {  // MSICAP Interrupt Pending Bits
        msicap.mpend = pkt->get<uint32_t>(ByteOrder::little);
    }
    // else if (offset == MSIXCAP_BASE ) {  // MSIXCAP cap //96
    //     msixcap.mxid = pkt->get<uint16_t>(ByteOrder::little);
    // }
    else if (offset == MSIXCAP_BASE + 2 ) {  // MSIXCAP Message Control
        val = pkt->get<uint16_t>(ByteOrder::little);

        msixcap.mxc &= ~0xC000;
        msixcap.mxc |= (val & 0xC000);
    }
    else panic("out of range access to pcie config space\n") ;
    pkt->makeAtomicResponse() ;
    if(flag) {
        if(id == 0 || is_switch == 0 )
        bridge->responsePort1.public_sendRangeChange() ; // send a range change when any of the bridge limit/BAR registers are changed
        if(id == 1)
        bridge->responsePort_DMA1.public_sendRangeChange() ;
        if(id == 2)
        bridge->responsePort_DMA2.public_sendRangeChange() ;
        if(id ==3 )
        bridge->responsePort_DMA3.public_sendRangeChange() ;

        //esj 2025-04-21
        //esj 2025-04-22
        // if(id ==4 )
        if(id ==4 || is_switch == 0)
        bridge->responsePort2.public_sendRangeChange() ;

        //esj 2025-06-22
        if(id == 5)
        bridge->responsePort1_1.public_sendRangeChange() ;
        if(id == 6)
        bridge->responsePort2_1.public_sendRangeChange() ;
        if(id == 7)
        bridge->responsePort3.public_sendRangeChange() ;
        if(id == 8)
        bridge->responsePort4.public_sendRangeChange() ;
        if(id == 9)
        bridge->responsePort3_1.public_sendRangeChange() ;
        if(id == 10)
        bridge->responsePort4_1.public_sendRangeChange() ;


  }
  return configDelay ;
}
void
PciBridge2::init()
{
    // make sure both sides are connected and have the same block size
    //esj 2025-05-07
    //  if (!responsePort1.isConnected() || !responsePort_DMA1.isConnected() || !responsePort_DMA2.isConnected() || !responsePort_DMA3.isConnected() || !requestPort_DMA1.isConnected() || !requestPort1.isConnected() || !requestPort2.isConnected() || !requestPort3.isConnected())
    //     fatal("Both ports of a bridge must be connected.\n");

    // notify the request side  of our address ranges
    responsePort1.sendRangeChange();

    //esj 2025-04-21
    responsePort2.sendRangeChange();

    //esj 2025-06-22
    responsePort1_1.sendRangeChange();
    responsePort2_1.sendRangeChange();
    responsePort3.sendRangeChange();
    responsePort4.sendRangeChange();
    responsePort3_1.sendRangeChange();
    responsePort4_1.sendRangeChange();
}

bool
PciBridge2::PciBridgeResponsePort::respQueueFull() const
{
    return outstandingResponses == respQueueLimit;
}

bool
PciBridge2::PciBridgeRequestPort::hasPendingHostReq() const
{
    return !transmitList_1st.empty() || !transmitList_2nd.empty() ||
           !transmitList_3rd.empty() || !transmitList_4th.empty();
}

size_t
PciBridge2::PciBridgeRequestPort::hostQueueSizeTotal() const
{
    return transmitList_1st.size() + transmitList_2nd.size() +
           transmitList_3rd.size() + transmitList_4th.size();
}

std::deque<PciBridge2::DeferredPacket>&
PciBridge2::PciBridgeRequestPort::hostQueue(uint8_t host_idx)
{
    switch (host_idx) {
      case 0: return transmitList_1st;
      case 1: return transmitList_2nd;
      case 2: return transmitList_3rd;
      case 3: return transmitList_4th;
      default:
        return transmitList_4th;
    }
}

const std::deque<PciBridge2::DeferredPacket>&
PciBridge2::PciBridgeRequestPort::hostQueue(uint8_t host_idx) const
{
    switch (host_idx) {
      case 0: return transmitList_1st;
      case 1: return transmitList_2nd;
      case 2: return transmitList_3rd;
      case 3: return transmitList_4th;
      default:
        return transmitList_4th;
    }
}

uint8_t
PciBridge2::PciBridgeRequestPort::packetHostIdx(const PacketPtr pkt) const
{
    return packetStatsHostIdx(pkt);
}

void
PciBridge2::PciBridgeRequestPort::ensurePriorityCycleInitialized()
{
    if (priority_cycle_initialized) {
        return;
    }

    std::vector<std::pair<uint8_t, uint8_t>> weighted_hosts;
    for (uint8_t host = 0; host < bridge.priority_host_ratios.size(); ++host) {
        const uint8_t weight = bridge.priority_host_ratios[host];
        if (weight > 0) {
            weighted_hosts.emplace_back(host, weight);
        }
    }

    if (weighted_hosts.empty()) {
        weighted_hosts.emplace_back(0, 1);
    }

    std::sort(
        weighted_hosts.begin(),
        weighted_hosts.end(),
        [](const std::pair<uint8_t, uint8_t> &a,
           const std::pair<uint8_t, uint8_t> &b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });

    priority_cycle.clear();
    for (const auto &entry : weighted_hosts) {
        for (uint8_t i = 0; i < entry.second; ++i) {
            priority_cycle.push_back(entry.first);
        }
    }

    if (priority_cycle.empty()) {
        priority_cycle.push_back(0);
    }
    priority_cycle_index = 0;
    priority_cycle_initialized = true;
}

PciBridge2::DeferredPacket
PciBridge2::PciBridgeRequestPort::pickRoundRobinReq()
{
    assert(hasPendingHostReq());

    for (uint8_t offset = 0; offset < Packet::MaxPciRequesterIds; ++offset) {
        const uint8_t idx = (next_transaction_list_index + offset) %
                            Packet::MaxPciRequesterIds;
        auto &q = hostQueue(idx);
        if (!q.empty()) {
            next_transaction_list_index = (idx + 1) %
                                          Packet::MaxPciRequesterIds;
            return q.front();
        }
    }

    panic("ROUND_ROBIN: all host queues empty");
}

PciBridge2::DeferredPacket
PciBridge2::PciBridgeRequestPort::pickPriorityReq()
{
    ensurePriorityCycleInitialized();
    assert(hasPendingHostReq());

    for (size_t step = 0; step < priority_cycle.size(); ++step) {
        const size_t pos = (priority_cycle_index + step) % priority_cycle.size();
        const uint8_t host_idx = priority_cycle[pos];
        auto &q = hostQueue(host_idx);
        if (!q.empty()) {
            priority_cycle_index = (pos + 1) % priority_cycle.size();
            return q.front();
        }
    }

    // fallback: if every weighted slot missed, pick first non-empty host queue.
    for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds; ++host_idx) {
        auto &q = hostQueue(host_idx);
        if (!q.empty()) {
            return q.front();
        }
    }

    panic("PRIORITY: all host queues empty");
}

Tick
PciBridge2::PciBridgeRequestPort::peekNextReadyTick() const
{
    Tick next_tick = 0;
    for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds; ++host_idx) {
        const auto &q = hostQueue(host_idx);
        if (q.empty()) {
            continue;
        }
        if (next_tick == 0 || q.front().tick < next_tick) {
            next_tick = q.front().tick;
        }
    }
    return next_tick;
}

void
PciBridge2::PciBridgeRequestPort::popSentPacketFromHostQueue(PacketPtr pkt)
{
    for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds; ++host_idx) {
        auto &q = hostQueue(host_idx);
        if (!q.empty() && q.front().pkt == pkt) {
            q.pop_front();
            return;
        }
    }
    panic("Failed to find sent packet in host routing queues");
}

bool
PciBridge2::PciBridgeRequestPort::reqQueueFull() const
{
    if (bridge.routing_mode != DEFAULT) {
        return hostQueueSizeTotal() >= reqQueueLimit;
    }
    return transmitList.size() == reqQueueLimit;
}

bool
PciBridge2::PciBridgeRequestPort::reqQueueFull(uint8_t host_idx) const
{
    if (bridge.routing_mode == DEFAULT) {
        return reqQueueFull();
    }

    if (host_idx >= Packet::MaxPciRequesterIds) {
        host_idx = Packet::MaxPciRequesterIds - 1;
    }
    const auto &q = hostQueue(host_idx);
    return q.size() >= reqQueueLimit;
}

bool
PciBridge2::PciBridgeRequestPort::recvTimingResp(PacketPtr pkt)
{
    // all checks are done when the request is accepted on the response
    // side, so we are guaranteed to have space for the response
    DPRINTF(PciBridge2, "%s: %s addr 0x%x\n",__func__,pkt->cmdString(), pkt->getAddr());
    DPRINTF(PciBridge2, "%s: pkt addr = %x\n", __func__,pkt->getAddr());
    DPRINTF(PciBridge2, "%s,Request queue size: %d\n", __func__,transmitList.size());

    // if((pkt->req_bus[rcid] == 0) && (bridge.is_switch == 1)) printf("Request Port received response to switch port\n");

    //esj 2025-04-23
    // PciBridgeResponsePort * responsePort1 = bridge.getResponsePort(pkt->req_bus[rcid]) ;
    PciBridgeResponsePort * responsePort = nullptr;
    uint8_t source_host_idx = packetStatsHostIdx(pkt);
    pkt->is_from_second_response_port = (source_host_idx != 0);

    if (source_host_idx == 0) {
        DPRINTF(PciBridge2, "source_host_idx=0, send to response1_1\n");
        responsePort = &bridge.responsePort1_1;
    } else if (source_host_idx == 1) {
        DPRINTF(PciBridge2, "source_host_idx=1, send to response2_1\n");
        responsePort = &bridge.responsePort2_1;
    } else if (source_host_idx == 2) {
        DPRINTF(PciBridge2, "source_host_idx=2, send to response3_1\n");
        responsePort = &bridge.responsePort3_1;
    } else if (source_host_idx == 3) {
        DPRINTF(PciBridge2, "source_host_idx=3, send to response4_1\n");
        responsePort = &bridge.responsePort4_1;
    } else {
        DPRINTF(PciBridge2,
                "source_host_idx=%d out of range, fallback to response4_1\n",
                source_host_idx);
        responsePort = &bridge.responsePort4_1;
    }


    DPRINTF(PciBridge2, "%s,responseport name = %s\n", __func__,responsePort->name());
    DPRINTF(PciBridge2, "%s, responsePort = %d\n", __func__,responsePort->respID);
    // if(bridge.is_switch == 1) printf("Found response %d\n" , responsePort1->respID) ;
    if((responsePort->respID == 1) && (bridge.is_switch==0) && (pkt->isResponse()) && (pkt->hasData()) && (bridge.is_transmit == 1))totalCount += pkt->getSize() ;



    // technically the packet only reaches us after the header delay,
    // and typically we also need to deserialise any payload (unless
    // the two sides of the bridge are synchronous)
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    //if(bridge.is_switch==0)printf("sched resp : cur:%lu, when :%lu, r_d %lu\n",curTick(), bridge.clockEdge(delay) + receive_delay, receive_delay) ;

    responsePort->schedTimingResp(pkt, bridge.clockEdge(delay) +receive_delay); //esj 2024-04-16
    //esj 2024-12-27
    DPRINTF(PciBridge2,"%s, recv response packet addr = %0x, when = %d\n",__func__,pkt->getAddr(),bridge.clockEdge(delay) +receive_delay);
    // responsePort1->schedTimingResp(pkt, delay +receive_delay);
   // if((responsePort1->respID == 0) && (bridge.is_switch ==1)) printf("Scheduling response to upstream response port\n") ;
    return true;
}

bool
PciBridge2::PciBridgeResponsePort::recvTimingReq(PacketPtr pkt)
{
    Addr pkt_Addr = pkt->getAddr() ;
    //if((respeID==0) &&(bridge.is_switch==0))printf("Response received timing req to Addr: %08x\n" , (unsigned int)pkt_Addr) ;
    // DPRINTF(PciBridge2, "%s: pkt addr = %x\n",__func__, pkt->getAddr());
    //esj 2025-05-08
    // PciBridgeRequestPort * requestPort  = bridge.getRequestPort(pkt_Addr) ;
    uint8_t source_host_idx = clampStatsHostIdx(rcid);
    const uint8_t rc_index = source_host_idx;
    pkt->source_host_idx = source_host_idx;
    pkt->is_from_second_response_port = (source_host_idx != 0);
    const bool chbs_needs_response = pkt->needsResponse();
    if (bridge.accessCxlHostBridgeMmio(pkt, source_host_idx)) {
        if (chbs_needs_response)
            schedTimingResp(pkt, bridge.clockEdge(delay));
        else
            pendingDelete.reset(pkt);
        return true;
    }
    PciBridgeRequestPort *requestPort = nullptr;
    if (pkt->cxl_flag &&
        (pkt->is_cxl_mem || pkt->cxl_pkt.is_controlflit)) {
        requestPort = &bridge.requestPort2_1;
    } else if (bridge.is_switch && pkt->cxl_flag && pkt->is_cxl_io) {
        requestPort = &bridge.requestPort3;
    } else {
        requestPort = bridge.getRequestPort(pkt_Addr, source_host_idx);
    }
    DPRINTF(PciBridge2,
            "%s: pkt addr = %x, source_host_idx = %u, request_port = %s\n",
            __func__, pkt->getAddr(), source_host_idx,
            requestPort->name());




    if(pkt->req_bus[rc_index] == -1) // response port assigns the requester id
    {
        if(respID ==0)
        {

            pkt->req_bus[rc_index] =
                bridge.upstreamStorage(source_host_idx)->pci_bus ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;

        }
        else if (respID == 1)
        {
        // printf("Root Port 1 received DMA access\n") ;
            pkt->req_bus[rc_index] =
                bridge.downstreamStorage(0, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;
        //  printf("Root Port 1 received DMA access , appending %d to address %16lx \n" , (int)pkt->req_bus[rc_index] , pkt_Addr) ;
        }
        else if (respID == 2)
        {

            pkt->req_bus[rc_index] =
                bridge.downstreamStorage(1, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;
        //   printf("Root Port 2 received DMA access , appending %d\n" , (int)pkt->req_bus[rc_index]) ;
        }

        else if (respID == 3)
        {
        // printf("Root port 3 received DMA\n") ;

            pkt->req_bus[rc_index] =
                bridge.downstreamStorage(2, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;
        //  printf("Root Port 3 received DMA access , appending %d\n" , (int)pkt->req_bus[rc_index]) ;
        }

        //esj 2025-04-21
        else if(respID ==4)
        {

            pkt->req_bus[rc_index] =
                bridge.upstreamStorage(source_host_idx)->pci_bus ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;

        }
        else if(respID ==7)
        {
            pkt->req_bus[rc_index] =
                bridge.upstreamStorage(source_host_idx)->pci_bus ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;
        }
        else if(respID ==9)
        {
            pkt->req_bus[rc_index] =
                bridge.upstreamStorage(source_host_idx)->pci_bus ;
            pkt->req_dev[rc_index] = 0 ;
            pkt->req_func[rc_index] = 0 ;
        }
    }

    DPRINTF(PciBridge2, "%s: %s addr 0x%x\n",
        __func__,pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    DPRINTF(PciBridge2, "%s, Response queue size: %d pkt->resp: %d\n",
        __func__,transmitList.size(), outstandingResponses);

    // if the request queue is full then there is no hope
    if (requestPort->reqQueueFull(source_host_idx)) {
        DPRINTF(PciBridge2,
                "%s Request queue full (host=%u)\n",
                __func__,
                source_host_idx + 1);
        bridge.stats.request_queue_full_count[source_host_idx]++;
        retryReq = true;
    } else {
        // Only a new data request consumes a response reservation.  Retry
        // data already owns the reservation taken by its original request,
        // while RRSM/LRSM control flits never produce an architectural
        // response.  Applying respQueueFull() to either class creates a
        // circular wait when all ordinary credits are occupied: the packet
        // needed to release a credit is itself rejected for lack of credit.
        const bool reserves_response =
            pkt->needsResponse() &&
            !pkt->cxl_pkt.is_controlflit &&
            !pkt->cxl_pkt.retry_req;
        if (reserves_response) {
            if (respQueueFull()) {
                DPRINTF(PciBridge2, "%s Response queue full\n",__func__);
                bridge.stats.response_queue_full_count[source_host_idx]++;
                retryReq = true;
            } else {
                // ok to send the request with space for the response
                DPRINTF(PciBridge2, "Reserving space for response\n");
                assert(outstandingResponses != respQueueLimit);

                //esj 2025-05-08
                ++outstandingResponses;
                DPRINTF(
                    PciBridge2,
                    "PciBridgeResponsePort::recvTimingReq "
                    "outstandingResponses = %d\n",
                    outstandingResponses);
                // DPRINTF(PciBridge2, "%s: outstandingResponses = %d\n",
                //         __func__, outstandingResponses);
               //if((respID==2) && bridge.is_switch==0) printf("Outstanding responses inc : %u\n" , outstandingResponses) ;

                // no need to set retryReq to false as this is already the
                // case
            }
        }

        if (!retryReq) {
            // technically the packet only reaches us after the header
            // delay, and typically we also need to deserialise any
            // payload (unless the two sides of the bridge are
            // synchronous)

            //esj 2024-05-08
            //add Flex bus delay
            //esj 2024-11-05
            // Tick Flexbus_delay = Flex_Bus(pkt);
            //esj 2024-11-05
            // Tick receive_delay = pkt->headerDelay + pkt->payloadDelay + Flexbus_delay;
            Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
            // DPRINTF(PciBridge2, "%s:pkt addr : %0x, pkt->headerDelay = %d,  pkt->payloadDelay = %d, Flexbus_delay = %d, receive_delay = %d\n",__func__,pkt->headerDelay, pkt->payloadDelay,Flexbus_delay,receive_delay);
            // Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;



            //if(bridge.is_switch==0)printf("sched req : cur:%lu, when :%lu, r_d %lu\n",curTick(), bridge.clockEdge(delay) + receive_delay, receive_delay) ;

            requestPort->schedTimingReq(pkt, bridge.clockEdge(delay) +receive_delay);
            DPRINTF(PciBridge2, "%s:pkt addr : %0x, bridge.clockEdge(delay) = %d,  receive_delay = %d, when = %d\n",__func__,pkt->getAddr(),bridge.clockEdge(delay),receive_delay,bridge.clockEdge(delay) +receive_delay);
            // requestPort->schedTimingReq(pkt, delay +receive_delay);

        }
    }

    // remember that we are now stalling a packet and that we have to
    // tell the sending request to retry once space becomes available,
    // we make no distinction whether the stalling is due to the
    // request queue or response queue being full
    return !retryReq;
}
void
PciBridge2::PciBridgeResponsePort::retryStalledReq()
{
    if (retryReq) {
        DPRINTF(PciBridge2, "%s,Request waiting for retry, now retrying\n",__func__);
        retryReq = false;
        sendRetryReq();
    }
}

//esj 2024-05-08
Tick
PciBridge2::PciBridgeResponsePort::Flex_Bus(PacketPtr pkt){
    if(pkt->cxl_flag && !bridge.is_switch){
        // 여기에 cxl protocl id 추가 하는 방식으로..?

        return 2 * sim_clock::as_int::ns;
    }
    else
        return 0;
}

void
PciBridge2::PciBridgeRequestPort::schedTimingReq(PacketPtr pkt, Tick when)
{
    // If we're about to put this packet at the head of the queue, we
    // need to schedule an event to do the transmit.  Otherwise there
    // should already be an event scheduled for sending the head
    // packet.
    // if (transmitList.empty()) {
    //     DPRINTF(PciBridge2, "esj PciBridge2::PciBridgeRequestPort::schedTimingReq when = %d\n",when);
    //     bridge.schedule(sendEvent, when);
    // }

    const uint8_t host_idx = packetHostIdx(pkt);
    bridge.stats.request_enqueue_count[host_idx]++;

    switch(bridge.routing_mode){
        case DEFAULT:
            assert(transmitList.size() != reqQueueLimit);

            if (transmitList.empty()) {
                DPRINTF(PciBridge2, "%s when = %d\n",__func__,when);
                bridge.schedule(sendEvent, when);
            }

            DPRINTF(PciBridge2, "%s: pkt addr = %x, when = %d\n",__func__, pkt->getAddr(),when);
            transmitList.emplace_back(pkt, when);
            bridge.stats.request_queue_occupancy_total[host_idx] +=
                transmitList.size();
            bridge.stats.request_queue_occupancy_samples[host_idx]++;
            bridge.stats.request_queue_occupancy_hist.sample(
                transmitList.size());
            if (bridge.stats.request_queue_occupancy_max[host_idx].value() <
                transmitList.size()) {
                bridge.stats.request_queue_occupancy_max[host_idx] =
                    transmitList.size();
            }
            DPRINTF(PciBridge2,
                    "%s: transmitList[0].pkt->getAddr() = %x, cmd = %s\n",
                    __func__, pkt->getAddr(), pkt->cmd.toString());
            break;

        case ROUND_ROBIN:
        case PRIORITY: {
            auto &queue = hostQueue(host_idx);
            assert(queue.size() != reqQueueLimit);
            if (!hasPendingHostReq()) {
                DPRINTF(PciBridge2, "%s when = %d\n",__func__,when);
                bridge.schedule(sendEvent, when);
            }

            queue.emplace_back(pkt, when);
            bridge.stats.request_queue_occupancy_total[host_idx] +=
                queue.size();
            bridge.stats.request_queue_occupancy_samples[host_idx]++;
            bridge.stats.request_queue_occupancy_hist.sample(queue.size());
            if (bridge.stats.request_queue_occupancy_max[host_idx].value() <
                queue.size()) {
                bridge.stats.request_queue_occupancy_max[host_idx] =
                    queue.size();
            }
            DPRINTF(PciBridge2, "%s: mode=%d enqueue addr=%x when=%d\n",
                    __func__, bridge.routing_mode, pkt->getAddr(), when);
            DPRINTF(PciBridge2,
                    "%s: enqueue host=%u sizes=[%lu,%lu,%lu,%lu]\n",
                    __func__,
                    host_idx + 1,
                    (unsigned long)transmitList_1st.size(),
                    (unsigned long)transmitList_2nd.size(),
                    (unsigned long)transmitList_3rd.size(),
                    (unsigned long)transmitList_4th.size());
            break;
        }

    }


}


void
PciBridge2::PciBridgeResponsePort::schedTimingResp(PacketPtr pkt, Tick when)
{
    // If we're about to put this packet at the head of the queue, we
    // need to schedule an event to do the transmit.  Otherwise there
    // should already be an event scheduled for sending the head
    // packet.
    // if (transmitList.empty()) {
    //     DPRINTF(PciBridge2, "esj PciBridge2::PciBridgeResponsePort::schedTimingResp when = %d, pkt->addr: %x, pkt->isresponse = %d\n",when, pkt->getAddr(),pkt->isResponse());
    //     bridge.schedule(sendEvent, when);
    // }

    const uint8_t host_idx = packetStatsHostIdx(pkt);
    bridge.stats.response_enqueue_count[host_idx]++;

    if (transmitList.empty()) {
        DPRINTF(PciBridge2, "%s, when = %d, pkt->addr: %x, pkt->isresponse = %d\n",__func__,when, pkt->getAddr(),pkt->isResponse());
        bridge.schedule(sendEvent, when);
    }

    DPRINTF(PciBridge2, "%s: pkt addr = %x\n",__func__, pkt->getAddr());
    transmitList.emplace_back(pkt, when);
    bridge.stats.response_queue_occupancy_total[host_idx] +=
        transmitList.size();
    bridge.stats.response_queue_occupancy_samples[host_idx]++;
    bridge.stats.response_queue_occupancy_hist.sample(transmitList.size());
    if (bridge.stats.response_queue_occupancy_max[host_idx].value() <
        transmitList.size()) {
        bridge.stats.response_queue_occupancy_max[host_idx] =
            transmitList.size();
    }
    DPRINTF(PciBridge2, "%s: addr = %x, cmd = %s, when = %d\n",
            __func__, pkt->getAddr(), pkt->cmd.toString(), when);

}

void
PciBridge2::PciBridgeRequestPort::trySendTiming()
{
    DeferredPacket req = [&]() -> DeferredPacket {
        switch (bridge.routing_mode) {
            case DEFAULT:
                assert(!transmitList.empty());
                DPRINTF(PciBridge2, "%s [DEFAULT]: pick addr=%x tick=%ld size=%lu\n",
                        __func__,
                        transmitList.front().pkt->getAddr(),
                        (long)transmitList.front().tick,
                        (unsigned long)transmitList.size());
                return transmitList.front();

            case ROUND_ROBIN: {
                DeferredPacket picked = pickRoundRobinReq();
                DPRINTF(PciBridge2,
                        "%s [RR]: pick host=%u addr=%x tick=%ld sizes=[%lu,%lu,%lu,%lu]\n",
                        __func__,
                        packetHostIdx(picked.pkt) + 1,
                        picked.pkt->getAddr(),
                        (long)picked.tick,
                        (unsigned long)transmitList_1st.size(),
                        (unsigned long)transmitList_2nd.size(),
                        (unsigned long)transmitList_3rd.size(),
                        (unsigned long)transmitList_4th.size());
                return picked;
            }

            case PRIORITY: {
                DeferredPacket picked = pickPriorityReq();
                DPRINTF(PciBridge2,
                        "%s [PRIO]: pick host=%u addr=%x tick=%ld sizes=[%lu,%lu,%lu,%lu]\n",
                        __func__,
                        packetHostIdx(picked.pkt) + 1,
                        picked.pkt->getAddr(),
                        (long)picked.tick,
                        (unsigned long)transmitList_1st.size(),
                        (unsigned long)transmitList_2nd.size(),
                        (unsigned long)transmitList_3rd.size(),
                        (unsigned long)transmitList_4th.size());
                return picked;
            }

            default:
                panic("Unknown routing mode");
        }
    }();

    // assert(req.tick <= curTick());

    PacketPtr pkt = req.pkt;
    const uint8_t host_idx = packetHostIdx(pkt);
    const Tick wait_ticks = curTick() > req.tick ? curTick() - req.tick : 0;
    const uint64_t wait_ns = ticksToNs(wait_ticks);
    const uint64_t wait_cycles =
        static_cast<uint64_t>(bridge.ticksToCycles(wait_ticks));

    // DPRINTF(PciBridge2, "%s, trySend request addr 0x%x, queue size %d\n",__func__,
    //         pkt->getAddr(), transmitList.size());

    if (sendTimingReq(pkt)) {
        bridge.stats.request_grant_count[host_idx]++;
        bridge.stats.request_wait_ns[host_idx] += wait_ns;
        bridge.stats.request_wait_cycles[host_idx] += wait_cycles;
        bridge.stats.request_wait_ns_hist.sample(wait_ns);

        // send successful
        // transmitList.pop_front();
        // DPRINTF(PciBridge2, "trySend request successful\n");

        // // If there are more packets to send, schedule event to try again.
        // if (!transmitList.empty()) {
        //     DeferredPacket next_req = transmitList.front();
        //     DPRINTF(PciBridge2, "Scheduling next send\n");

        //     //esj 2024-10-03
        //     bridge.schedule(sendEvent, std::max(next_req.tick,bridge.clockEdge()));
        //     // bridge.schedule(sendEvent, next_req.tick);
        // }

        switch (bridge.routing_mode) {
            case DEFAULT: {
                transmitList.pop_front();
                DPRINTF(PciBridge2, "trySend request successful\n");
                if (!transmitList.empty()) {
                    DeferredPacket next_req = transmitList.front();
                    DPRINTF(PciBridge2, "Scheduling next send\n");
                    bridge.schedule(
                        sendEvent,
                        std::max(next_req.tick, bridge.clockEdge()));
                }
                break;
            }
            case ROUND_ROBIN:
            case PRIORITY: {
                popSentPacketFromHostQueue(pkt);
                if (hasPendingHostReq()) {
                    Tick next_tick = peekNextReadyTick();
                    if (next_tick == 0) {
                        next_tick = bridge.clockEdge();
                    }
                    DPRINTF(PciBridge2,
                            "%s [mode=%d]: schedule next_tick=%ld sizes=[%lu,%lu,%lu,%lu]\n",
                            __func__,
                            bridge.routing_mode,
                            (long)next_tick,
                            (unsigned long)transmitList_1st.size(),
                            (unsigned long)transmitList_2nd.size(),
                            (unsigned long)transmitList_3rd.size(),
                            (unsigned long)transmitList_4th.size());
                    bridge.schedule(sendEvent, std::max(next_tick, bridge.clockEdge()));
                }
                break;
            }

            default:
                panic("Unknown routing mode");
        }

        bridge.stats.request_dequeue_count[host_idx]++;

        // if we have stalled a request due to a full request queue,
        // then send a retry at this point, also note that if the
        // request we stalled was waiting for the response queue
        // rather than the request queue we might stall it again

        //esj 2025-04-21
        // bridge.responsePort.retryStalledReq();
        bridge.responsePort1.retryStalledReq();
        bridge.responsePort2.retryStalledReq();
        bridge.responsePort1_1.retryStalledReq();
        bridge.responsePort2_1.retryStalledReq();
        bridge.responsePort3.retryStalledReq();
        bridge.responsePort4.retryStalledReq();
        bridge.responsePort3_1.retryStalledReq();
        bridge.responsePort4_1.retryStalledReq();
        bridge.responsePort_DMA1.retryStalledReq() ;
        bridge.responsePort_DMA2.retryStalledReq() ;
        bridge.responsePort_DMA3.retryStalledReq() ;  // call all response ports retry stalled request just to be safe and avoid a request being infinitely stalled .

    }
    else {
        bridge.stats.request_send_blocked_count[host_idx]++;
    }

    // if the send failed, then we try again once we receive a retry,
    // and therefore there is no need to take any action
}

void PciBridge2::PciBridgeRequestPort:: incCount()
{

   if(totalCount !=0) { printf("R.P Count : %lu\n" , totalCount) ; }
   totalCount = 0 ;
   bridge.schedule(countEvent , curTick() + 1000000000000) ;
}
void
PciBridge2::PciBridgeResponsePort::trySendTiming()
{

    assert(!transmitList.empty());

    DeferredPacket resp = transmitList.front();
    DPRINTF(PciBridge2, "%s: transmitList[0].pkt->getAddr() = %x, size = %d, cmd = %s\n",__func__, transmitList[0].pkt->getAddr(),transmitList.size(),transmitList[0].pkt->cmd.toString());
    assert(resp.tick <= curTick());

    PacketPtr pkt = resp.pkt;
    const uint8_t host_idx = packetStatsHostIdx(pkt);
    const Tick wait_ticks = curTick() > resp.tick ? curTick() - resp.tick : 0;
    const uint64_t wait_ns = ticksToNs(wait_ticks);
    const uint64_t wait_cycles =
        static_cast<uint64_t>(bridge.ticksToCycles(wait_ticks));
    // if ((respID == 0) && (bridge.is_switch == 0))
    //     printf("Upstream switch port received response\n");

    DPRINTF(PciBridge2, "trySend response addr 0x%x, outstanding %d\n",
            pkt->getAddr(), outstandingResponses);

    if (sendTimingResp(pkt)) {
        bridge.stats.response_wait_ns[host_idx] += wait_ns;
        bridge.stats.response_wait_cycles[host_idx] += wait_cycles;
        bridge.stats.response_wait_ns_hist.sample(wait_ns);

        // send successful
        DPRINTF(PciBridge2, "PciBridgeResponsePort::trySendTiming: before pop front , transmitlist size = %d\n",transmitList.size());
        transmitList.pop_front();
        bridge.stats.response_dequeue_count[host_idx]++;
        DPRINTF(PciBridge2, "trySend response successful\n");
        DPRINTF(PciBridge2, "PciBridgeResponsePort::trySendTiming: after pop front , transmitlist size = %d\n",transmitList.size());
        //////////////////////// esj 2023-04-24


        //esj 2025-05-08
        // assert(outstandingResponses != 0);
        //esj 2025-07-18
        // if(!pkt->cxl_pkt.is_controlflit && !pkt->cxl_pkt.retry_resp){
        if(!pkt->cxl_pkt.is_controlflit){
            //esj 2025-07-09
            // if(outstandingResponses != 0)
            //     --outstandingResponses;
            PciBridgeResponsePort *credit_port = this;
            if (name().find("response1_1") != std::string::npos) {
                credit_port = &bridge.responsePort1;
            } else if (name().find("response2_1") != std::string::npos) {
                credit_port = &bridge.responsePort2;
            } else if (name().find("response3_1") != std::string::npos) {
                credit_port = &bridge.responsePort3;
            } else if (name().find("response4_1") != std::string::npos) {
                credit_port = &bridge.responsePort4;
            }
            if (credit_port->outstandingResponses != 0) {
                --credit_port->outstandingResponses;
            }

            DPRINTF(PciBridge2, "PciBridgeResponsePort::trySendTiming --outstandingResponses = %d\n",outstandingResponses);
        }

        // DPRINTF(PciBridge2, "PciBridgeResponsePort::trySendTiming --outstandingResponses = %d\n",outstandingResponses);
        ////////////////////////////
       // if(respID==2 && bridge.is_switch ==0) printf("Outstanding responses dec %u\n", outstandingResponses) ;

        // If there are more packets to send, schedule event to try again.
        if (!transmitList.empty()) {
            DeferredPacket next_resp = transmitList.front();
            DPRINTF(PciBridge2, "Scheduling next send\n");
            //esj 2024-10-03
            bridge.schedule(sendEvent, std::max(next_resp.tick,bridge.clockEdge()));
            // bridge.schedule(sendEvent, next_resp.tick);
        }

        // if there is space in the request queue and we were stalling
        // a request, it will definitely be possible to accept it now
        // since there is guaranteed space in the response queue
        if (retryReq) {
            DPRINTF(PciBridge2, "Request waiting for retry, now retrying\n");
            retryReq = false;
            sendRetryReq();
        }
    }
    else {
        bridge.stats.response_send_blocked_count[host_idx]++;
    }

    // if the send failed, then we try again once we receive a retry,
    // and therefore there is no need to take any action
}

void
PciBridge2::PciBridgeRequestPort::recvReqRetry()
{
    DPRINTF(PciBridge2, "PciBridgeRequestPort::recvReqRetry\n");
    trySendTiming();
}

void
PciBridge2::PciBridgeResponsePort::recvRespRetry()
{
    DPRINTF(PciBridge2, "PciBridgeResponsePort::recvReqRetry\n");
    trySendTiming();
}

Tick
PciBridge2::PciBridgeResponsePort::recvAtomic(PacketPtr pkt)
{

   //printf("REcv atomic called\n") ;
    const uint8_t source_host_idx = clampStatsHostIdx(rcid);
    pkt->source_host_idx = source_host_idx;
    pkt->is_from_second_response_port = (source_host_idx != 0);

    if (bridge.accessCxlHostBridgeMmio(pkt, source_host_idx))
        return bridge.clockPeriod();

    if (pkt->req_bus[source_host_idx] == -1) // response port assigns the requester id
   {
        if(respID ==0)
        {

        pkt->req_bus[source_host_idx] = bridge.upstreamStorage(source_host_idx)->pci_bus ;
        pkt->req_dev[source_host_idx] = 0 ;
        pkt->req_func[source_host_idx] = 0 ;

        }
        else if (respID == 1)
            {
            //  printf("Root Port 1 received atomic DMA access\n") ;
            pkt->req_bus[source_host_idx] =
                bridge.downstreamStorage(0, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[source_host_idx] = 0 ;
            pkt->req_func[source_host_idx] = 0 ;
            //  printf("Root Port 1 received atomic DMA access , appending %d\n" , (int)pkt->req_bus[rcid]) ;
            }
        else if (respID == 2)
        {

            pkt->req_bus[source_host_idx] =
                bridge.downstreamStorage(1, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[source_host_idx] = 0 ;
            pkt->req_func[source_host_idx] = 0 ;
            //   printf("Root Port 2 received atomic DMA access , appending %d\n" , (int)pkt->req_bus[rcid]) ;
        }

        else if (respID == 3)
        {
            //  printf("Root port 3 received DMA\n") ;

            pkt->req_bus[source_host_idx] =
                bridge.downstreamStorage(2, source_host_idx)->storage1.SecondaryBusNumber ;
            pkt->req_dev[source_host_idx] = 0 ;
            pkt->req_func[source_host_idx] = 0 ;
            //  printf("Root Port 3 received atomic DMA access , appending %d\n" , (int)pkt->req_bus[rcid]) ;
        }

        //esj 2025-04-21
        else if(respID ==4)
        {
        pkt->req_bus[source_host_idx] = bridge.upstreamStorage(source_host_idx)->pci_bus ;
        pkt->req_dev[source_host_idx] = 0 ;
        pkt->req_func[source_host_idx] = 0 ;

        }
        else if(respID == 7 )
        {
        pkt->req_bus[source_host_idx] = bridge.upstreamStorage(source_host_idx)->pci_bus ;
        pkt->req_dev[source_host_idx] = 0 ;
        pkt->req_func[source_host_idx] = 0 ;
        }
        else if(respID == 8 )
        {
        pkt->req_bus[source_host_idx] = bridge.upstreamStorage(source_host_idx)->pci_bus ;
        pkt->req_dev[source_host_idx] = 0 ;
        pkt->req_func[source_host_idx] = 0 ;
        }
   }

    DPRINTF(PciBridge2, "pcibridge ::recvAtomic: pkt addr = %x, pkt = %s\n", pkt->getAddr(), pkt->print());
    //esj 2025-05-08
    // PciBridgeRequestPort * requestPort = bridge.getRequestPort(pkt->getAddr()) ;
    PciBridgeRequestPort * requestPort =
        bridge.getRequestPort(pkt->getAddr(), source_host_idx);
    if (bridge.is_switch && pkt->cxl_flag && pkt->is_cxl_io) {
        requestPort = (PciBridge2::PciBridgeRequestPort*) &bridge.requestPort3;
    }
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");
    // return  requestPort->sendAtomic(pkt);

    //esj 2024-08-03
    Flex_Bus(pkt);
    Tick latency = requestPort->sendAtomic(pkt);
    if(bridge.is_switch) {
        latency += delay;
    }
    if(pkt->cxl_flag)
        DPRINTF(PciBridge2,"esj %s latenncy = %d, latency+50ns = %d\n",__func__,latency,latency+delay);

    return latency;
    //////////////
}

void
PciBridge2::PciBridgeResponsePort::recvFunctional(PacketPtr pkt)
{
    // printf("REceive functional called\n") ;
    uint8_t source_host_idx = rcid;
    if (source_host_idx >= Packet::MaxPciRequesterIds) {
        source_host_idx = Packet::MaxPciRequesterIds - 1;
    }
    pkt->source_host_idx = source_host_idx;
    pkt->is_from_second_response_port = (source_host_idx != 0);

    if (bridge.accessCxlHostBridgeMmio(pkt, source_host_idx))
        return;

    PciBridgeRequestPort * requestPort = bridge.getRequestPort(pkt->getAddr(), source_host_idx) ;
    pkt->pushLabel(name());

    // check the response queue
    for (auto i = transmitList.begin();  i != transmitList.end(); ++i) {
        if (pkt->checkFunctional((*i).pkt)) {
            pkt->makeResponse();
            return;
        }
    }

    // also check the request port's request queue
    if (requestPort->checkFunctional(pkt)) {
        return;
    }

    pkt->popLabel();

    // fall through if pkt still not satisfied
    requestPort->sendFunctional(pkt);
}

bool
PciBridge2::PciBridgeRequestPort::checkFunctional(PacketPtr pkt)
{
    auto scan_list = [&](const std::deque<DeferredPacket> &queue) {
        for (auto i = queue.begin(); i != queue.end(); ++i) {
            if (pkt->checkFunctional((*i).pkt)) {
                pkt->makeResponse();
                return true;
            }
        }
        return false;
    };

    if (bridge.routing_mode == DEFAULT) {
        return scan_list(transmitList);
    }

    return scan_list(transmitList_1st) || scan_list(transmitList_2nd) ||
           scan_list(transmitList_3rd) || scan_list(transmitList_4th);
}


void
PciBridge2::PciBridgeResponsePort::fill_ranges(AddrRangeList & ranges , config_class * storage_ptr) const
{
    //esj 2024-02-16
    uint32_t barsize[2] ;
    //uint64_t barsize[2] ;
    barsize[0] = storage_ptr->barsize[0] ;
    barsize[1] = storage_ptr->barsize[1] ;
    Addr Bar0 =  storage_ptr->getBar0() ;
    Addr Bar1 =  storage_ptr->getBar1() ;
    // Addr Bar0 =  storage_ptr->storage1.Bar0 ; //esj 2024-02-17
    // Addr Bar1 =  storage_ptr->storage1.Bar1 ;
    Addr MemoryBase  = storage_ptr->getMemoryBase() ;
    Addr MemoryLimit = storage_ptr->getMemoryLimit() ;
    Addr PrefetchableMemoryBase =  storage_ptr->getPrefetchableMemoryBase() ;
    Addr PrefetchableMemoryLimit = storage_ptr->getPrefetchableMemoryLimit() ;
    Addr IOLimit = storage_ptr->getIOLimit() ;
    Addr IOBase =  storage_ptr->getIOBase() ;
    //uint8_t flag1, flag2, flag3 ;
    //flag1 = 0 ;
    //flag2 = 0 ;
    //flag3 = 0 ;
    //DPRINTF(PciBridge2, "esj fill_range ");
    //DPRINTF(PciBridge2, "esj Memory base is %0x , memory limit is %0x\n" , MemoryBase, MemoryLimit) ;
    //DPRINTF(PciBridge2, "esj Prefetch Memory Base is %0x , Prefetch MEmory Limit is %0x\n" , (unsigned int) PrefetchableMemoryBase , (unsigned int) PrefetchableMemoryLimit) ;
    if(barsize[0]!=0 && Bar0!=0)
    {
        DPRINTF(PciBridge2, "Bar0 assigned\n") ;
        ranges.push_back(RangeSize(Bar0 , barsize[0])) ;
    }

    if(barsize[1]!=0 && Bar1!=0)
    {
        ranges.push_back(RangeSize(Bar1, barsize[1])) ;
        DPRINTF(PciBridge2, "Bar 1 assigned \n") ;
    }
    if(storage_ptr->valid_memory_base && storage_ptr->valid_memory_limit && (MemoryBase < MemoryLimit) && (MemoryBase != 0))
    {
        //flag1 = 1 ;
        DPRINTF(PciBridge2, "Memory base is %0x , memory limit is %0x\n" , MemoryBase, MemoryLimit) ;
        //esj 2025-04-23
        // if(storage_ptr->pci_dev == 0x0d93){
        //     DPRINTF(PciBridge2, "cxl checkin");
        //     warn("cxl checkin");
        //     gem5::trace::cxl_check = 1; //esj 2024-03-06
        // }

        //esj 2025-04-23
        // if(gem5::trace::cxl_check == 0){
        if(gem5::trace::cxl_check == 0 && (respID == 4)){
            DPRINTF(PciBridge2, "cxl check 0\n");
            DPRINTF(PciBridge2, "cxl checkin 1\n");
            gem5::trace::cxl_check = 1; //esj 2024-03-06
        }
        else if(gem5::trace::cxl_check == 1 && (respID == 0)){
            DPRINTF(PciBridge2, "cxl check 1\n");
            DPRINTF(PciBridge2, "cxl checkin 2\n");
            gem5::trace::cxl_check = 2;
        }

        ranges.push_back(RangeIn(MemoryBase, MemoryLimit)) ;
    }
    if(storage_ptr->valid_prefetchable_memory_base && storage_ptr->valid_prefetchable_memory_limit && (PrefetchableMemoryBase < PrefetchableMemoryLimit) && (PrefetchableMemoryBase != 0))
    {
        //flag2 = 1 ;
        DPRINTF(PciBridge2, "Prefetch Memory Base is %0x , Prefetch MEmory Limit is %0x\n" , (unsigned int) PrefetchableMemoryBase , (unsigned int) PrefetchableMemoryLimit) ;
        ranges.push_back(RangeIn(PrefetchableMemoryBase, PrefetchableMemoryLimit)) ;
    }
    if(storage_ptr->valid_io_base && storage_ptr->valid_io_limit && (IOBase < IOLimit) && (IOBase != 0))
    {
        //flag3 = 1 ;
        DPRINTF(PciBridge2, "pushing back addr io ranges %0x - %0x\n" , (unsigned int)IOBase ,(unsigned int) IOLimit) ;
        ranges.push_back(RangeIn(IOBase , IOLimit)) ;

        // printf("IO Base is %08x , IO limit is %08x\n" , (unsigned int) IOBase , (unsigned int)IOLimit) ;
    }

}



AddrRangeList
PciBridge2::PciBridgeResponsePort::getAddrRanges() const
{

    AddrRangeList  ranges ;
    const uint8_t host_idx = clampStatsHostIdx(rcid);
    // printf("%s, respID = %d, is_switch = %d\n",__func__,respID, bridge.is_switch);
    if (is_upstream && bridge.cxlChbsSize != 0)
        ranges.push_back(RangeSize(bridge.cxlChbsBase, bridge.cxlChbsSize));

    if((respID == 0) && (bridge.is_switch ==0))
    {
        fill_ranges(ranges , bridge.downstreamStorage(0, host_idx)) ;
        fill_ranges(ranges , bridge.downstreamStorage(1, host_idx)) ;
        fill_ranges(ranges , bridge.downstreamStorage(2, host_idx)) ;
        // printf("Root port ranges\n") ;
        // for (auto it = ranges.begin() ; it != ranges.end() ; it++)
        //  printf("Range is %16lx to %16lx\n" , (*it).start() , (*it).end()) ;
    }
    else if (((respID == 0) && (bridge.is_switch == 1)) || (respID == 5)) //esj 2025-07-09
    {
        // printf("Upstream switch port !!! - %d\n", respID) ;
        fill_ranges(ranges , bridge.upstreamStorage(host_idx)) ;
        // for (auto it = ranges.begin() ;  it!=ranges.end(); it++)
        // printf("Upstream switch port Range start %16lx , Upstream switch port Range end %16lx\n" , (*it).start() , (*it).end()) ;
    }
    else if (respID == 1)
    {
        fill_ranges(ranges , bridge.downstreamStorage(0, host_idx)) ;
    }

    else if (respID == 2)
    {
        fill_ranges(ranges , bridge.downstreamStorage(1, host_idx)) ;
    }
    else if (respID == 3)
    {
        fill_ranges(ranges , bridge.downstreamStorage(2, host_idx)) ;
    }
    else if (((respID == 4) && (bridge.is_switch == 1)) || (respID == 6)) //esj 2025-07-09
    {
        // printf("Upstream switch port !!! - %d\n", respID) ;
        fill_ranges(ranges , bridge.upstreamStorage(host_idx)) ;
        // for (auto it = ranges.begin() ;  it!=ranges.end(); it++)
        // printf("Upstream switch port Range start %16lx , Upstream switch port Range end %16lx\n" , (*it).start() , (*it).end()) ;
    }
    else if (respID == 7 || respID == 9)
    {
        fill_ranges(ranges, bridge.upstreamStorage(host_idx));
    }
    else if (respID == 8 || respID == 10)
    {
        fill_ranges(ranges, bridge.upstreamStorage(host_idx));
    }

    ranges.sort() ; // sort this list
        // This is a downstream response port used to accept dma requests from the device. Need to configure the range as the ~ of downstream ranges

    if(is_upstream)
        return ranges ;
    else
    {


        AddrRangeList downstream_ranges ;


        Addr last_val = 0 ;
        for (auto it = ranges.begin() ; it !=ranges.end() ; it++)
        {
            if(last_val < (*it).start()) downstream_ranges.push_back(RangeIn(last_val , ((*it).start()) - 1)) ;
            //DPRINTF(PciBridge2, "esj pcibridge last_val = %x\n",last_val);
            last_val = (*it).end() + 1 ;
        }

        if(last_val == 0) return downstream_ranges ;
        // downstream_ranges.push_back(RangeIn(last_val , ADDR_MAX)) ;
        downstream_ranges.push_back(RangeIn(last_val , 0xCCCCCCCCCCCCCCCC)) ;


        return downstream_ranges ;
    }


 return ranges ;

}


void
PciBridge2::serialize(CheckpointOut &cp) const
{
    SERIALIZE_ARRAY(storage_ptr4->storage1.data, sizeof(storage_ptr4->storage1.data)/sizeof(storage_ptr4->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr4->storage2.data, sizeof(storage_ptr4->storage2.data)/sizeof(storage_ptr4->storage2.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr1->storage1.data, sizeof(storage_ptr1->storage1.data)/sizeof(storage_ptr1->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr1->storage2.data, sizeof(storage_ptr1->storage2.data)/sizeof(storage_ptr1->storage2.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr2->storage1.data, sizeof(storage_ptr2->storage1.data)/sizeof(storage_ptr2->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr2->storage2.data, sizeof(storage_ptr2->storage2.data)/sizeof(storage_ptr2->storage2.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr3->storage1.data, sizeof(storage_ptr3->storage1.data)/sizeof(storage_ptr3->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr3->storage2.data, sizeof(storage_ptr3->storage2.data)/sizeof(storage_ptr3->storage2.data[0])) ;
    SERIALIZE_SCALAR(storage_ptr4->is_valid);
    SERIALIZE_SCALAR(storage_ptr4->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr4->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr4->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr4->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr4->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr4->valid_prefetchable_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr3->is_valid);
    SERIALIZE_SCALAR(storage_ptr3->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr3->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr3->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr3->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr3->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr3->valid_prefetchable_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr2->is_valid);
    SERIALIZE_SCALAR(storage_ptr2->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr2->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr2->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr2->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr2->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr2->valid_prefetchable_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr1->is_valid);
    SERIALIZE_SCALAR(storage_ptr1->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr1->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr1->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr1->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr1->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr1->valid_prefetchable_memory_limit) ;

    //esj 2025-04-21
    SERIALIZE_ARRAY(storage_ptr5->storage1.data, sizeof(storage_ptr5->storage1.data)/sizeof(storage_ptr5->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr5->storage2.data, sizeof(storage_ptr5->storage2.data)/sizeof(storage_ptr5->storage2.data[0])) ;
    SERIALIZE_SCALAR(storage_ptr5->is_valid);
    SERIALIZE_SCALAR(storage_ptr5->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr5->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr5->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr5->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr5->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr5->valid_prefetchable_memory_limit) ;

    SERIALIZE_ARRAY(storage_ptr6->storage1.data, sizeof(storage_ptr6->storage1.data)/sizeof(storage_ptr6->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr6->storage2.data, sizeof(storage_ptr6->storage2.data)/sizeof(storage_ptr6->storage2.data[0])) ;
    SERIALIZE_SCALAR(storage_ptr6->is_valid);
    SERIALIZE_SCALAR(storage_ptr6->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr6->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr6->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr6->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr6->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr6->valid_prefetchable_memory_limit) ;

    SERIALIZE_ARRAY(storage_ptr7->storage1.data, sizeof(storage_ptr7->storage1.data)/sizeof(storage_ptr7->storage1.data[0])) ;
    SERIALIZE_ARRAY(storage_ptr7->storage2.data, sizeof(storage_ptr7->storage2.data)/sizeof(storage_ptr7->storage2.data[0])) ;
    SERIALIZE_SCALAR(storage_ptr7->is_valid);
    SERIALIZE_SCALAR(storage_ptr7->valid_io_base) ;
    SERIALIZE_SCALAR(storage_ptr7->valid_io_limit) ;
    SERIALIZE_SCALAR(storage_ptr7->valid_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr7->valid_memory_limit) ;
    SERIALIZE_SCALAR(storage_ptr7->valid_prefetchable_memory_base) ;
    SERIALIZE_SCALAR(storage_ptr7->valid_prefetchable_memory_limit) ;

}

void
PciBridge2::unserialize(CheckpointIn &cp)
{
    printf("unserializing R.C\n") ;

    UNSERIALIZE_ARRAY(storage_ptr4->storage1.data, sizeof(storage_ptr4->storage1.data)/sizeof(storage_ptr4->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr4->storage2.data, sizeof(storage_ptr4->storage2.data)/sizeof(storage_ptr4->storage2.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr1->storage1.data, sizeof(storage_ptr1->storage1.data)/sizeof(storage_ptr1->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr1->storage2.data, sizeof(storage_ptr1->storage2.data)/sizeof(storage_ptr1->storage2.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr2->storage1.data, sizeof(storage_ptr2->storage1.data)/sizeof(storage_ptr2->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr2->storage2.data, sizeof(storage_ptr2->storage2.data)/sizeof(storage_ptr2->storage2.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr3->storage1.data, sizeof(storage_ptr3->storage1.data)/sizeof(storage_ptr3->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr3->storage2.data, sizeof(storage_ptr3->storage2.data)/sizeof(storage_ptr3->storage2.data[0])) ;

    UNSERIALIZE_SCALAR(storage_ptr4->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr4->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr4->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr4->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr4->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr4->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr4->valid_prefetchable_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr3->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr3->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr3->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr3->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr3->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr3->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr3->valid_prefetchable_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr2->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr2->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr2->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr2->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr2->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr2->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr2->valid_prefetchable_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr1->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr1->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr1->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr1->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr1->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr1->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr1->valid_prefetchable_memory_limit) ;

    //esj 2025-04-21
    UNSERIALIZE_ARRAY(storage_ptr5->storage1.data, sizeof(storage_ptr5->storage1.data)/sizeof(storage_ptr5->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr5->storage2.data, sizeof(storage_ptr5->storage2.data)/sizeof(storage_ptr5->storage2.data[0])) ;
    UNSERIALIZE_SCALAR(storage_ptr5->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr5->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr5->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr5->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr5->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr5->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr5->valid_prefetchable_memory_limit) ;

    UNSERIALIZE_ARRAY(storage_ptr6->storage1.data, sizeof(storage_ptr6->storage1.data)/sizeof(storage_ptr6->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr6->storage2.data, sizeof(storage_ptr6->storage2.data)/sizeof(storage_ptr6->storage2.data[0])) ;
    UNSERIALIZE_SCALAR(storage_ptr6->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr6->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr6->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr6->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr6->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr6->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr6->valid_prefetchable_memory_limit) ;

    UNSERIALIZE_ARRAY(storage_ptr7->storage1.data, sizeof(storage_ptr7->storage1.data)/sizeof(storage_ptr7->storage1.data[0])) ;
    UNSERIALIZE_ARRAY(storage_ptr7->storage2.data, sizeof(storage_ptr7->storage2.data)/sizeof(storage_ptr7->storage2.data[0])) ;
    UNSERIALIZE_SCALAR(storage_ptr7->is_valid);
    UNSERIALIZE_SCALAR(storage_ptr7->valid_io_base) ;
    UNSERIALIZE_SCALAR(storage_ptr7->valid_io_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr7->valid_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr7->valid_memory_limit) ;
    UNSERIALIZE_SCALAR(storage_ptr7->valid_prefetchable_memory_base) ;
    UNSERIALIZE_SCALAR(storage_ptr7->valid_prefetchable_memory_limit) ;

    //esj 2025-04-21
    // responsePort.public_sendRangeChange() ;
    responsePort1.public_sendRangeChange() ;
    responsePort2.public_sendRangeChange() ;

    //esj 2025-07-09
    responsePort1_1.public_sendRangeChange() ;
    responsePort2_1.public_sendRangeChange() ;
    responsePort3.public_sendRangeChange() ;
    responsePort4.public_sendRangeChange() ;
    responsePort3_1.public_sendRangeChange() ;
    responsePort4_1.public_sendRangeChange() ;


    responsePort_DMA1.public_sendRangeChange() ;
    responsePort_DMA2.public_sendRangeChange() ;
    responsePort_DMA3.public_sendRangeChange() ;


}
PciBridge2::PciBridgeRequestPort&
PciBridge2::getRequestPort(const std::string &if_name, PortID idx)
{
    if (if_name == "request1")
        return requestPort1;
    else if(if_name == "request2")
        return requestPort2 ;
    else if (if_name == "request3")
        return requestPort3 ;
    //esj 2025-04-21
    else if (if_name == "request_dma1")
        return requestPort_DMA1 ;
    else if (if_name == "request_dma2")
        return requestPort_DMA2 ;
    else if (if_name == "request_dma3")
        return requestPort_DMA3 ;
    else if (if_name == "request_dma4")
        return requestPort_DMA4 ;

    //esj 2025-06-22
    else if (if_name == "request1_1")
        return requestPort1_1;
    else if(if_name == "request2_1")
        return requestPort2_1 ;
    else if (if_name == "request3_1")
        return requestPort3_1 ;
    else if (if_name == "request1_2")
        return requestPort1_2;
    else if(if_name == "request2_2")
        return requestPort2_2 ;
    else if (if_name == "request3_2")
        return requestPort3_2 ;
    //
    else
        fatal("%s does not have any port named %s\n", name(), if_name);
}

PciBridge2::PciBridgeResponsePort&
PciBridge2::getResponsePort(const std::string &if_name, PortID idx)
{
    //esj 2025-04-21
    if (if_name == "response1")
        return responsePort1;
    else if (if_name == "response2")
        return responsePort2;
    //

    else if (if_name == "response_dma1")
        return responsePort_DMA1 ;
    else if (if_name == "response_dma2")
        return responsePort_DMA2 ;
    else if (if_name == "response_dma3")
        return responsePort_DMA3 ;
    //esj 2025-06-22
    else if (if_name == "response1_1")
        return responsePort1_1;
    else if (if_name == "response2_1")
        return responsePort2_1;
    else if (if_name == "response3")
        return responsePort3;
    else if (if_name == "response4")
        return responsePort4;
    else if (if_name == "response3_1")
        return responsePort3_1;
    else if (if_name == "response4_1")
        return responsePort4_1;

    else
        fatal("%s does not have any port named %s\n", name(), if_name);
}

Port & PciBridge2::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "request1")
        return requestPort1;
    else if(if_name == "request2")
        return requestPort2 ;
    else if (if_name == "request3")
        return requestPort3 ;

    //esj 2025-04-21
    else if (if_name == "request_dma1")
        return requestPort_DMA1 ;
    else if (if_name == "request_dma2")
        return requestPort_DMA2 ;
    else if (if_name == "request_dma3")
        return requestPort_DMA3 ;
    else if (if_name == "request_dma4")
        return requestPort_DMA4 ;
    else if (if_name == "response1")
        return responsePort1;
    else if (if_name == "response2")
        return responsePort2;
    //
    else if (if_name == "response_dma1")
        return responsePort_DMA1 ;
    else if (if_name == "response_dma2")
        return responsePort_DMA2 ;
    else if (if_name == "response_dma3")
        return responsePort_DMA3 ;
    //esj 2025-06-22
    else if (if_name == "request1_1")
        return requestPort1_1;
    else if(if_name == "request2_1")
        return requestPort2_1 ;
    else if (if_name == "request3_1")
        return requestPort3_1 ;
    else if (if_name == "request1_2")
        return requestPort1_2;
    else if(if_name == "request2_2")
        return requestPort2_2 ;
    else if (if_name == "request3_2")
        return requestPort3_2 ;
    else if (if_name == "response1_1")
        return responsePort1_1;
    else if (if_name == "response2_1")
        return responsePort2_1;
    else if (if_name == "response3")
        return responsePort3;
    else if (if_name == "response4")
        return responsePort4;
    else if (if_name == "response3_1")
        return responsePort3_1;
    else if (if_name == "response4_1")
        return responsePort4_1;
    else
        fatal("%s does not have any port named %s\n", name(), if_name);
}
}
