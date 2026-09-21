#include <array>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/addr_range.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "dev/cxl_dramsim3_wrapper.hh"
#include "dev/io_device.hh"
#include "dev/pci/device.hh"
#include "mem/abstract_mem.hh"
#include "mem/backdoor.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "params/CXLDRAMsim3.hh"
#include "sim/clocked_object.hh"
#include "sim/stats.hh"

// constexpr uint32_t ROM_SIZE = 0x20000;        // 128kB
//esj 2025-04-30
#define PCIE_CONFIG_SIZE 0xFFF

namespace gem5
{
    // class System;

    typedef enum _INTERRUPT_MODE{
        INTERRUPT_PIN,
        INTERRUPT_MSI,
        INTERRUPT_MSIX
    }INTERRUPT_MODE2;

    class CXLDRAMsim3 : public PciDevice {

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
                // DVSECHeader hdr;
                uint32_t cap_hdr;
                uint32_t dv_hdr1;
                uint16_t dv_hdr2;

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
                uint16_t cap3;
                uint8_t reserved[2];
            };
        }CXLDVSECDevice;

        //esj 2025-05-01
        typedef union CXLDVSECDevice_Locator
        {
            uint8_t data[36];
            struct
            {
                uint32_t cap_hdr;
                uint32_t dv_hdr1;
                uint16_t dv_hdr2;
                uint8_t reserved[2];
                uint32_t REGBLK1_LOW;
                uint32_t REGBLK1_HIGH;
                uint32_t REGBLK2_LOW;
                uint32_t REGBLK2_HIGH;
                uint32_t REGBLK3_LOW;
                uint32_t REGBLK3_HIGH;

            };
        }CXLDVSECDevice_Locator;

        //esj 2025-05-01
        typedef union CXLDVSECDevice_HeaderRegister
        {
            uint8_t data[4];
            struct
            {
                uint16_t capid;
                uint16_t version;
                uint8_t array_size;
            };
        }CXLDVSECDevice_HeaderRegister;

        protected:
            CXLDVSECDevice cxl_config;
            //esj 2025-05-01
            CXLDVSECDevice_Locator cxl_config_locator;
            CXLDVSECDevice_HeaderRegister cxl_config_header;
            using CxlRegisterBlock = std::array<uint8_t, 0x10000>;
            std::array<CxlRegisterBlock, Packet::MaxPciRequesterIds>
                hostCxlComponentRegs;
            std::array<CxlRegisterBlock, Packet::MaxPciRequesterIds>
                hostCxlMemdevRegs;
            std::array<std::array<uint8_t, 4>, Packet::MaxPciRequesterIds>
                hostCxlEventInterruptPolicies;
            std::array<PCIConfig, Packet::MaxPciRequesterIds> hostPciConfigs;
            struct HostPciBarRange
            {
                Addr base = 0;
                Addr size = 0;
                bool valid = false;
            };
            std::array<std::array<HostPciBarRange, 6>,
                Packet::MaxPciRequesterIds> hostPciBarRanges;
            struct HostPciCapabilityState
            {
                CXLDVSECDevice cxl_config;
                CXLDVSECDevice_Locator cxl_config_locator;
                PMCAP pmcap;
                MSICAP msicap;
                MSIXCAP msixcap;
                PXCAP pxcap;
                uint16_t vectors = 1;
                INTERRUPT_MODE2 mode = INTERRUPT_PIN;
            };
            std::array<HostPciCapabilityState, Packet::MaxPciRequesterIds>
                hostPciCapabilityStates;

            struct CXLStats : public statistics::Group
            {
                CXLStats(CXLDRAMsim3 &mem);

                void regStats() override;

                const CXLDRAMsim3 &mem;

                /** Number of total bytes read from this memory */
                statistics::Vector bytesRead;
                /** Number of instruction bytes read from this memory */
                statistics::Vector bytesInstRead;
                /** Number of bytes written to this memory */
                statistics::Vector bytesWritten;
                /** Number of read requests */
                statistics::Vector numReads;
                /** Number of write requests */
                statistics::Vector numWrites;
                /** Number of other requests */
                statistics::Vector numOther;
                /** Read bandwidth from this memory */
                statistics::Formula bwRead;
                /** Read bandwidth from this memory */
                statistics::Formula bwInstRead;
                /** Write bandwidth from this memory */
                statistics::Formula bwWrite;
                /** Total bandwidth from this memory */
                statistics::Formula bwTotal;
            } stats;
        private:


        //esj 2025-01-31
        /**
         * Locked address class that represents a physical address and a
         * context id.
         */
        class LockedAddr
        {

        private:

            // on alpha, minimum LL/SC granularity is 16 bytes, so lower
            // bits need to masked off.
            static const Addr Addr_Mask = 0xf;

        public:

            // locked address
            Addr addr;

            // locking hw context
            const ContextID contextId;

            static Addr mask(Addr paddr) { return (paddr & ~Addr_Mask); }

            // check for matching execution context
            bool matchesContext(const RequestPtr &req) const
            {
                assert(contextId != InvalidContextID);
                assert(req->hasContextId());
                return (contextId == req->contextId());
            }

            LockedAddr(const RequestPtr &req) : addr(mask(req->getPaddr())),
                                                contextId(req->contextId())
            {}

            // constructor for unserialization use
            LockedAddr(Addr _addr, int _cid) : addr(_addr), contextId(_cid)
            {}
        };

        class Memory{

            private:
            AddrRange range;
            // uint8_t* pmemAddr = nullptr;
            uint8_t* pmemAddr; //esj
            uint8_t* ownedPmemAddr = nullptr;
            bool inAddrMap = true;
            const std::string name_ = "CXLDRAMsim3::Memory";
            CXLDRAMsim3& owner;
            //esj
            int number =0;

            public:
            Memory(const AddrRange& range, CXLDRAMsim3& owner);
            inline uint8_t*
            toHostAddr(Addr addr) const
            {
                return pmemAddr + addr - range.start();
            }
            const std::string& name() const { return name_; }
            uint64_t size() const { return range.size(); }
            Addr start() const { return range.start(); }
            bool isInAddrMap() const { return inAddrMap; }
            void access(PacketPtr pkt);
            Memory(const Memory& other) = delete;
            Memory& operator=(const Memory& other) = delete;
            ~Memory();

            void change_addr(PacketPtr pkt); //esj 2024-06-28

            //esj 2025-01-31
            protected:
            // Are writes allowed to this memory
            bool writeable = true;

            // helper function for checkLockedAddrs(): we really want to
            // inline a quick check for an empty locked addr list (hopefully
            // the common case), and do the full list search (if necessary) in
            // this out-of-line function
            bool checkLockedAddrList(PacketPtr pkt);

            // Record the address of a load-locked operation so that we can
            // clear the execution context's lock flag if a matching store is
            // performed
            void trackLoadLocked(PacketPtr pkt);

            // Compare a store address with any locked addresses so we can
            // clear the lock flag appropriately.  Return value set to 'false'
            // if store operation should be suppressed (because it was a
            // conditional store and the address was no longer locked by the
            // requesting execution context), 'true' otherwise.  Note that
            // this method must be called on *all* stores since even
            // non-conditional stores must clear any matching lock addresses.
            bool
            writeOK(PacketPtr pkt)
            {
                const RequestPtr &req = pkt->req;
                if (!writeable)
                    return false;
                if (lockedAddrList.empty()) {
                    // no locked addrs: nothing to check, store_conditional fails
                    bool isLLSC = pkt->isLLSC();
                    if (isLLSC) {
                        req->setExtraData(0);
                    }
                    return !isLLSC; // only do write if not an sc
                } else {
                    // iterate over list...
                    return checkLockedAddrList(pkt);
                }
            }

            std::list<LockedAddr> lockedAddrList;
            // Backdoor to access this memory.
            MemBackdoor backdoor;

            public:
            /**
             * Set the host memory backing store to be used by this memory
             * controller.
             *
             * @param pmem_addr Pointer to a segment of host memory
             */
            void setBackingStore(uint8_t* pmem_addr);

            void
            getBackdoor(MemBackdoorPtr &bd_ptr)
            {
                if (lockedAddrList.empty() && backdoor.ptr())
                    bd_ptr = &backdoor;
            }

            /**
             * Get the list of locked addresses to allow checkpointing.
             */
            const std::list<LockedAddr> &
            getLockedAddrList() const
            {
                return lockedAddrList;
            }

            /**
             * Add a locked address to allow for checkpointing.
             */
            void
            addLockedAddr(LockedAddr addr)
            {
                backdoor.invalidate();
                lockedAddrList.push_back(addr);
            }

        };

        //esj 2025-05-19
        // class CXLResponsePort : public ResponsePort
        //     {
        //         private:
        //             CXLDRAMsim3& ctrl;

        //         public:

        //             CXLResponsePort(const std::string& _name, CXLDRAMsim3& _ctrl);


        //         protected:
        //             //PciHost::DeviceInterface hostInterface; //esj 2024-02-17

        //             /** When receiving a timing request from the peer port,
        //                 pass it to the bridge. */
        //             bool recvTimingReq(PacketPtr pkt);

        //             /** When receiving a retry request from the peer port,
        //                 pass it to the bridge. */
        //             void recvRespRetry();

        //             /** When receiving a Atomic requestfrom the peer port,
        //                 pass it to the bridge. */
        //             Tick recvAtomic(PacketPtr pkt);

        //             /** When receiving a Functional request from the peer port,
        //                 pass it to the bridge. */
        //             void recvFunctional(PacketPtr pkt);

        //             /** When receiving a address range request the peer port,
        //                 pass it to the bridge. */
        //             AddrRangeList getAddrRanges() const override{
        //                 return ctrl.getAddrRanges();
        //             }

        //     };


        //esj 2025-05-19
        // CXLResponsePort cxl_response_port;

        //esj 2025-06-22
        class CXL_resp_port : public ResponsePort
        {
            private:
                CXLDRAMsim3& ctrl;

            public:
                CXL_resp_port(const std::string& _name, CXLDRAMsim3& _ctrl);
                Tick recvAtomic(PacketPtr pkt){
                    if (ctrl.accessCxlRegisterMmio(pkt))
                        return ctrl.latency_;
                    ctrl.accessAndRespond(pkt);
                    return ctrl.latency_;
                };
                void recvFunctional(PacketPtr pkt){
                    if (ctrl.accessCxlRegisterMmio(pkt))
                        return;
                    ctrl.accessAndRespond(pkt);
                };
                bool recvTimingReq(PacketPtr pkt) override{
                    // warn("esj CXL_resp_port recvTimingReq addr = %08x data? = %d request? = %08x\n",pkt->getAddr(), pkt->hasData(), pkt->isRequest());
                    if (pkt->cxl_flag && pkt->is_cxl_mem)
                        return ctrl.recvTimingReq(pkt);
                    if (ctrl.recvCxlRegisterTimingReq(pkt))
                        return true;
                    ctrl.accessAndRespond(pkt);
                    return true;
                };
                AddrRangeList getAddrRanges() const override{
                    return ctrl.getAddrRanges();
                };
                virtual ~CXL_resp_port();

            protected:
                void
                recvRespRetry() override {
                    ctrl.recvRespRetry();
                };

        };

        CXL_resp_port Pio2;
        //


        /** Pointer to the System object.
         * This is used for getting the number of requestors in the system which is
         * needed when registering stats
         */
        System *_system;

        /** read the system pointer
        * Implemented for completeness with the setter
        * @return pointer to the system object */
        System* system() const { return _system; }
        /** Set the system pointer on this memory
         * This can't be done via a python parameter because the system needs
         * pointers to all the memories and the reverse would create a cycle in the
         * object graph. An init() this is set.
         * @param sys system pointer to set
         */
        void system(System *sys) { _system = sys; }

        Memory mem_;

        Tick latency_;

        Tick cxl_mem_latency_;

        //esj 2024-06-28
        uint64_t cxl_mem_size;
        Addr cxl_mem_start;
        Addr cxlChbsBase;
        uint64_t cxlChbsSize;
        uint32_t cxlLogicalDeviceCount;
        std::vector<uint64_t> hostVisibleCxlMemSizes;
        std::vector<uint64_t> hostCxlDeviceOffsets;

        //esj 2024-10-02
        bool switch_mode;

        uint64_t hostVisibleCxlMemSize(uint8_t host_idx) const;
        void applyCxlDvsecMemRange(CXLDVSECDevice &dvsec,
                                   uint64_t visible_size) const;
        void initCxlRegisterBlocks(CxlRegisterBlock &component_regs,
                                   CxlRegisterBlock &memdev_regs);
        uint8_t configHostIdx(PacketPtr pkt) const;
        void snapshotHostBarRanges(uint8_t host_idx);
        bool hostBarOffset(uint8_t host_idx, Addr addr, int &bar,
                           Addr &offset) const;
        bool cxlRegisterOffset(uint8_t host_idx, Addr addr, bool &memdev,
                               Addr &offset);
        bool accessCxlRegisterMmio(PacketPtr pkt);
        bool recvCxlRegisterTimingReq(PacketPtr pkt);
        void writeCxlRegisterBytes(uint8_t host_idx, bool memdev, Addr offset,
                                   const uint8_t *data, unsigned size);
        void executeCxlMailboxCommand(uint8_t host_idx);


        //esj 2024-02-09
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
        INTERRUPT_MODE2 mode;

        Tick _curTick(){
            Tick start = getTick(tickEvent);
            // warn("esj current tick = %d",start);
            return start;
        }

        // Stats
        //EventFunctionWrapper statUpdateEvent;
        statistics::Scalar *pStats;
        //esj 2024-02-09

        //esj 2024-04-10
        /**
         * Callback functions
         */
        std::function<void(uint64_t)> read_cb;
        std::function<void(uint64_t)> write_cb;

        //esj 2025-02-02
        std::function<void(uint64_t)> read_cb_write;
        std::function<void(uint64_t)> write_cb_write;

        /**
         * The actual DRAMsim3 wrapper
         */
        memory::CXLDRAMsim3Wrapper wrapper;
        //esj 2025-02-01
        memory::CXLDRAMsim3Wrapper wrapper_write;

        /**
         * Is the connected port waiting for a retry from us
         */
        bool retryReq;

        /**
         * Are we waiting for a retry for sending a response.
         */
        bool retryResp;
        /**
         * Keep track of when the wrapper is started.
         */
        Tick startTick;

        /**
         * Keep track of what packets are outstanding per
         * address, and do so separately for reads and writes. This is
         * done so that we can return the right packet on completion from
         * DRAMSim.
         */
        std::unordered_map<Addr, std::queue<PacketPtr> > outstandingReads;
        std::unordered_map<Addr, std::queue<PacketPtr> > outstandingWrites;

        /**
         * Count the number of outstanding transactions so that we can
         * block any further requests until there is space in DRAMsim3 and
         * the sending queue we need to buffer the response packets.
         */
        unsigned int nbrOutstandingReads;
        unsigned int nbrOutstandingWrites;

        /**
         * Queue to hold response packets until we can send them
         * back. This is needed as DRAMsim3 unconditionally passes
         * responses back without any flow control.
         */
        std::deque<PacketPtr> responseQueue;


        unsigned int nbrOutstanding() const;

        /**
         * When a packet is ready, use the "access()" method in
         * AbstractMemory to actually create the response packet, and send
         * it back to the outside world requestor.
         *
         * @param pkt The packet from the outside world
         */
        void accessAndRespond(PacketPtr pkt);

        void sendResponse();

        /**
         * Event to schedule sending of responses
         */
        EventFunctionWrapper sendResponseEvent;

        /**
         * Progress the controller one clock cycle.
         */
        void tick();

        /**
         * Event to schedule clock ticks
         */
        EventFunctionWrapper tickEvent;

        /**
        * Upstream caches need this packet until true is returned, so
        * hold it for deletion until a subsequent call
        */
        std::unique_ptr<Packet> pendingDelete;

        // PioPort2<CXLDRAMsim3> cxl_port;

        public:
        Tick read(PacketPtr pkt) override;
        Tick write(PacketPtr pkt) override;
        void change_addr(PacketPtr pkt); //esj 2024-06-28
        AddrRangeList getAddrRanges() const override;
        Tick resolve_cxl_mem(PacketPtr ptk);

        using Param = CXLDRAMsim3Params;
        CXLDRAMsim3(const Param &p);

        Tick writeConfig(PacketPtr pkt) override; //esj 2024-02-09
        Tick readConfig(PacketPtr pkt) override;  //esj 2024-02-09
        void getVendorID(uint16_t &, uint16_t &); //esj 2024-02-09
        void regStats() override;   //esj 2024-02-09
        void resetStats() override;  //esj 2024-02-09

        void change_cxl_mem_packet_size(PacketPtr pkt); //esj 2024-10-22
        //esj 2024-10-22
        unsigned int cxl_mem_NDR_resp_size = 4;//30bit -> 11byte CXL NDR, Req header size
        unsigned int cxl_mem_DRS_resp_size = 5;//40bit -> 11byte CXL DRS, Req header size
        /**
         * Read completion callback.
         *
         * @param id Channel id of the responder
         * @param addr Address of the request
         * @param cycle Internal cycle count of DRAMsim3
         */
        void readComplete(unsigned id, uint64_t addr);
        void readComplete_write(unsigned id, uint64_t addr); //esj 2025-02-02

        /**
         * Write completion callback.
         *
         * @param id Channel id of the responder
         * @param addr Address of the request
         * @param cycle Internal cycle count of DRAMsim3
         */
        void writeComplete(unsigned id, uint64_t addr);
        void writeComplete_write(unsigned id, uint64_t addr); //esj 2025-02-02
        void startup() override;

        //esj 2025-01-31
        DrainState drain() override;

        void recvRespRetry() ;
        // friend class PioPort2<CXLDRAMsim3>;
        Port &getPort(const std::string &if_name,PortID idx=InvalidPortID) override;

        //esj 2025-04-30
        void serialize(CheckpointOut &cp) const override;
        void unserialize(CheckpointIn &cp) override;


        //esj
        bool recvTimingReq(PacketPtr pkt);
    };

} // namespace gem5
