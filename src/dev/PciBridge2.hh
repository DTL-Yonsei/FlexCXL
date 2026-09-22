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
#include "dev/CXL/CXLRequestAdmission.hh"
#include "base/types.hh"
#include "sim/clocked_object.hh"
#include "sim/stats.hh"
#include <array>
#include <cstddef>
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

            //esj 2025-05-04
            union cxl_dvsec_port
            {
                uint8_t data[40];
                struct
                {
                    uint32_t dvsec_cap_hdr;
                    uint32_t dvsec_hdr1;
                    uint16_t dvsec_hdr2;
                    uint16_t port_ext_status;
                    uint16_t port_ext_control;
                    uint8_t alt_bus_base;
                    uint8_t alt_bus_limit;
                    uint8_t alt_prefetch_mem_base;
                    uint8_t alt_prefetch_mem_limit;
                    uint32_t alt_prefetch_mem_base_high;
                    uint32_t alt_prefetch_mem_limit_high;
                    uint32_t cxl_rcrb_base;
                    uint32_t cxl_rcrb_base_high;
                };
            }cxl_dvsec_port;

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
                const Tick enqueueTick;

                DeferredPacket(PacketPtr _pkt, Tick _tick,
                               Tick _enqueue_tick = 0)
                    : tick(_tick), pkt(_pkt), enqueueTick(_enqueue_tick)
                { }
            };

            class PciBridgeRequestPort;

            class PciBridgeResponsePort : public ResponsePort,
                                          public CXLRequestAdmission
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

                    //esj 2025-05-16
                    // std::deque<DeferredPacket> transmitList_write;

                    /** Counter to track the outstanding responses. */
                    unsigned int outstandingResponses;

                    /** If we should send a retry when space becomes available. */
                    bool retryReq;
                    bool is_upstream ;

                    /** Max queue size for reserved responses. */
                    unsigned int respQueueLimit;

                    // Validation-observation state. These members never
                    // participate in bridge flow-control decisions; they pair
                    // aggregate stats and optional structured begin/end events.
                    bool validationQueueFullActive = false;
                    Tick validationQueueFullStart = 0;
                    PacketPtr validationQueueFullPkt = nullptr;
                    PciBridgeRequestPort *validationQueueFullRequestPort =
                        nullptr;
                    bool validationQueueFullIsResponseReservation = false;
                    unsigned int validationQueueFullNeededSlots = 1;
                    bool validationDownstreamBlocked = false;
                    Tick validationDownstreamBlockStart = 0;

                    void validationQueueFullBegin(
                        PacketPtr pkt, PciBridgeRequestPort *request_port,
                        bool response_reservation);
                    void validationQueueFullEnd();
                    void validationDownstreamBlockBegin(PacketPtr pkt);
                    void validationDownstreamBlockEnd();

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
                    void fill_ranges(AddrRangeList & ranges , config_class * storage_ptr) const ;
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
                        //esj 2025-05-07
                        // warn("esj PciBridgeResponsePort::public_sendRangeChange()\n");
                        sendRangeChange() ;
                        // bridge.responsePort.sendRangeChange();
                    }

                    /**
                     * Retry any stalled request that we have failed to accept at
                     * an earlier point in time. This call will do nothing if no
                     * request is waiting.
                     */
                    void retryStalledReq();

                    bool canAcceptCxlRequest(PacketPtr pkt) override;

                    /** Rebase an open episode at a statistics epoch boundary.
                     *  Queue-full return: 0=closed, 1=request queue,
                     *  2=response reservation. */
                    int rebaseQueueFullStats(Tick epoch);
                    bool rebaseDownstreamBlockStats(Tick epoch);

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

                //esj 2025-06-01
                std::deque<DeferredPacket> transmitList_write;
                EventFunctionWrapper sendEvent_write;
                void trySendTiming_write();
                bool is_read_turn = false;
                //

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

                // Validation-only pairing state for a downstream timing-port
                // rejection. It is ignored by production flow control.
                bool validationDownstreamBlocked = false;
                Tick validationDownstreamBlockStart = 0;

                void validationDownstreamBlockBegin(PacketPtr pkt);
                void validationDownstreamBlockEnd();

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

                unsigned int queueOccupancy() const
                {
                    return transmitList.size();
                }

                unsigned int queueCapacity() const
                {
                    return reqQueueLimit;
                }

                /**
                 * Queue a request packet to be sent out later and also schedule
                 * a send if necessary.
                 *
                 * @param pkt a request to send out after a delay
                 * @param when tick when response packet should be sent
                 */
                void schedTimingReq(PacketPtr pkt, Tick when);

                /** Rebase an open downstream-block episode at a stats reset. */
                bool rebaseDownstreamBlockStats(Tick epoch);

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

            /**
             * Aggregate CXL traffic and backpressure observations for a
             * production PCIe switch.  These counters are independent of the
             * optional JSONL validation logger so long full-system runs can
             * keep bounded output while still exposing causal witnesses.
             */
            uint64_t cxlRequestQueueOccupancy = 0;
            uint64_t cxlResponseQueueOccupancy = 0;
            uint64_t cxlRequestQueueHighWatermark = 0;
            uint64_t cxlResponseQueueHighWatermark = 0;
            uint64_t cxlRequestEnqueueToSendHighWatermark = 0;
            uint64_t cxlResponseEnqueueToSendHighWatermark = 0;
            uint64_t cxlQueueFullOpen = 0;
            uint64_t cxlDownstreamBlockOpen = 0;

            struct SwitchStats : public statistics::Group
            {
                explicit SwitchStats(PciBridge2 &bridge);

                void resetStats() override;
                void preDumpStats() override;

                PciBridge2 &bridge;

                statistics::Scalar cxlRequestEnqueues;
                statistics::Scalar cxlRequestDequeues;
                statistics::Scalar cxlResponseEnqueues;
                statistics::Scalar cxlResponseDequeues;
                statistics::Scalar cxlRequestBytes;
                statistics::Scalar cxlResponseBytes;
                statistics::Scalar cxlRequestQueueCurrent;
                statistics::Scalar cxlResponseQueueCurrent;
                statistics::Scalar cxlRequestQueueMax;
                statistics::Scalar cxlResponseQueueMax;
                statistics::Scalar cxlRequestQueueFullEpisodes;
                statistics::Scalar cxlRequestQueueFullTicks;
                statistics::Scalar cxlResponseReservationFullEpisodes;
                statistics::Scalar cxlResponseReservationFullTicks;
                statistics::Scalar cxlQueueFullOpenEpisodes;
                statistics::Scalar cxlRequestDownstreamBlockEpisodes;
                statistics::Scalar cxlRequestDownstreamBlockTicks;
                statistics::Scalar cxlResponseDownstreamBlockEpisodes;
                statistics::Scalar cxlResponseDownstreamBlockTicks;
                statistics::Scalar cxlDownstreamBlockOpenEpisodes;
                statistics::Scalar cxlRequestEnqueueToSendTicks;
                statistics::Scalar cxlResponseEnqueueToSendTicks;
                statistics::Scalar cxlRequestEnqueueToSendMax;
                statistics::Scalar cxlResponseEnqueueToSendMax;
            } stats;

            public:
                PciBridgeResponsePort responsePort;
                PciBridgeResponsePort responsePort_DMA1;
                PciBridgeResponsePort responsePort_DMA2;
                PciBridgeResponsePort responsePort_DMA3;
                PciBridgeRequestPort requestPort_DMA;
                PciBridgeRequestPort requestPort1;
                PciBridgeRequestPort requestPort2;
                PciBridgeRequestPort requestPort3;

                //esj 2025-06-22
                PciBridgeRequestPort requestPort1_1;
                PciBridgeRequestPort requestPort2_1;
                PciBridgeRequestPort requestPort3_1;
                PciBridgeResponsePort responsePort_1;


                PciBridgeRequestPort * getRequestPort(Addr address);
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
                void initialize_ports (config_class * storage_ptr , const PciBridge2Params & p ) ;
                static constexpr std::size_t CxlHostBridgeRegSize = 0x10000;
                Addr cxlChbsBase = 0;
                uint64_t cxlChbsSize = 0;
                Addr cxlHdmBase = 0;
                uint64_t cxlHdmSize = 0;
                bool validationForceCxlWindow = false;
                Addr validationCxlBarStart = 0;
                uint64_t validationCxlBarSize = 0;
                std::array<uint8_t, CxlHostBridgeRegSize> cxlHostBridgeRegs;
                void initCxlHostBridgeRegs();
                bool cxlHostBridgeOffset(Addr addr, Addr &offset) const;
                bool accessCxlHostBridgeMmio(PacketPtr pkt);
                void serialize(CheckpointOut &cp) const override;
                void unserialize(CheckpointIn &cp) override;
                uint8_t rc_id ; // root complex id
                int is_switch = 0;
                int is_transmit ;

                // config_class *register_bridge_init(const PciBridge2Params &p,PciBridge2 * bridge, int num);

                PciBridge2(const PciBridge2Params & p);
                ~PciBridge2();
    };

}
#endif
