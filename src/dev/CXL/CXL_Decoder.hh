
#include <deque>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_Decoder.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Decoder : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };
        
        class CXL_DecoderResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            void recvRespRetry();
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Decoder* decoder;

            CXL_DecoderResponsePort(const std::string& _name, CXL_Decoder* decoder);
        };

         class CXL_DecoderRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){}
            CXL_Decoder* decoder;

            void recvRangeChange() override { 
                if (decoder) {  // parent validation check
                    decoder->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_DecoderRequestPort(const std::string& _name, CXL_Decoder* decoder);
        };   

        public:

        CXL_DecoderRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_DecoderResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_DecoderResponsePort in_port ; 
        CXL_DecoderRequestPort out_port ;

        int cycleperflit;

        EventFunctionWrapper DecodingEvent;
        void Decoding();

        EventFunctionWrapper Decoding_respEvent;
        void Decoding_resp();

        PacketPtr packet;

        Tick Decoding_packet(PacketPtr packet);

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
        using Param = CXL_DecoderParams;
        CXL_Decoder (const Param & p) ; 
    } ; 
}


