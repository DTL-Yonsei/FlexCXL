

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_Deframer.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Deframer : public ClockedObject //SimObject
    {

        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };

        class CXL_DeframerResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Deframer* deframer;

            CXL_DeframerResponsePort(const std::string& _name, CXL_Deframer* deframer);
        };

         class CXL_DeframerRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_Deframer* deframer;

            void recvRangeChange() override { 
                if (deframer) {  // parent validation check
                    deframer->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_DeframerRequestPort(const std::string& _name, CXL_Deframer* deframer);
        };   

        public:

        CXL_DeframerRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_DeframerResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_DeframerResponsePort in_port ; 
        CXL_DeframerRequestPort out_port ;

        int cycleperflit;

        bool Host2Device_busy;
        bool Device2Host_busy;

        EventFunctionWrapper DeframingEvent;
        void Deframing();

        EventFunctionWrapper Deframing_respEvent;
        void Deframing_resp();
        
        PacketPtr packet;

        bool retry;

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

        //esj 2025-02-19
        Tick logical_PHY_delay;

        /////////////////////

        Tick Deframing_packet(PacketPtr pkt);

            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_DeframerParams;
        CXL_Deframer (const Param & p) ; 
    } ; 
}


