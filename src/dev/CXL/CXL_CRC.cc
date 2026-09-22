
#include "dev/CXL/CXL_CRC.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_CRC.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"
#include "sim/protocol_validation_logger.hh"

namespace gem5
{
    float random_success() {
        //esj 2025-11-21
        static std::mt19937 rng(std::time(nullptr));
        std::uniform_int_distribution<int> dist(0, 9999999); //esj 2025-11-21 999 -> 9999999
        return ((float)dist(rng)/10000000.0f); //esj 2025-11-21 1000 -> 10000000.0f
    }

    //esj 2025-11-21
    double random_success_double() {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng);
    }

    CXL_CRC::CRCStats::CRCStats(CXL_CRC &crc)
        : statistics::Group(&crc),
          ADD_STAT(iidEligiblePackets,
                   "Validation seeded-IID eligible data packets"),
          ADD_STAT(iidInjectedErrors,
                   "Validation seeded-IID errors applied"),
          ADD_STAT(iidEligibleRetryPackets,
                   "Eligible retransmitted packets in seeded-IID mode"),
          ADD_STAT(iidInjectedRetryErrors,
                   "Seeded-IID errors applied to retransmitted packets"),
          ADD_STAT(iidSkippedControlPackets,
                   "Control packets excluded from seeded-IID injection"),
          ADD_STAT(iidSkippedWriteResponsePackets,
                   "CXL write responses excluded from seeded-IID injection"),
          ADD_STAT(iidSkippedMetadataPackets,
                   "Packets carrying prior CRC-error metadata excluded from seeded-IID injection")
    {}

    CXL_CRC::CXL_CRCRequestPort::CXL_CRCRequestPort(const std::string& _name,CXL_CRC* crc)
        : RequestPort(_name, crc), crc(crc)
    {}


    CXL_CRC::CXL_CRCResponsePort::CXL_CRCResponsePort(const std::string& _name,CXL_CRC* crc)
        : ResponsePort(_name, crc), crc(crc)
    {}

    CXL_CRC :: CXL_CRC(const Param &p)
        : ClockedObject(p),
        in_port(p.name +".in_port", this),
        out_port(p.name +".out_port", this),
        retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Device2Host_busy(false),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name),
        error_rate(p.error_rate),
        validationSeededIidEnable(p.validation_seeded_iid_enable),
        validationIidSeed(p.validation_iid_seed),
        validationIidRetryEligible(p.validation_iid_retry_eligible),
        validationIidRng(validationIidSeed),
        validationIidDist(0, 9999999),
        stats(*this)

    {

    }
    bool
    CXL_CRC :: CXL_CRCResponsePort :: recvTimingReq(PacketPtr pkt)
    {
        //esj 2025-03-02
        // if(crc->transmitList_host.size() >= crc->maxQueueSize - 1){
        //esj 2025-06-27
        // if(crc->transmitList_host.size() > crc->maxQueueSize - 1){
        //     DPRINTF(CXL_CRC, "%s, crc is busy, transmitList_host size = %d\n",__func__,crc->transmitList_host.size());

        //     return false;
        // }

        Tick delay = 0;

        delay += crc->CRC_packet(pkt);

        DPRINTF(CXL_CRC, "%s, CRC delay = %d \n",__func__, delay);


        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_CRC, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        crc->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool
    CXL_CRC::CXL_CRCRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2025-03-02
        // if(crc->transmitList_device.size() >= crc->maxQueueSize -1){
        //esj 2025-06-27
        // if(crc->transmitList_device.size() > crc->maxQueueSize -1){
        //     DPRINTF(CXL_CRC, "%s, crc is busy, transmitList_device size = %d\n",__func__,crc->transmitList_device.size());

        //     return false;
        // }

        Tick delay = 0;

        delay += crc->CRC_packet(pkt);

        DPRINTF(CXL_CRC, "%s, CRC delay = %d \n",__func__, delay);

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_CRC, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        crc->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }


    void
    CXL_CRC :: CXL_CRCResponsePort :: recvRespRetry(){
        DPRINTF(CXL_CRC,"%s, retry CRC \n",__func__);

        if(!crc->sendEventResp.scheduled()){
            crc->schedule(crc->sendEventResp,curTick()+crc->clockPeriod());
        }
    }

    void
    CXL_CRC :: CXL_CRCResponsePort :: recvFunctional(PacketPtr pkt)
    {
        recvTimingReq(pkt);
    }

    Tick
    CXL_CRC :: CXL_CRCResponsePort :: recvAtomic(PacketPtr pkt)
    {
        DPRINTF(CXL_CRC,"%s\n",__func__);
        Tick delay = crc->out_port.sendAtomic(pkt);
        return delay;
    }



    void
    CXL_CRC::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_CRC, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_CRC, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    }

    void
    CXL_CRC::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_CRC, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    Tick
    CXL_CRC::CRC_packet(PacketPtr pkt){
        DPRINTF(CXL_CRC,"%s, pkt addr = %0x, packed = %d, flit num = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.is_packed, pkt->cxl_pkt.flit_num);

        // pkt->cxl_pkt.crc_check = random_success(error_rate);
        pkt->cxl_pkt.crc_check = true;

        //esj 2025-03-13
        //esj 2025-11-21
        pkt->cxl_pkt.crc_data =
            validationSeededIidEnable ? 1.0 : random_success();
        // pkt->cxl_pkt.crc_data_double = random_success_double();

        const std::string direction =
            pkt->cxl_pkt.is_from_device ? "D2H" : "H2D";
        const int64_t attempt =
            (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp ||
             pkt->cxl_pkt.is_retry_req || pkt->cxl_pkt.is_retry_resp) ? 1 : 0;
        const int64_t sequence = pkt->cxl_pkt.seqNum;

        if (validationSeededIidEnable) {
            const bool is_retry = attempt > 0;
            const bool has_error_metadata =
                pkt->cxl_pkt.crc_error_req_seqnum != -1 ||
                pkt->cxl_pkt.crc_error_resp_seqnum != -1;
            bool eligible = true;

            if (pkt->cxl_pkt.is_controlflit) {
                ++stats.iidSkippedControlPackets;
                eligible = false;
            }
            if (pkt->is_cxl_write_resp) {
                ++stats.iidSkippedWriteResponsePackets;
                eligible = false;
            }
            if (has_error_metadata) {
                ++stats.iidSkippedMetadataPackets;
                eligible = false;
            }
            if (is_retry && !validationIidRetryEligible)
                eligible = false;

            // In seeded mode the receiver-side threshold is configured to
            // zero.  Corruption is applied here so the exact eligible
            // denominator and the RNG stream are both explicit.
            pkt->cxl_pkt.crc_data = 1.0;
            if (eligible) {
                ++stats.iidEligiblePackets;
                if (is_retry)
                    ++stats.iidEligibleRetryPackets;

                const double draw =
                    static_cast<double>(validationIidDist(validationIidRng)) /
                    10000000.0;
                if (draw < error_rate) {
                    pkt->cxl_pkt.crc_check = false;
                    ++stats.iidInjectedErrors;
                    if (is_retry)
                        ++stats.iidInjectedRetryErrors;

                    ProtocolValidationEvent applied;
                    applied.event = "ERROR_APPLIED";
                    applied.component = name();
                    applied.layer = "CXL_PROTOCOL";
                    applied.direction = direction;
                    applied.packetSeq = sequence;
                    applied.flitIndex = 0;
                    applied.txAttempt = attempt;
                    applied.reason = "SEEDED_PACKET_IID_CRC";
                    ProtocolValidationLogger::recordPacket(applied, pkt);
                }
            }
        }

        if (ProtocolValidationLogger::corruptionPlanned(
                direction, sequence, attempt)) {
            ProtocolValidationEvent planned;
            planned.event = "ERROR_PLANNED";
            planned.component = name();
            planned.layer = "CXL_PROTOCOL";
            planned.direction = direction;
            planned.packetSeq = sequence;
            planned.flitIndex = 0;
            planned.txAttempt = attempt;
            planned.reason = "DETERMINISTIC_ONE_SHOT_CRC";
            ProtocolValidationLogger::recordPacket(planned, pkt);
        }

        if (ProtocolValidationLogger::takeCorruption(
                direction, sequence, attempt)) {
            pkt->cxl_pkt.crc_check = false;
            ProtocolValidationEvent applied;
            applied.event = "ERROR_APPLIED";
            applied.component = name();
            applied.layer = "CXL_PROTOCOL";
            applied.direction = direction;
            applied.packetSeq = sequence;
            applied.flitIndex = 0;
            applied.txAttempt = attempt;
            applied.reason = "DETERMINISTIC_ONE_SHOT_CRC";
            ProtocolValidationLogger::recordPacket(applied, pkt);
        }

        // for(int i = 0; i < pkt->cxl_pkt.flit_num; i++){
        //     if(random_success() > error_rate){
        //         pkt->cxl_pkt.crc_data ++;
        //     }
        // }

        DPRINTF(CXL_CRC,"%s, pkt->cxl_pkt.crc_data = %lf\n",__func__,pkt->cxl_pkt.crc_data);

        //



        //esj 2025-01-01
        //esj 2025-03-13
        pkt->cxl_pkt.packing_size += 2; //crc add
        // pkt->cxl_pkt.packing_size += 2 * pkt->cxl_pkt.flit_num;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;

        receive_delay += cycleperflit*pkt->cxl_pkt.flit_num*this->clockPeriod();

        return receive_delay;
    }

    void
    CXL_CRC::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        DPRINTF(CXL_CRC, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();


            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_CRC, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }

        }
        else{
            DPRINTF(CXL_CRC, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

    }

    void
    CXL_CRC::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        DPRINTF(CXL_CRC, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();


            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_CRC, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }

        }
        else{
            DPRINTF(CXL_CRC, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }
    }


    void
    CXL_CRC::init()
    {
    }

    CXL_CRC::CXL_CRCRequestPort&
    CXL_CRC::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_CRC::CXL_CRCResponsePort&
    CXL_CRC::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_CRC::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "out_port") {
            return out_port;
        }
        else if (if_name == "in_port") {
            return in_port ;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }

}
