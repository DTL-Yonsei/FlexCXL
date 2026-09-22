

#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_Packing.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Packing : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                Tick tick;
                PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };

        class CXL_PackingResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Packing* packer;

            CXL_PackingResponsePort(const std::string& _name, CXL_Packing* packer);
        };

         class CXL_PackingRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_Packing* packer;

            void recvRangeChange() override { 
                if (packer) {  // parent validation check
                    packer->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_PackingRequestPort(const std::string& _name, CXL_Packing* packer);
        };   

        public:

        CXL_PackingRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_PackingResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_PackingResponsePort in_port ; 
        CXL_PackingRequestPort out_port ;

        int cycleperflit;

        EventFunctionWrapper PackingEvent;
        void packing();

        EventFunctionWrapper Packing_respEvent;
        void packing_resp();

        //esj 2024-12-07

        std::deque<DeferredPacket> transmitList_host;
        std::deque<DeferredPacket> transmitList_device;

        bool retryReq_host;
        bool retryResp_host;
        
        void trySendTiming();
        void schedTimingReq(PacketPtr pkt, Tick when);
        size_t selectHostTxIndex() const;
        Tick nextHostTxWakeup() const;
        EventFunctionWrapper sendEvent;

        void trySendTimingResp();
        void schedTimingResp(PacketPtr pkt, Tick when);
        size_t selectDeviceTxIndex() const;
        Tick nextDeviceTxWakeup() const;
        EventFunctionWrapper sendEventResp;

        int maxQueueSize;

        /////////////////////
 
        Tick packing_packet(PacketPtr pkt);

       

        PacketPtr packet;

        bool Device2Host_busy;
        bool Host2Device_busy;
        bool retry;

            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_PackingParams;
        CXL_Packing (const Param & p) ; 
    } ; 
}
