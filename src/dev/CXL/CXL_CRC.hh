

#include <deque>
#include <random>
#include <queue>
#include <string>
#include "base/statistics.hh"
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_CRC.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_CRC : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };

        class CXL_CRCResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_CRC* crc;

            CXL_CRCResponsePort(const std::string& _name, CXL_CRC* crc);
        };

         class CXL_CRCRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_CRC* crc;

            void recvRangeChange() override {
                if (crc) {  // parent validation check
                    crc->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }
            }

            CXL_CRCRequestPort(const std::string& _name, CXL_CRC* crc);
        };

        public:

        CXL_CRCRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_CRCResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_CRCResponsePort in_port ;
        CXL_CRCRequestPort out_port ;

        int cycleperflit;

        //esj 2024-12-07

        std::deque<DeferredPacket> transmitList_host;
        std::deque<DeferredPacket> transmitList_device;

        bool retryReq_host;
        bool retryResp_host;

        void trySendTiming();
        void schedTimingReq(PacketPtr pkt, Tick when);
        EventFunctionWrapper sendEvent;

        void trySendTimingResp();
        void schedTimingResp(PacketPtr pkt, Tick when);
        EventFunctionWrapper sendEventResp;

        int maxQueueSize;

        /////////////////////

        Tick CRC_packet(PacketPtr pkt);

        double error_rate;
        const bool validationSeededIidEnable;
        const uint64_t validationIidSeed;
        const bool validationIidRetryEligible;
        std::mt19937 validationIidRng;
        std::uniform_int_distribution<uint32_t> validationIidDist;

        struct CRCStats : public statistics::Group
        {
            CRCStats(CXL_CRC &crc);

            statistics::Scalar iidEligiblePackets;
            statistics::Scalar iidInjectedErrors;
            statistics::Scalar iidEligibleRetryPackets;
            statistics::Scalar iidInjectedRetryErrors;
            statistics::Scalar iidSkippedControlPackets;
            statistics::Scalar iidSkippedWriteResponsePackets;
            statistics::Scalar iidSkippedMetadataPackets;
        } stats;

        PacketPtr packet;

        bool Device2Host_busy;
        bool Host2Device_busy;
        bool retry;


        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_CRCParams;
        CXL_CRC (const Param & p) ;
    } ;
}
