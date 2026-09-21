

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_FlexBus.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_FlexBus : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                std::string portname;

                DeferredPacket(PacketPtr _pkt, Tick _tick,std::string _portname) : tick(_tick), pkt(_pkt), portname(_portname)
                { }
        };

        class CXL_FlexBusResponsePort : public ResponsePort
        {
            
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_FlexBus* flexbus;

            CXL_FlexBusResponsePort(const std::string& _name, CXL_FlexBus* flexbus);
        };

         class CXL_FlexBusRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_FlexBus* flexbus;

            void recvRangeChange() override { 
                if (flexbus) {  // parent validation check
                    if(flexbus->is_host){
                        flexbus->Host_resp_port.sendRangeChange();
                    }
                    else{
                        flexbus->Host_resp_port.sendRangeChange();
                    }
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_FlexBusRequestPort(const std::string& _name, CXL_FlexBus* flexbus);
        };   

        public:

        CXL_FlexBusRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_FlexBusResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_FlexBusResponsePort Host_resp_port,Host_req_port,PCIe_resp_port,CXL_in_port ; 
        CXL_FlexBusRequestPort Device_resp_port,Device_req_port,PCIe_req_port;
        CXL_FlexBusRequestPort CXL_out_port;
        
        bool is_host;

        int cycleperflit;

        // EventFunctionWrapper ArbitratingEvent;
        // void Arbitrating();

        // EventFunctionWrapper Arbitrating_respEvent;
        // void Arbitrating_resp();

        PacketPtr packet;

        Tick Arbitrating_packet(PacketPtr pkt);

        bool retry;
        bool Device2Host_busy;
        bool Host2Device_busy;

        
        //esj 2024-12-07

        std::deque<DeferredPacket> transmitList_host;
        std::deque<DeferredPacket> transmitList_device;

        bool retryReq_host;
        bool retryResp_host;
        
        void trySendTiming();
        void schedTimingReq(PacketPtr pkt, Tick when, std::string portname);
        EventFunctionWrapper sendEvent;

        void trySendTimingResp();
        void schedTimingResp(PacketPtr pkt, Tick when, std::string portname);
        EventFunctionWrapper sendEventResp;

        int maxQueueSize;

        /////////////////////
            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_FlexBusParams;
        CXL_FlexBus (const Param & p) ; 
    } ; 
}


