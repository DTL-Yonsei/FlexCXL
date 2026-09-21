

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_Unpacking.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Unpacking : public ClockedObject //SimObject
    {

        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };
        
        class CXL_UnpackingResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry(){
                //esj 2025-07-12
                unpacker->trySendTimingResp();
            }

            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Unpacking* unpacker;

            CXL_UnpackingResponsePort(const std::string& _name, CXL_Unpacking* unpacker);
        };

         class CXL_UnpackingRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){
                //esj 2025-06-29
                unpacker->trySendTiming();
            }
            CXL_Unpacking* unpacker;

            void recvRangeChange() override { 
                if (unpacker) {  // parent validation check
                    unpacker->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_UnpackingRequestPort(const std::string& _name, CXL_Unpacking* unpacker);
        };   

        public:

        CXL_UnpackingRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_UnpackingResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_UnpackingResponsePort in_port ; 
        CXL_UnpackingRequestPort out_port ;

        int cycleperflit;

        EventFunctionWrapper UnpackingEvent;
        void Unpacking();

        EventFunctionWrapper Unpacking_respEvent;
        void Unpacking_resp();

        Tick UnPacking_packet(PacketPtr pkt);

        PacketPtr packet;

        bool retry;
        bool Host2Device_busy;
        bool Device2Host_busy;


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
        using Param = CXL_UnpackingParams;
        CXL_Unpacking (const Param & p) ; 
    } ; 
}


