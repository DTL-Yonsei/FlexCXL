
#include "dev/CXL/CXL_Encoder.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Encoder.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_Encoder::CXL_EncoderRequestPort::CXL_EncoderRequestPort(const std::string& _name,CXL_Encoder* encoder)
        : RequestPort(_name, encoder), encoder(encoder)
    {}
    

    CXL_Encoder::CXL_EncoderResponsePort::CXL_EncoderResponsePort(const std::string& _name,CXL_Encoder* encoder)
        : ResponsePort(_name, encoder), encoder(encoder)
    {}
    
    CXL_Encoder :: CXL_Encoder(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        EncodingEvent([this]{Encoding();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Encoding_respEvent([this]{Encoding_resp();},p.name),Device2Host_busy(false),lanes(p.lanes),ticksPerByte(p.speed),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name)
    {

    }

    // bool 
    // CXL_Encoder::CXL_EncoderRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += encoder->Encoding_packet(pkt);
    //     delay += (Tick)ceil(((double)pkt->cxl_pkt.packing_size*(encoder->ticksPerByte/encoder->lanes))+1.0);

    //     DPRINTF(CXL_Encoder, "%s, Encoding delay = %d \n",__func__, delay);

    //     if(encoder->Device2Host_busy){
    //         DPRINTF(CXL_Encoder, "%s, encoder is busy\n",__func__);
    //         return false;
    //     }

    //     //esj 2024-12-07
    //     // encoder->Device2Host_busy = true;

    //     // if(!encoder->retry){
    //     //     encoder->packet = pkt;
    //     // }
    //     if(!encoder->Encoding_respEvent.scheduled()){
    //         encoder->Device2Host_busy = true; //esj 2024-12-07
    //         if(!encoder->retry){
    //             encoder->packet = pkt;
    //         }
    //         encoder->schedule(encoder->Encoding_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;
    // }

    // bool 
    // CXL_Encoder :: CXL_EncoderResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += encoder->Encoding_packet(pkt);
    //     delay += (Tick)ceil(((double)pkt->cxl_pkt.packing_size*(encoder->ticksPerByte/encoder->lanes))+1.0);

    //     DPRINTF(CXL_Encoder, "%s, Encoding delay = %d \n",__func__, delay);

    //     if(encoder->Host2Device_busy){
    //         DPRINTF(CXL_Encoder, "%s, encoder is busy\n",__func__);
    //         return false;
    //     }
    //     // if(!encoder->retry){
    //     //     encoder->packet = pkt;
    //     // }
    //     if(!encoder->EncodingEvent.scheduled()){
    //         encoder->Host2Device_busy = true; //esj 2024-12-07
    //         if(!encoder->retry){
    //             encoder->packet = pkt;
    //         }
    //         encoder->schedule(encoder->EncodingEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;

    // }

    bool 
    CXL_Encoder :: CXL_EncoderResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(encoder->Host2Device_busy || encoder->transmitList_host.size() > encoder->maxQueueSize){
        //esj 2025-03-02
        // if(encoder->transmitList_host.size() >= encoder->maxQueueSize -1){
        //esj 2025-06-27
        // if(encoder->transmitList_host.size() > encoder->maxQueueSize -1){
        //     DPRINTF(CXL_Encoder, "%s, encoder is busy, transmitList_host size = %d\n",__func__,encoder->transmitList_host.size());

        //     //esj 2024-12-07
        //     encoder->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += encoder->Encoding_packet(pkt);

        // For merged request parent, process child packet encoding as well.
        // Response path is intentionally excluded.
        if (pkt->isRequest() && pkt->cxlMergedParent &&
            (pkt->cxlMergedPkt != nullptr)) {
            Tick child_delay = encoder->Encoding_packet(pkt->cxlMergedPkt);
            delay += child_delay;
            DPRINTF(CXL_Encoder,
                    "[%s] merged req child encoding: parent=0x%x child=0x%x "
                    "child_delay=%d total_delay=%d\n",
                    __func__, pkt->getAddr(), pkt->cxlMergedPkt->getAddr(),
                    child_delay, delay);
        }

        DPRINTF(CXL_Encoder, "%s, packing delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // encoder->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Encoder, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        encoder->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Encoder::CXL_EncoderRequestPort::recvTimingResp(PacketPtr pkt)
    {   
        //esj 2024-12-09
        // if(encoder->Device2Host_busy || encoder->transmitList_device.size() > encoder->maxQueueSize){
        //esj 2025-03-02
        // if(encoder->transmitList_device.size() >= encoder->maxQueueSize -1){
        //esj 2025-06-27
        // if(encoder->transmitList_device.size() > encoder->maxQueueSize -1){
        
        //     DPRINTF(CXL_Encoder, "%s, encoder is busy, transmitList_device size = %d\n",__func__,encoder->transmitList_device.size());

        //     //esj 2024-12-07
        //     encoder->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += encoder->Encoding_packet(pkt);

        DPRINTF(CXL_Encoder, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09
        // encoder->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Encoder, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        encoder->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }

    //esj 2025-07-12
    // void
    // CXL_Encoder :: CXL_EncoderResponsePort :: recvRespRetry(){
    //     DPRINTF(CXL_Encoder,"%s, retry Encoding \n",__func__);
    //     // encoder->schedule(encoder->Encoding_respEvent,curTick()+2*encoder->clockPeriod());

    //     // encoder->retryResp_host = true;
    //     if(!encoder->sendEventResp.scheduled()){
    //         encoder->schedule(encoder->sendEventResp,curTick()+encoder->clockPeriod());
    //     }
    // }

    void 
    CXL_Encoder :: CXL_EncoderResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Encoder :: CXL_EncoderResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Encoder,"%s\n",__func__);
        Tick delay = encoder->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Encoder::Encoding(){
        DPRINTF(CXL_Encoder,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
        bool success = this->out_port.sendTimingReq(packet);
        Host2Device_busy = false;

        if(success){
            packet = NULL;
            retry = false;
        }
        else{
            retry = true;
            // this->in_port.recvRespRetry();
            this->out_port.recvReqRetry();//esj 2024-12-07
        }
    }

    void
    CXL_Encoder::Encoding_resp(){
        DPRINTF(CXL_Encoder,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
        bool success = this->in_port.sendTimingResp(packet);
        Device2Host_busy = false;

        if(success){
            packet = NULL;
            retry = false;
        }
        else{
            retry = true;
            this->in_port.recvRespRetry();
        }
    }

    Tick
    CXL_Encoder::Encoding_packet(PacketPtr packet){
        Tick delay = 0;
        // DPRINTF(CXLLink,"%s\n",__func__);
        if(!packet->cxl_pkt.is_encoded){
            //esj 2025-07-27
            // packet->cxl_pkt.packing_size += 2;
            packet->cxl_pkt.packing_size = packet->cxl_pkt.packing_size * 128 / 130;
            //esj 2025-06-14
            // if(packet->cxl_pkt.packing_size - packet->getSize() > 0){
            //     packet->cxl_pkt.packing_size -= packet->getSize();
            // }

            delay = this->clockPeriod() * cycleperflit; 
            packet->cxl_pkt.is_encoded = true;

            //esj 2025-07-28
            // delay += (Tick)ceil(((double)packet->cxl_pkt.packing_size*(ticksPerByte/lanes))+1.0);

            DPRINTF(CXL_Encoder,"[%s], packing_size = %d, delay = %d\n",__func__,packet->cxl_pkt.packing_size,delay);
        }
        
        return delay;
    }

    void
    CXL_Encoder::schedTimingReq(PacketPtr pkt, Tick when)
    {
        DPRINTF(CXL_Encoder, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
        scheduleRequestSend();
    } 

    void
    CXL_Encoder::scheduleRequestSend()
    {
        if (retryReq_host || transmitList_host.empty())
            return;

        Tick when = transmitList_host.front().tick;
        for (const auto &entry : transmitList_host) {
            if (entry.pkt->cxl_pkt.is_controlflit)
                when = std::min(when, entry.tick);
        }
        when = std::max(when, clockEdge());
        if (!sendEvent.scheduled())
            schedule(sendEvent, when);
        else if (when < sendEvent.when())
            reschedule(sendEvent, when);
    }

    void
    CXL_Encoder::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Encoder, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    void
    CXL_Encoder::trySendTiming()
    {
        assert(!transmitList_host.empty());
        assert(!retryReq_host);

        auto selected = transmitList_host.begin();
        for (auto it = transmitList_host.begin();
             it != transmitList_host.end(); ++it) {
            if (it->pkt->cxl_pkt.is_controlflit && it->tick <= curTick()) {
                selected = it;
                break;
            }
        }
        if (selected->tick > curTick()) {
            scheduleRequestSend();
            return;
        }

        PacketPtr pkt = selected->pkt;

        // A capacity query is not a failed send: no timing-port retry is
        // outstanding, so a newly ready ACK/NAK may win the next arbitration.
        // Poll only while queued work is blocked, at the existing link clock.
        if (requestAdmission && !requestAdmission->canAcceptCxlRequest(pkt)) {
            DPRINTF(CXL_Encoder,
                    "Admission wait seq=%llu control=%d; no port retry\n",
                    pkt->cxl_pkt.seqNum, pkt->cxl_pkt.is_controlflit);
            schedule(sendEvent, clockEdge(Cycles(1)));
            return;
        }
    
        DPRINTF(CXL_Encoder, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.erase(selected);

            //esj 2024-12-09
            // Host2Device_busy = false;
            
            scheduleRequestSend();
    
        }
        else {
            // An actual failed send still requires recvReqRetry. Do not send
            // another packet (including control) before that notification.
            retryReq_host = true;
        }
        // esj 2025-05-16
        // else{
        //     DPRINTF(CXL_Encoder, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEvent, curTick()+this->clockPeriod());
        // }
        // esj 2025-07-09
        // else{
        //     transmitList_host.pop_front();
        // }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Encoder,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Encoder::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Encoder, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();

            //esj 2024-12-09
            // Device2Host_busy = false;
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_Encoder, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        // esj 2025-05-16
        // else{
        //     DPRINTF(CXL_Encoder, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEventResp, curTick()+this->clockPeriod());
        // }
        //esj 2025-07-12
        // else{
        //     transmitList_device.pop_front();
        // }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Encoder,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }


    void 
    CXL_Encoder::init()
    {
        if (out_port.isConnected()) {
            requestAdmission = dynamic_cast<CXLRequestAdmission *>(
                &out_port.getPeer());
        }
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Encoder Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Encoder::CXL_EncoderRequestPort&
    CXL_Encoder::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Encoder::CXL_EncoderResponsePort&
    CXL_Encoder::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Encoder::getPort(const std::string &if_name, PortID idx)
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
