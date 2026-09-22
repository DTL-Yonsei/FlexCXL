

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_CRC_check.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_CRC_check : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };

        class CXL_CRC_checkResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_CRC_check* crc_checker;

            CXL_CRC_checkResponsePort(const std::string& _name, CXL_CRC_check* crc_checker);
        };

         class CXL_CRC_checkRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_CRC_check* crc_checker;

            void recvRangeChange() override { 
                if (crc_checker) {  // parent validation check
                    crc_checker->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_CRC_checkRequestPort(const std::string& _name, CXL_CRC_check* crc_checker);
        };   

        public:

        CXL_CRC_checkRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_CRC_checkResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_CRC_checkResponsePort in_port ; 
        CXL_CRC_checkRequestPort out_port ;

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
 
        Tick CRC_packet_check(PacketPtr pkt);

        //esj 2025-11-21
        float success_rate;
        // double success_rate;


       

        PacketPtr packet;

        bool Device2Host_busy;
        bool Host2Device_busy;
        bool retry;

            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_CRC_checkParams;
        CXL_CRC_check (const Param & p) ; 
    } ; 
}


