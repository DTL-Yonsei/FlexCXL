

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_Framer.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Framer : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };

        class CXL_FramerResponsePort : public ResponsePort
        {
            
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Framer* framer;

            CXL_FramerResponsePort(const std::string& _name, CXL_Framer* framer);
        };

         class CXL_FramerRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_Framer* framer;

            void recvRangeChange() override { 
                if (framer) {  // parent validation check
                    framer->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_FramerRequestPort(const std::string& _name, CXL_Framer* framer);
        };   

        public:

        CXL_FramerRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_FramerResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_FramerResponsePort in_port ; 
        CXL_FramerRequestPort out_port ;

        int cycleperflit;

        EventFunctionWrapper FramingEvent;
        void Framing();

        EventFunctionWrapper Framing_respEvent;
        void Framing_resp();

        PacketPtr packet;

        Tick Framing_packet(PacketPtr pkt);

        bool retry;
        bool Device2Host_busy;
        bool Host2Device_busy;

        //esj 2025-02-19
        Tick logical_PHY_delay;

        
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
            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_FramerParams;
        CXL_Framer (const Param & p) ; 
    } ; 
}


