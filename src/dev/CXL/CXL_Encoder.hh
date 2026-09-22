

#include <deque>
#include <list>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "dev/CXL/CXLRequestAdmission.hh"
#include "params/CXL_Encoder.hh"

#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"


namespace gem5
{
    class CXL_Encoder : public ClockedObject //SimObject
    {
        class DeferredPacket
        {
            public:

                const Tick tick;
                const PacketPtr pkt;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                { }
        };
        
        class CXL_EncoderResponsePort : public ResponsePort
        {
            public:
            AddrRangeList getAddrRanges() const {AddrRangeList temp; return temp;}

            // void recvRespRetry();
            void recvRespRetry(){
                //esj 2025-07-12
                encoder->trySendTimingResp();
            }
            bool recvTimingReq(PacketPtr pkt);
            void recvFunctional(PacketPtr pkt);
            Tick recvAtomic(PacketPtr pkt);

            CXL_Encoder* encoder;

            CXL_EncoderResponsePort(const std::string& _name, CXL_Encoder* encoder);
        };

         class CXL_EncoderRequestPort : public RequestPort
        {
            public:
            bool recvTimingResp(PacketPtr pkt) ;
            void recvReqRetry(){
                encoder->retryReq_host = false;
                encoder->scheduleRequestSend();
            }
            CXL_Encoder* encoder;

            void recvRangeChange() override { 
                if (encoder) {  // parent validation check
                    encoder->in_port.sendRangeChange();
                } else {
                    panic("Parent PCIELink is not set!");
                }            
            }
            
            CXL_EncoderRequestPort(const std::string& _name, CXL_Encoder* encoder);
        };   

        public:

        CXL_EncoderRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_EncoderResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        CXL_EncoderResponsePort in_port ; 
        CXL_EncoderRequestPort out_port ;

        int cycleperflit;

        EventFunctionWrapper EncodingEvent;
        void Encoding();

        EventFunctionWrapper Encoding_respEvent;
        void Encoding_resp();

        PacketPtr packet;

        bool Device2Host_busy;
        bool Host2Device_busy;
        bool retry;

        Tick Encoding_packet(PacketPtr packet);

        double ticksPerByte;
        int lanes;

        //esj 2024-12-07

        // Erase a selected control packet without changing packet contents
        // or the relative order of data requests.
        std::list<DeferredPacket> transmitList_host;
        std::deque<DeferredPacket> transmitList_device;

        bool retryReq_host;
        bool retryResp_host;
        
        void trySendTiming();
        void scheduleRequestSend();
        CXLRequestAdmission *requestAdmission = nullptr;
        void schedTimingReq(PacketPtr pkt, Tick when);
        EventFunctionWrapper sendEvent;

        void trySendTimingResp();
        void schedTimingResp(PacketPtr pkt, Tick when);
        EventFunctionWrapper sendEventResp;

        int maxQueueSize;

        /////////////////////
            
        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;
        void init() ;
        using Param = CXL_EncoderParams;
        CXL_Encoder (const Param & p) ; 
    } ; 
}

