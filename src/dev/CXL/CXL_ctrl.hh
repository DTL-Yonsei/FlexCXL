#include <deque>
#include <memory>
#include <queue>
#include <string>
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "params/CXL_ctrl.hh"
#include "base/str.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "sim/sim_events.hh"
#include "sim/system.hh"
#include "sim/clocked_object.hh"

#define ENCODING_FACTOR 1.02
#define CXL_mem_PHYSICAL_OVERHEAD 2
#define CXL_mem_REQUEST_HEADER_SIZE 16

namespace gem5
{

    class cxl_ReplayBuffer
    {
    public:

        int maximumSize ;

        std::deque<PacketPtr> queue ;

        //esj 2025-01-10
        std::deque<int> seqnum ;

        cxl_ReplayBuffer(int max_size) ;
        bool check_seqnum(uint64_t seq_num);

        int size() ;
        void pushBack (PacketPtr ptr) ;


        PacketPtr  popFront() ;
        PacketPtr popByAddress(Addr address);
        bool popByseqnum(uint64_t seq_num);

        // esj 2025-05-16
        bool popByseqnum_double(uint64_t seq_num, uint64_t seq_num2);

        PacketPtr popByseqnum2(uint64_t seq_num);
        bool popByseqnum3(uint64_t seq_num);
        bool popByseqnum4(uint64_t seq_num);
        bool findByseqnum(uint64_t seq_num);
        void findByseqnum2(uint64_t seq_num); //esj 2025-07-14
        PacketPtr popByseqnum5(uint64_t seq_num);
        PacketPtr popByseqnum6(uint64_t seq_num);
        PacketPtr popByseqnum7(uint64_t seq_num); //esj 2025-07-17
        void print_buffer();
        void print_buffer2();

        //esj 2025-01-24
        void eraseinvalid();
        PacketPtr
        front()
        {
            return queue.front() ;
        }

        PacketPtr  get(int idx) ;

        unsigned int buffer_size = 0;

        bool empty() ;

    };

    class TXSM{
        public:
        uint64_t send_packet_num;
        uint64_t send_flit_num;
        TXSM():
            send_packet_num(0),send_flit_num(0){}
    };
    class RXSM{
        public:
        uint64_t recv_packet_num;
        uint64_t recv_flit_num;
        RXSM():
            recv_packet_num(0),recv_flit_num(0){}
    };

    class CXL_Local_state{
        public:
        enum LRSM {
            RETRY_LOCAL_NORMAL = 0,
            RETRY_LLRREQ,
            RETRY_LOCAL_IDLE,
            RETRY_PHY_REINIT,
            RETRY_ABORT,
            Invalid_state
        };
        CXL_Local_state(LRSM val1) :
            Local_state(val1)
        {}
        CXL_Local_state() :
            Local_state(RETRY_LOCAL_NORMAL)
        {
            NUM_RETRY=0;
        }
        CXL_Local_state(const std::string& val1) :
            Local_state(Invalid_state)
        {
            if(val1 == "RETRY_LOCAL_NORMAL"){
                Local_state = RETRY_LOCAL_NORMAL;
            } else if(val1 == "RETRY_LLRREQ"){
                Local_state = RETRY_LLRREQ;
            } else if(val1 == "RETRY_LOCAL_IDLE"){
                Local_state = RETRY_LOCAL_IDLE;
            } else if(val1 == "RETRY_PHY_REINIT"){
                Local_state = RETRY_PHY_REINIT;
            } else if(val1 == "RETRY_ABORT"){
                Local_state = RETRY_ABORT;
            }
            assert("Unknown memory type." && Local_state != Invalid_state);
        }

        public:
        LRSM Local_state;
        uint16_t NUM_RETRY;
        uint16_t NUM_PHY_REINIT;
        std::string cur_stat(){
            switch(Local_state){
                case(RETRY_LOCAL_NORMAL):
                {
                    std::string val = "RETRY_LOCAL_NORMAL";
                    return val;
                }
                case(RETRY_LLRREQ):
                {
                    std::string val = "RETRY_LLRREQ";
                    return val;
                }
                case(RETRY_LOCAL_IDLE):
                {
                    std::string val = "RETRY_LOCAL_IDLE";
                    return val;
                }
                case(RETRY_PHY_REINIT):
                {
                    std::string val = "RETRY_PHY_REINIT";
                    return val;
                }

            }
        }
    };

    class CXL_Remote_state{
        public:
        enum RRSM {
            RETRY_REMOTE_NORMAL = 0,
            RETRY_LLRACK,
            Invalid_state
        };
        CXL_Remote_state(RRSM val1) :
            Remote_state(val1)
        {}
        CXL_Remote_state() :
            Remote_state(RETRY_REMOTE_NORMAL)
        {}
        CXL_Remote_state(const std::string& val1) :
            Remote_state(Invalid_state)
        {
            if(val1 == "RETRY_REMOTE_NORMAL"){
                Remote_state = RETRY_REMOTE_NORMAL;
            } else if(val1 == "RETRY_LLRACK"){
                Remote_state = RETRY_LLRACK;
            }
            assert("Unknown memory type." && Remote_state != Invalid_state);
        }

        public:
        RRSM Remote_state;
        std::string cur_stat(){
            switch(Remote_state){
                case(RETRY_REMOTE_NORMAL):
                {
                    std::string val = "RETRY_REMOTE_NORMAL";
                    return val;
                }
                case(RETRY_LLRACK):
                {
                    std::string val = "RETRY_LLRACK";
                    return val;
                }
                case(Invalid_state):
                {
                    std::string val = "Invalid_state";
                    return val;
                }

            }
        }
    };
    void copyPacketInfo(PacketPtr pkt);
    void returnPacketInfo(PacketPtr pkt);
    class CXL_CRC;
    class CXL_Decoder;
    class CXL_Encoder;
    class CXL_FlexBus;
    class CXL_Packing;
    class CXL_Unpacking;
    class CXL_Framer;
    class CXL_Deframer;
    class CXL_CRC_check;


    class CXL_ctrl : public ClockedObject //SimObject
    {


        // class Link;
        class DeferredPacket
        {

            public:

                Tick tick;
                PacketPtr pkt;
                //esj 2025-01-05
                bool valid;

                DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
                {
                    //esj 2025-01-05
                    valid = true;
                }
        };

        class retrypacket
        {
            public:
                uint64_t crc_error_seqnum;

                retrypacket(uint64_t _crc_error_seqnum) :  crc_error_seqnum(_crc_error_seqnum)
                {

                }
        };

        class CXL_ctrlResponsePort : public ResponsePort
        {
            public:

                //esj 2025-05-07
                AddrRangeList getAddrRanges() const { return ctrl->cxl_ranges;}

                //esj 2025-05-13
                void recvRespRetry() override ;

                bool recvTimingReq(PacketPtr pkt);
                void recvFunctional(PacketPtr pkt);
                Tick recvAtomic(PacketPtr pkt);

                CXL_ctrl* ctrl;
                CXL_ctrlResponsePort(const std::string& _name, CXL_ctrl* ctrl);
        };


        class CXL_ctrlRequestPort : public RequestPort
        {

            public:
                bool recvTimingResp(PacketPtr pkt) ;

                //esj 2025-05-08
                // void recvReqRetry(){}
                void recvReqRetry();

                CXL_ctrl* ctrl;
                // Link ** transmitLink ;

                //esj 2025-05-07
                void recvRangeChange() override {
                    if (ctrl) {  // parent validation check
                        if(ctrl->is_host){
                            if(name().find("downstreamRequest") != std::string::npos){
                                ctrl->cxl_ranges  = this->getAddrRanges();
                                if(ctrl->cxl_ranges.size() > 0){
                                    ctrl->upstreamResponse.sendRangeChange();
                                }
                            }
                        }
                        else{
                            if(name().find("upstreamRequest") != std::string::npos){
                                ctrl->cxl_ranges  =  this->getAddrRanges();

                                if(ctrl->cxl_ranges.size() > 0){
                                    ctrl->downstreamResponse.sendRangeChange();
                                }
                            }
                        }
                    } else {
                        panic("Parent PCIELink is not set!");
                    }
                }


                CXL_ctrlRequestPort(const std::string& _name, CXL_ctrl* ctrl);
        };

        //esj 2025-02-24
        struct CXLStats : public statistics::Group
        {
            CXLStats(CXL_ctrl &ctrl);

            // void regStats() override;

            void regStats() override;   //esj 2025-07-29
            void resetStats() override;  //esj 2025-07-29
            void preDumpStats() override;  //esj 2025-07-29

            CXL_ctrl &ctrl;

            statistics::Scalar control_flit_req_read;
            statistics::Scalar control_flit_req_write;
            statistics::Scalar control_flit_resp_read;
            statistics::Scalar control_flit_resp_write;
            statistics::Scalar data_flit_req_read;
            statistics::Scalar data_flit_req_write;
            statistics::Scalar data_flit_resp_read;
            statistics::Scalar data_flit_resp_write;

            statistics::Scalar controller_req_queue_occupancy_total;
            statistics::Scalar controller_req_queue_occupancy_samples;
            statistics::Scalar controller_req_queue_occupancy_max;
            statistics::Formula controller_req_queue_occupancy_avg;
            statistics::Scalar controller_resp_queue_occupancy_total;
            statistics::Scalar controller_resp_queue_occupancy_samples;
            statistics::Scalar controller_resp_queue_occupancy_max;
            statistics::Formula controller_resp_queue_occupancy_avg;

            statistics::Scalar replay_buffer_host_occupancy_total;
            statistics::Scalar replay_buffer_host_occupancy_samples;
            statistics::Scalar replay_buffer_host_occupancy_max;
            statistics::Formula replay_buffer_host_occupancy_avg;
            statistics::Scalar replay_buffer_device_occupancy_total;
            statistics::Scalar replay_buffer_device_occupancy_samples;
            statistics::Scalar replay_buffer_device_occupancy_max;
            statistics::Formula replay_buffer_device_occupancy_avg;
            statistics::Scalar retry_buffer_req_occupancy_total;
            statistics::Scalar retry_buffer_req_occupancy_samples;
            statistics::Scalar retry_buffer_req_occupancy_max;
            statistics::Formula retry_buffer_req_occupancy_avg;
            statistics::Scalar retry_buffer_resp_occupancy_total;
            statistics::Scalar retry_buffer_resp_occupancy_samples;
            statistics::Scalar retry_buffer_resp_occupancy_max;
            statistics::Formula retry_buffer_resp_occupancy_avg;

            //esj 2025-11-21
            statistics::Scalar stall_req_count;
            statistics::Scalar stall_resp_count;
            statistics::Scalar stall_req_retry_count;
            statistics::Scalar stall_resp_retry_count;
            statistics::Scalar stall_req_cycles;
            statistics::Scalar stall_resp_cycles;
            statistics::Scalar stall_req_retry_cycles;
            statistics::Scalar stall_resp_retry_cycles;

            //esj 2025-11-29
            statistics::Scalar crc_error_count;


            //esj 2025-09-09
            // statistics::Histogram replay_buffer;
            // statistics::Histogram replay_buffer_without_zero;
            // statistics::Vector replay_buffer_size;

        } stats;


        public:
        //esj 2025-02-24
        // void regStats() override;

        ///

        //esj 2025-05-07
        AddrRangeList cxl_ranges;

        PacketPtr replayPacket;
        PacketPtr cxl_packet;
        PacketPtr cxl_packet_device;
        std::deque<DeferredPacket> transmitList;

        // esj 2025-05-16
        std::deque<DeferredPacket> transmitList_write;
        bool read_turn;
        //

        std::deque<DeferredPacket> transmitList_resp;

        //esj 2024-12-15
        std::deque<DeferredPacket> retry_transmitList_req;
        std::deque<DeferredPacket> retry_transmitList_resp;
        cxl_ReplayBuffer retry_buffer_req;
        cxl_ReplayBuffer retry_buffer_resp;
        void retry_transmit_req(PacketPtr pkt, Tick when);
        void retry_transmit_resp(PacketPtr pkt, Tick when);
        bool popfromtransmitlist_req( uint64_t seq_num);
        bool popfromtransmitlist_resp( uint64_t seq_num);
        void print_retry_transmitList_resp();
        int getsize_retry_transmitList_resp();
        int getsize_retry_transmitList_req();
        int get_retry_num_resp();
        int get_retry_num_req();

        //esj 2025-01-17
        // bool check_seqnum_req(uint64_t seq_num);
        // bool check_seqnum_resp(uint64_t seq_num);
        int check_seqnum_resp(Tick when);
        int check_seqnum_req(Tick when);

        //esj 2025-01-18
        void check_retry_req_packet(PacketPtr pkt);
        void check_retry_resp_packet(PacketPtr pkt);

        void print_retry_transmitList_req();

        int retry_transmitList_req_size;
        int retry_transmitList_resp_size;



        //esj 2025-03-02
        // int req_seqnum;
        // int resp_seqnum;
        uint64_t req_seqnum;
        uint64_t resp_seqnum;
        uint64_t req_seqnum_by_host[Packet::MaxPciRequesterIds];
        uint64_t resp_seqnum_by_host[Packet::MaxPciRequesterIds];

        //esj 2025-01-02
        int control_flit_cycle;

        //esj 2025-01-17
        int crc_error_control_flit_cycle;
        uint64_t crc_error_resp_seqnum;
        uint64_t crc_error_req_seqnum;
        std::deque<retrypacket> error_resp_transmitList;
        std::deque<retrypacket> error_req_transmitList;

        //esj 2025-07-21
        uint64_t req_seqnum_second;
        std::deque<retrypacket> error_req_transmitList_second;
        std::deque<retrypacket> error_req_transmitList_by_host[Packet::MaxPciRequesterIds];


        //

        //esj 2025-05-13
        std::deque<DeferredPacket> transmitList_host;
        bool is_host_retry = false;
        //

        PacketPtr replayPacket_host;
        PacketPtr cxl_packet_host;
        bool Host2Device_busy;
        bool Device2Host_busy;

        uint32_t retryTime; //esj 2024-05-25

        bool retryReq ;
        bool retryResp ;

        bool retryReq_host;
        bool retryResp_host;

        cxl_ReplayBuffer cxl_buffer_device;
        cxl_ReplayBuffer cxl_buffer_host;

        // D2H responses continue toward the host after entering the replay
        // buffer. Keep controller-owned copies so replay never dereferences
        // a packet whose downstream receiver has already freed it.
        std::map<uint64_t, std::unique_ptr<Packet>> deviceReplaySnapshots;
        PacketPtr cloneDeviceReplayPacket(PacketPtr pkt) const;
        PacketPtr cloneDeviceReplayPacket(uint64_t seq_num) const;
        bool insertDeviceReplaySnapshot(PacketPtr pkt);
        void releaseDeviceReplaySnapshot(uint64_t seq_num);

        int maxQueueSize;

        //esj 2025-01-29
        int read_queue_size;
        int reserved_read_queue;

        CXL_Local_state LRSM;
        CXL_Remote_state RRSM;

        CXL_Local_state LRSM_device ;
        CXL_Remote_state RRSM_device;

        RXSM recv_state;
        TXSM trans_state;

        RXSM recv_state_device;
        TXSM trans_state_device;

        EventFunctionWrapper TL2Packingevent;
        EventFunctionWrapper PHY2Decodingevent;


        void schedTimingReq(PacketPtr pkt, Tick when);
        void trySendTiming();
        size_t selectRequestTxIndex() const;
        Tick nextRequestTxWakeup() const;
        bool lrsmRequestControlReady() const;
        EventFunctionWrapper sendEvent;

        void trySendTimingResp();
        void schedTimingResp(PacketPtr pkt, Tick when);
        size_t selectResponseTxIndex() const;
        Tick nextResponseTxWakeup() const;
        bool lrsmResponseControlReady() const;
        bool needsDeviceReplaySlot(PacketPtr pkt) const;
        size_t pendingDeviceReplaySlots() const;
        bool deviceReplayBlocksAdmission(PacketPtr pkt);
        EventFunctionWrapper sendEventResp;
        bool sendResponse(PacketPtr pkt);


        //esj 2024-05-13
        uint32_t CXL_io_retryTime;
        uint32_t CXL_mem_retryTime;
        double ticksPerByte;
        int lanes;
        int mps;
        Tick delay;
        Tick delay_var;

        //esj 2025-02-19
        Tick queuing_delay;

        CXL_ctrlResponsePort upstreamResponse, downstreamResponse, internal_Response, pcie_Response;
        CXL_ctrlRequestPort upstreamRequest , downstreamRequest, internal_Resquest, pcie_Resquest;

        //esj 2025-06-22
        CXL_ctrlResponsePort downstreamResponse2,upstreamResponse2;
        CXL_ctrlRequestPort upstreamRequest2,downstreamRequest2;
        //

        Tick CXL_timeout;


        void TL2Packing();
        void PHY2Decoding();

        bool TL2PHY_interface(PacketPtr pkt);
        bool TL2PHY_interface2(PacketPtr pkt);
        bool PHY2TL_interface(PacketPtr pkt);
        bool transmit_cxl(PacketPtr pkt);
        bool transmit_cxl_device(PacketPtr pkt);
        uint64_t packetFlitCount(const PacketPtr pkt) const;
        void sampleEndpointStats();
        void recordReqStall();
        void recordRespStall();
        void recordReqRetryStall();
        void recordRespRetryStall();
        uint8_t normalizeHostIdx(uint8_t source_host_idx) const;
        uint8_t packetHostIdx(const PacketPtr pkt) const;
        uint64_t& reqSeqnumByHost(uint8_t source_host_idx);
        const uint64_t& reqSeqnumByHost(uint8_t source_host_idx) const;
        uint64_t& respSeqnumByHost(uint8_t source_host_idx);
        const uint64_t& respSeqnumByHost(uint8_t source_host_idx) const;
        std::deque<retrypacket>& errorReqListByHost(uint8_t source_host_idx);
        const std::deque<retrypacket>& errorReqListByHost(uint8_t source_host_idx) const;
        // bool transmit_cxl2Host(PacketPtr pkt);

        // bool busy() { return cxl_packet != NULL; }
        bool busyhost() { return Host2Device_busy ; }

        // Add to CXL_ctrl class
        std::map<uint64_t, EventFunctionWrapper*> seqEvents;  // Store events for each seqNum
        void handleSeqTimeout(uint64_t seqNum);  // Sequence timeout handler
        void createSeqEvent(uint64_t seqNum);

        // esj 2025-05-16
        void cancelSeqEvent_double(uint64_t seqNum, uint64_t seqNum2);
        void cancelSeqEvent(uint64_t seqNum);

        std::map<uint64_t, EventFunctionWrapper*> seqEvents_device;  // Store events for each seqNum
        void handleSeqTimeout_device(uint64_t seqNum);  // Sequence timeout handler
        void createSeqEvent_device(uint64_t seqNum);
        void cancelSeqEvent_device(uint64_t seqNum);
        //esj 2025-01-10
        void cancelSeqEvent_device2(uint64_t seqNum);


        CXL_ctrlRequestPort& getRequestPort(const std::string &if_name, PortID idx) ;
        CXL_ctrlResponsePort& getResponsePort(const std::string &if_name, PortID idx) ;

        Port & getPort(const std::string &if_name, PortID idx=InvalidPortID) override;

        CXL_CRC *cxl_crc ;
        CXL_Decoder *cxl_decoder ;
        CXL_Encoder *cxl_encoder ;
        CXL_FlexBus *cxl_flexbus;
        CXL_Packing *cxl_packing ;
        CXL_Unpacking *cxl_unpacking ;
        CXL_Framer *cxl_framer ;
        CXL_Deframer *cxl_deframer ;
        CXL_CRC_check *cxl_crc_checker;

        EventFunctionWrapper timeoutEvent ;
        void timeoutfunc();

        bool retransmit ;
        int retransmitIdx ;

        EventFunctionWrapper txQueueEvent ;
        std::deque<std::pair<Tick , PacketPtr>> txQueue ;
        void txComplete(PacketPtr pkt) ;
        void processTxQueue() ;

        EventFunctionWrapper control_flit_respEvent ;
        void control_flit_respfunc();

        EventFunctionWrapper control_flit_reqEvent ;
        void control_flit_reqfunc();

        bool response_busy;
        bool request_busy;

        bool recvresponse(PacketPtr);
        bool recvrequest(PacketPtr);

        PacketPtr temp_pkt;

        PacketPtr control_flit;
        PacketPtr control_flit_resp;

        PacketPtr make_control_flit_req(PacketPtr pkt);
        PacketPtr make_control_flit_resp(PacketPtr pkt);
        bool popseq;

        //esj 2025-05-08
        bool retry_pkt = false;


        bool is_host;

        uint64_t sendSeqNum ;
        uint64_t recvSeqNum ;

        uint64_t recv_packet_num;

        void init() ;
        using Param = CXL_ctrlParams;
        CXL_ctrl (const Param & p) ;
        ~CXL_ctrl() override;

    } ;
}
