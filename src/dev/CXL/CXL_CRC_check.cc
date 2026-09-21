
#include "dev/CXL/CXL_CRC_check.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_CRC_check.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_CRC_check::CXL_CRC_checkRequestPort::CXL_CRC_checkRequestPort(const std::string& _name,CXL_CRC_check* crc_checker)
        : RequestPort(_name, crc_checker), crc_checker(crc_checker)
    {}


    CXL_CRC_check::CXL_CRC_checkResponsePort::CXL_CRC_checkResponsePort(const std::string& _name,CXL_CRC_check* crc_checker)
        : ResponsePort(_name, crc_checker), crc_checker(crc_checker)
    {}

    CXL_CRC_check :: CXL_CRC_check(const Param &p)
        : ClockedObject(p),
        in_port(p.name +".in_port", this),
        out_port(p.name +".out_port", this),
        retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Device2Host_busy(false),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name),
        success_rate(p.success_rate)
    {

    }
    bool
    CXL_CRC_check :: CXL_CRC_checkResponsePort :: recvTimingReq(PacketPtr pkt)
    {
        //esj 2025-03-02
        // if(crc_checker->transmitList_host.size() >= crc_checker->maxQueueSize -1){
        //esj 2025-06-27
        // if(crc_checker->transmitList_host.size() > crc_checker->maxQueueSize -1){
        //     DPRINTF(CXL_CRC_check, "%s, crc_checker is busy, transmitList_host size = %d\n",__func__,crc_checker->transmitList_host.size());

        //     return false;
        // }

        Tick delay = 0;

        delay += crc_checker->CRC_packet_check(pkt);

        DPRINTF(CXL_CRC_check, "%s, CRC delay = %d \n",__func__, delay);


        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_CRC_check, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        crc_checker->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool
    CXL_CRC_check::CXL_CRC_checkRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2025-03-02
        // if(crc_checker->transmitList_device.size() >= crc_checker->maxQueueSize -1){
        //esj 2025-06-27
        // if(crc_checker->transmitList_device.size() > crc_checker->maxQueueSize -1){
        //     DPRINTF(CXL_CRC_check, "%s, crc_checker is busy, transmitList_device size = %d\n",__func__,crc_checker->transmitList_device.size());

        //     return false;
        // }

        Tick delay = 0;

        delay += crc_checker->CRC_packet_check(pkt);

        DPRINTF(CXL_CRC_check, "%s, CRC delay = %d \n",__func__, delay);

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_CRC_check, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        crc_checker->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }


    void
    CXL_CRC_check :: CXL_CRC_checkResponsePort :: recvRespRetry(){
        DPRINTF(CXL_CRC_check,"%s, retry CRC \n",__func__);

        if(!crc_checker->sendEventResp.scheduled()){
            crc_checker->schedule(crc_checker->sendEventResp,curTick()+crc_checker->clockPeriod());
        }
    }

    void
    CXL_CRC_check :: CXL_CRC_checkResponsePort :: recvFunctional(PacketPtr pkt)
    {
        recvTimingReq(pkt);
    }

    Tick
    CXL_CRC_check :: CXL_CRC_checkResponsePort :: recvAtomic(PacketPtr pkt)
    {
        DPRINTF(CXL_CRC_check,"%s\n",__func__);
        Tick delay = crc_checker->out_port.sendAtomic(pkt);
        return delay;
    }



    void
    CXL_CRC_check::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_CRC_check, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_CRC_check, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-12-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    }

    void
    CXL_CRC_check::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_CRC_check, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    Tick
    CXL_CRC_check::CRC_packet_check(PacketPtr pkt){
        DPRINTF(CXL_CRC_check,
                "%s, pkt addr = %0x, packed = %d\n",
                __func__, pkt->getAddr(), pkt->cxl_pkt.is_packed);

        Tick delay = 0;

        //esj 2025-12-29
        delay += cycleperflit*pkt->cxl_pkt.flit_num*this->clockPeriod();

        if(pkt->cxl_pkt.crc_check){
            //esj 2025-12-29
            // delay += cycleperflit*pkt->cxl_pkt.flit_num*this->clockPeriod();
            DPRINTF(CXL_CRC_check,
                    "%s, crc_data = %f, success_rate = %f\n",
                    __func__, pkt->cxl_pkt.crc_data, success_rate);
            //esj 2025-05-20
            // if (pkt->cxl_pkt.crc_data >= success_rate |
            //     pkt->cxl_pkt.is_controlflit
            //esj 2025-07-18 add | -> ||
            if (pkt->cxl_pkt.crc_data >= success_rate ||
                pkt->cxl_pkt.is_controlflit || pkt->is_cxl_write_resp ||
                //esj 2025-01-18 add crc_error
                pkt->cxl_pkt.crc_error_req_seqnum != -1 ||
                pkt->cxl_pkt.crc_error_resp_seqnum != -1) {

                pkt->cxl_pkt.crc_check = true;
                DPRINTF(CXL_CRC_check, "%s, CRC check success, delay = %d\n",__func__, delay);
            }
            else{
                pkt->cxl_pkt.crc_check = false;
                // //esj 2025-01-11
                // if(pkt->isRequest()){
                //     pkt->cxl_pkt.retry_req = true;
                // }
                // else{
                //     pkt->cxl_pkt.retry_resp = true;
                // }
                // //

                DPRINTF(CXL_CRC_check, "%s, CRC check failed, delay = %d\n",__func__, delay);
            }
        }
        else{
            DPRINTF(CXL_CRC_check, "%s, CRC check failed, delay = %d\n",__func__, delay);
        }

        //esj 2025-01-01
        //esj 2025-03-13
        pkt->cxl_pkt.packing_size -= 2; //crc delete
        // pkt->cxl_pkt.packing_size -= 2*pkt->cxl_pkt.flit_num;


        return delay;
    }

    void
    CXL_CRC_check::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        DPRINTF(CXL_CRC_check, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();


            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_CRC_check, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }

        }
        else{
            DPRINTF(CXL_CRC_check, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

    }

    void
    CXL_CRC_check::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        DPRINTF(CXL_CRC_check, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();


            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_CRC_check, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }

        }
        else{
            DPRINTF(CXL_CRC_check, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }
    }


    void
    CXL_CRC_check::init()
    {
    }

    CXL_CRC_check::CXL_CRC_checkRequestPort&
    CXL_CRC_check::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_CRC_check::CXL_CRC_checkResponsePort&
    CXL_CRC_check::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_CRC_check::getPort(const std::string &if_name, PortID idx)
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
