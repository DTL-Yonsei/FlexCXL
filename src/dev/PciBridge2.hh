#ifndef __DEV_PCIBRIDGE_HH__
#define __DEV_PCIBRIDGE_HH__

// #include "dev/pci/host.hh"
#include <deque>
#include "params/PciBridge2.hh"
#include "dev/pci/types.hh"
#include "pci/host.hh"
// #include "dev/pci/device.hh"
#include "dev/pci/pcireg.h"
#include "mem/port.hh"
#include "base/types.hh"
#include "sim/clocked_object.hh"
#include "base/statistics.hh"
#include <array>
#include <cstring>
#include <vector>

#define PCIE_CONFIG_SIZE 0xFFF  // PCi express has an extended configuration space also. Total config space is 4096 B. Range of config addresses is from 0-4095.
#define PCIE_HEADER_SIZE 64    // The standard PCIe/PCI header is just 64 B. Other config space areas are occupied by extended capability registers and extended config space registers(only in PCIe).
#define PXCAPSIZE 36 // Pcie capability occupies 9 Dwords for PCi-Pci bridge.
#define SLOT_REG_BASE 20
#define SLOT_REG_LIMIT 27
#define IO_MASK 0x00000001
#define MEM_MASK 0x0000000F
#define prefetch_mask 0x00000004
#define IO_BASE_MASK 0x0F
#define IO_LIMIT_MASK 0x0F
#define PREFETCH_MEM_BASE_MASK 0x000
#define PREFETCH_MEM_LIMIT_MASK 0x000F
#define MMIO_BASE_MASK 0x000F
#define MMIO_LIMIT_MASK 0x000F
#define IO_BASE_SHIFT 4096
#define PREFETCH_BASE_SHIFT 1048576
#define ADDR_MAX 0xFFFFFFFFFFFFFFFF // 2^64 -1

namespace gem5
{
    class PciBridge2;

    //esj 2026-02-11
    enum RoutingModeType {
        DEFAULT,
        ROUND_ROBIN,
        PRIORITY
    };

    class config_class
    {
        public:
            union config_storage
            {
                uint8_t data[64];
                struct
                {
                    uint16_t VendorId ;
                    uint16_t DeviceId ;
                    uint16_t Command ;
                    uint16_t Status ;
                    uint8_t Revision ;
                    uint8_t ProgIF ;
                    uint8_t SubClassCode ;
                    uint8_t ClassCode ;
                    uint8_t CacheLineSize ;
                    uint8_t LatencyTimer ;
                    uint8_t HeaderType ;
                    uint8_t BIST ;
                    uint32_t Bar0 ;
                    uint32_t Bar1 ;
                    uint8_t  PrimaryBusNumber ;
                    uint8_t  SecondaryBusNumber ;
                    uint8_t  SubordinateBusNumber ;
                    uint8_t  SecondaryLatencyTimer ;
                    uint8_t  IOBase ;
                    uint8_t  IOLimit ;
                    uint16_t  SecondaryStatus ;
                    uint16_t  MemoryBase ;
                    uint16_t  MemoryLimit ;
                    uint16_t  PrefetchableMemoryBase ;
                    uint16_t  PrefetchableMemoryLimit ;
                    uint32_t  PrefetchableMemoryBaseUpper ;
                    uint32_t  PrefetchableMemoryLimitUpper ;
                    uint16_t  IOBaseUpper ;
                    uint16_t  IOLimitUpper ;
                    uint8_t   CapabilityPointer ;
                    uint8_t reserved[3] ;           // initialize reserved fields to 0
                    uint32_t ExpansionROMBaseAddress ;
                    uint8_t InterruptLine ;
                    uint8_t InterruptPin ;
                    uint16_t BridgeControl ;
                };
            } storage1;

            //여기에 cxl port 관련 dvsev을 넣으면 되는건가
            union pcie_capability_storage
            {
                uint8_t data[36];
                struct
                {
                    uint8_t PXCAPCapId ;
                    uint8_t PXCAPNextCapability ;
                    uint16_t PXCAPCapabilities;
                    uint32_t PXCAPDevCapabilities ;
                    uint16_t PXCAPDevStatus ;
                    uint16_t PXCAPDevCtrl ;
                    uint32_t PXCAPLinkCap;
                    uint16_t PXCAPLinkCtrl ;
                    uint16_t PXCAPLinkStatus ;
                    uint32_t PXCAPSlotCapabilities ; // set to 0 , assume that corresponding PCIe device is not in slot
                    uint16_t PXCAPSlotControl ; // set to 0 , assume that corresponding PCIe device is not in add -in card slot
                    uint16_t PXCAPSlotStatus ; // set to 0 for above reason.
                    uint32_t PXCAPRootControl ;
                    uint32_t PXCAPRootStatus ;
                };
            }   storage2;

            // uint64_t barsize[2] ; // size of BARs if implementeed for the bridge //esj 2024-02-16
            uint32_t barsize[2] ; // size of BARs if implementeed for the bridge
            uint32_t barflags[2] ; // whether the BARs have been written to with the base address.
            int pci_bus, pci_dev, pci_func ; // Bus , device and Function numbers for the PCI bridge
            Tick readConfig(PacketPtr pkt) ;  // Functions so configuration software can read and write values to the bridge
            Tick writeConfig(PacketPtr pkt) ;
            bool isWritable(int offset) ; // function to see if config address being written to has a R/W property. Return False if that address is read-only.
            uint32_t BarSize[2] ; // To store sizes of BARs , if implemented.
            // Any need to implement get AddrRange function ??? Seems to be needed only for PIO port, which bridge doesn't have. No need for DMA related fns either
            Tick configDelay ; // Time to be returned when a configuration access is made. Since no PIO port , no need for PIO latency either.
            PciBusAddr BridgeAddr ;
            uint8_t PXCAPBaseOffset ; // offset of the PCIe capability structure in config space. All PCIe devices have to implement this .
            config_class(int pci_bus , int pci_dev, int pci_func , uint8_t root_port_number):BridgeAddr((uint8_t)pci_bus , (uint8_t)pci_dev , (uint8_t)pci_func) {}  // constructor just initalizes the BridgeAddr object, which is used by the PCI host for config accesses
            PciBridge2 * bridge ;
            int is_switch ;
            Addr getBar0() ;
            Addr getBar1() ;
            // Addr getBar() ;
            Addr getIOBase() ;
            Addr getIOLimit() ;
            Addr getMemoryBase() ;
            Addr getMemoryLimit() ;
            Addr getPrefetchableMemoryBase() ;
            Addr getPrefetchableMemoryLimit() ;

            uint8_t id ;
            uint8_t is_valid ;

            bool valid_io_base ;
            bool valid_io_limit ;
            bool valid_prefetchable_memory_base ;
            bool valid_prefetchable_memory_limit ;
            bool valid_memory_base ;
            bool valid_memory_limit ;

            uint8_t MSICAP_BASE;
            MSICAP msicap;

            uint8_t MSIXCAP_BASE;
            uint8_t MSIXCAP_ID_OFFSET;
            uint8_t MSIXCAP_MXC_OFFSET;
            uint8_t MSIXCAP_MTAB_OFFSET;
            uint8_t MSIXCAP_MPBA_OFFSET;
            int MSIX_TABLE_OFFSET;
            int MSIX_TABLE_END;
            int MSIX_PBA_OFFSET;
            int MSIX_PBA_END;
            MSIXCAP msixcap;

            uint8_t PXCAP_BASE;
            PXCAP pxcap;
            /** @} */

            /** MSIX Table and PBA Structures */
            std::vector<MSIXTable> msix_table;
            std::vector<MSIXPbaEntry> msix_pba;

            // std::array<PciBar *, 2> BARs{};



    };

    class PciBridge2 :  public ClockedObject
    {
        public:
            class DeferredPacket
            {

            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
            };

            class PciBridgeResponsePort : public ResponsePort
            {
                private:
                    PciBridge2& bridge;
                    /** Minimum request delay though this bridge. */
                    const Cycles delay;
                    // const Tick delay;
                    /**
                    * Response packet queue. Response packets are held in this
                    * queue for a specified delay to model the processing delay
                    * of the bridge. We use a deque as we need to iterate over
                    * the items for functional accesses.
                    */
                    std::deque<DeferredPacket> transmitList;

                    /** Counter to track the outstanding responses. */
                    unsigned int outstandingResponses;

                    /** If we should send a retry when space becomes available. */
                    bool retryReq;
                    bool is_upstream ;

                    /** Max queue size for reserved responses. */
                    unsigned int respQueueLimit;

                    /**
                     * Upstream caches need this packet until true is returned, so
                     * hold it for deletion until a subsequent call
                     */
                    std::unique_ptr<Packet> pendingDelete;

                    /**
                     * Is this side blocked from accepting new response packets.
                     *
                     * @return true if the reserved space has reached the set limit
                     */
                    bool respQueueFull() const;

                    /**
                     * Handle send event, scheduled when the packet at the head of
                     * the response queue is ready to transmit (for timing
                     * accesses only).
                     */
                    void trySendTiming();

                    /** Send event for the response queue. */
                    EventFunctionWrapper sendEvent;

                public:
                    /**
                     * Constructor for the BridgeResponsePort.
                     *
                     * @param _name the port name including the owner
                     * @param _bridge the structural owner
                     * @param _RequestPort the Request port on the other side of the bridge
                     * @param _delay the delay in cycles from receiving to sending
                     * @param _resp_limit the size of the response queue
                     * @param _ranges a number of address ranges to forward
                     */

                    PciBridgeResponsePort(const std::string& _name, PciBridge2& _bridge, bool upstream, uint8_t respID,uint8_t rcid ,
                            Cycles _delay,
                            //Tick _delay,
                            int _resp_limit);
                    uint8_t respID ; // slave port id
                    uint8_t rcid ; // root complex id
                    void fill_ranges(AddrRangeList & ranges ,
                                     config_class * storage_ptr) const ;
                    /**
                     * Queue a response packet to be sent out later and also schedule
                     * a send if necessary.
                     *
                     * @param pkt a response to send out after a delay
                     * @param when tick when response packet should be sent
                     */
                    void schedTimingResp(PacketPtr pkt, Tick when);
                    void public_sendRangeChange()
                    {
                        //esj 2025-05-08
                        // sendRangeChange() ;
                        // warn("esj PciBridgeResponsePort::public_sendRangeChange()\n");
                        bridge.responsePort1.sendRangeChange();
                        bridge.responsePort2.sendRangeChange();
                        //esj 2025-07-09
                        bridge.responsePort1_1.sendRangeChange();
                        bridge.responsePort2_1.sendRangeChange();
                        bridge.responsePort3.sendRangeChange();
                        bridge.responsePort4.sendRangeChange();
                        bridge.responsePort3_1.sendRangeChange();
                        bridge.responsePort4_1.sendRangeChange();
                    }

                    /**
                     * Retry any stalled request that we have failed to accept at
                     * an earlier point in time. This call will do nothing if no
                     * request is waiting.
                     */
                    void retryStalledReq();

                protected:
                    //PciHost::DeviceInterface hostInterface; //esj 2024-02-17

                    /** When receiving a timing request from the peer port,
                        pass it to the bridge. */
                    bool recvTimingReq(PacketPtr pkt);

                    /** When receiving a retry request from the peer port,
                        pass it to the bridge. */
                    void recvRespRetry();

                    /** When receiving a Atomic requestfrom the peer port,
                        pass it to the bridge. */
                    Tick recvAtomic(PacketPtr pkt);

                    /** When receiving a Functional request from the peer port,
                        pass it to the bridge. */
                    void recvFunctional(PacketPtr pkt);

                    /** When receiving a address range request the peer port,
                        pass it to the bridge. */
                    AddrRangeList getAddrRanges() const;

                    // get muxing delay of CXL packet, add CXL protocol id
                    Tick Flex_Bus(PacketPtr pkt);

            };
            class PciBridgeRequestPort : public RequestPort
            {

            private:

                /** The bridge to which this port belongs. */
                PciBridge2& bridge;
                uint64_t totalCount ;
                /**
                 * The slave port on the other side of the bridge.
                 */
                // PciBridgeSlavePort& slavePort;
                // const Tick delay;
                /** Minimum delay though this bridge. */
                const Cycles delay;
                // const Tick delay;

                /**
                 * Request packet queue. Request packets are held in this
                 * queue for a specified delay to model the processing delay
                 * of the bridge.  We use a deque as we need to iterate over
                 * the items for functional accesses.
                 */
                std::deque<DeferredPacket> transmitList;

                //esj 2026-02-11
                std::deque<DeferredPacket> transmitList_1st;
                std::deque<DeferredPacket> transmitList_2nd;
                std::deque<DeferredPacket> transmitList_3rd;
                std::deque<DeferredPacket> transmitList_4th;

                //esj 2026-04-06
                uint8_t next_transaction_list_index = 0;
                size_t priority_cycle_index = 0;
                std::vector<uint8_t> priority_cycle;
                bool priority_cycle_initialized = false;

                std::deque<DeferredPacket>& hostQueue(uint8_t host_idx);
                const std::deque<DeferredPacket>& hostQueue(uint8_t host_idx) const;
                size_t hostQueueSizeTotal() const;
                bool hasPendingHostReq() const;
                uint8_t packetHostIdx(const PacketPtr pkt) const;
                DeferredPacket pickRoundRobinReq();
                DeferredPacket pickPriorityReq();
                void ensurePriorityCycleInitialized();
                Tick peekNextReadyTick() const;
                void popSentPacketFromHostQueue(PacketPtr pkt);

                /** Max queue size for request packets */
                const unsigned int reqQueueLimit;

                /**
                 * Handle send event, scheduled when the packet at the head of
                 * the outbound queue is ready to transmit (for timing
                 * accesses only).
                 */
                void trySendTiming();
                void incCount() ;

                /** Send event for the request queue. */
                EventFunctionWrapper sendEvent;
                EventFunctionWrapper countEvent ;

            public:

                /**
                 * Constructor for the BridgeMasterPort.
                 *
                 * @param _name the port name including the owner
                 * @param _bridge the structural owner
                 * @param _ResponsePort the slave port on the other side of the bridge
                 * @param _delay the delay in cycles from receiving to sending
                 * @param _req_limit the size of the request queue
                 */
                PciBridgeRequestPort(const std::string& _name, PciBridge2& _bridge,uint8_t rcid ,
                                Cycles _delay,
                                // Tick _delay,
                                int _req_limit);

                /**
                 * Is this side blocked from accepting new request packets.
                 *
                 * @return true if the occupied space has reached the set limit
                 */
                bool reqQueueFull() const;
                bool reqQueueFull(uint8_t host_idx) const;

                /**
                 * Queue a request packet to be sent out later and also schedule
                 * a send if necessary.
                 *
                 * @param pkt a request to send out after a delay
                 * @param when tick when response packet should be sent
                 */
                void schedTimingReq(PacketPtr pkt, Tick when);

                /**
                 * Check a functional request against the packets in our
                 * request queue.
                 *
                 * @param pkt packet to check against
                 *
                 * @return true if we find a match
                 */
                bool checkFunctional(PacketPtr pkt);
                uint8_t rcid ;

            protected:

                /** When receiving a timing request from the peer port,
                    pass it to the bridge. */
                bool recvTimingResp(PacketPtr pkt);

                /** When receiving a retry request from the peer port,
                    pass it to the bridge. */
                void recvReqRetry();

                // BitUnion32(Bar)
                //     Bitfield<31, 3> addr;
                //     SubBitUnion(type, 2, 1)
                //         Bitfield<2> wide;
                //         Bitfield<1> reserved;
                //     EndSubBitUnion(type)
                //     Bitfield<0> io;
                // EndBitUnion(Bar)
            };

            public:
                struct BridgeStats : public statistics::Group
                {
                    BridgeStats(PciBridge2 &bridge);

                    void regStats() override;

                    PciBridge2 &bridge;

                    statistics::Vector request_enqueue_count;
                    statistics::Vector request_grant_count;
                    statistics::Vector request_dequeue_count;
                    statistics::Vector request_queue_full_count;
                    statistics::Vector request_send_blocked_count;
                    statistics::Vector request_wait_ns;
                    statistics::Vector request_wait_cycles;
                    statistics::Formula request_wait_avg_ns;
                    statistics::Formula request_wait_avg_cycles;
                    statistics::Vector request_queue_occupancy_total;
                    statistics::Vector request_queue_occupancy_samples;
                    statistics::Vector request_queue_occupancy_max;
                    statistics::Formula request_queue_occupancy_avg;
                    statistics::Histogram request_wait_ns_hist;
                    statistics::Histogram request_queue_occupancy_hist;

                    statistics::Vector response_enqueue_count;
                    statistics::Vector response_dequeue_count;
                    statistics::Vector response_queue_full_count;
                    statistics::Vector response_send_blocked_count;
                    statistics::Vector response_wait_ns;
                    statistics::Vector response_wait_cycles;
                    statistics::Formula response_wait_avg_ns;
                    statistics::Formula response_wait_avg_cycles;
                    statistics::Vector response_queue_occupancy_total;
                    statistics::Vector response_queue_occupancy_samples;
                    statistics::Vector response_queue_occupancy_max;
                    statistics::Formula response_queue_occupancy_avg;
                    statistics::Histogram response_wait_ns_hist;
                    statistics::Histogram response_queue_occupancy_hist;
                };

                //esj 2025-04-21
                // PciBridgeResponsePort responsePort;
                PciBridgeResponsePort responsePort1;
                PciBridgeResponsePort responsePort2;

                PciBridgeResponsePort responsePort_DMA1;
                PciBridgeResponsePort responsePort_DMA2;
                PciBridgeResponsePort responsePort_DMA3;
                //esj 2025-04-21
                // PciBridgeRequestPort requestPort_DMA;
                PciBridgeRequestPort requestPort_DMA1;
                PciBridgeRequestPort requestPort_DMA2;
                PciBridgeRequestPort requestPort_DMA3;
                PciBridgeRequestPort requestPort_DMA4;

                PciBridgeRequestPort requestPort1;
                PciBridgeRequestPort requestPort2;
                PciBridgeRequestPort requestPort3;

                //esj 2025-06-22
                PciBridgeRequestPort requestPort1_1;
                PciBridgeRequestPort requestPort2_1;
                PciBridgeRequestPort requestPort3_1;
                PciBridgeResponsePort responsePort1_1;
                PciBridgeResponsePort responsePort2_1;
                PciBridgeRequestPort requestPort1_2;
                PciBridgeRequestPort requestPort2_2;
                PciBridgeRequestPort requestPort3_2;
                PciBridgeResponsePort responsePort3;
                PciBridgeResponsePort responsePort4;
                PciBridgeResponsePort responsePort3_1;
                PciBridgeResponsePort responsePort4_1;

                BridgeStats stats;


                PciBridgeRequestPort * getRequestPort(Addr address, uint8_t source_host_idx);
                PciBridgeResponsePort * getResponsePort(int bus_num);
                virtual PciBridgeRequestPort& getRequestPort(const std::string& if_name,
                                          PortID idx = InvalidPortID);
                virtual PciBridgeResponsePort& getResponsePort(const std::string& if_name,
                                        PortID idx = InvalidPortID);
                Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
                virtual void init();
                config_class * storage_ptr1 ; // create a new config_class when the bridge corresponding to a root port is created.
                config_class * storage_ptr2 ; // Configuration for root port 2
                config_class * storage_ptr3 ; // config structure for R.P. 3
                config_class * storage_ptr4 ;
                config_class * storage_ptr5 ;
                config_class * storage_ptr6 ;
                config_class * storage_ptr7 ;
                using DownstreamStorageSlots = std::array<config_class *, 3>;
                std::array<DownstreamStorageSlots, Packet::MaxPciRequesterIds>
                    hostDownstreamStorage;
                void initHostDownstreamStorage();
                void destroyHostDownstreamStorage();
                config_class *cloneStorage(const config_class *storage_ptr);
                config_class *downstreamStorage(uint8_t port_idx, uint8_t host_idx) const;
                config_class *upstreamStorage(uint8_t host_idx) const;
                uint8_t clampHostIdx(uint8_t host_idx) const;
                void initialize_ports (config_class * storage_ptr , const PciBridge2Params & p ) ;
                static constexpr std::size_t CxlHostBridgeRegSize = 0x10000;
                Addr cxlChbsBase = 0;
                uint64_t cxlChbsSize = 0;
                Addr cxlHdmBase = 0;
                uint64_t cxlHdmSize = 0;
                std::array<std::array<uint8_t, CxlHostBridgeRegSize>,
                    Packet::MaxPciRequesterIds> cxlHostBridgeRegsByHost;
                void initCxlHostBridgeRegs(
                    std::array<uint8_t, CxlHostBridgeRegSize> &regs,
                    uint64_t hdm_size);
                bool cxlHostBridgeOffset(Addr addr, Addr &offset) const;
                bool accessCxlHostBridgeMmio(PacketPtr pkt, uint8_t host_idx);
                void serialize(CheckpointOut &cp) const override;
                void unserialize(CheckpointIn &cp) override;
                uint8_t rc_id ; // root complex id
                uint8_t rc_id_second ; // root complex id 2
                uint8_t rc_id_third ; // root complex id 3
                uint8_t rc_id_fourth ; // root complex id 4
                int is_switch = 0;
                int is_transmit ;

                //esj 2026-02-11
                RoutingModeType routing_mode = DEFAULT;
                uint8_t priority_limit = 10;
                std::array<uint8_t, 4> priority_host_ratios{{10, 0, 0, 0}};

                // config_class *register_bridge_init(const PciBridge2Params &p,PciBridge2 * bridge, int num);

                PciBridge2(const PciBridge2Params & p);
                ~PciBridge2();
    };

}
#endif
