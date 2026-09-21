
#include "dev/CXL/CXL_Decoder.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Decoder.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_Decoder::CXL_DecoderRequestPort::CXL_DecoderRequestPort(const std::string& _name,CXL_Decoder* decoder)
        : RequestPort(_name, decoder), decoder(decoder)
    {}
    

    CXL_Decoder::CXL_DecoderResponsePort::CXL_DecoderResponsePort(const std::string& _name,CXL_Decoder* decoder)
        : ResponsePort(_name, decoder), decoder(decoder)
    {}
    
    CXL_Decoder :: CXL_Decoder(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        DecodingEvent([this]{Decoding();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Host2Device_busy(false),Decoding_respEvent([this]{Decoding_resp();} , p.name),Device2Host_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name)
    {

    }

    // bool 
    // CXL_Decoder::CXL_DecoderRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += decoder->Decoding_packet(pkt);

    //     DPRINTF(CXL_Decoder, "%s, Framing delay = %d \n",__func__, delay);

    //     if(decoder->Device2Host_busy){
    //         DPRINTF(CXL_Decoder, "%s, decoder is busy\n",__func__);
    //         return false;
    //     }

    //     // decoder->Device2Host_busy = true;

    //     // if(!decoder->retry){
    //     //     decoder->packet = pkt;
    //     // }
    //     if(!decoder->Decoding_respEvent.scheduled()){
    //         decoder->Device2Host_busy = true;
    //         if(!decoder->retry){
    //             decoder->packet = pkt;
    //         }
    //         decoder->schedule(decoder->Decoding_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;
    // }

    // bool 
    // CXL_Decoder :: CXL_DecoderResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += decoder->Decoding_packet(pkt);

    //     DPRINTF(CXL_Decoder, "%s, Framing delay = %d \n",__func__, delay);

    //     if(decoder->Host2Device_busy){
    //         DPRINTF(CXL_Decoder, "%s, decoder is busy\n",__func__);
    //         return false;
    //     }

    //     // decoder->Host2Device_busy = true;

    //     // if(!decoder->retry){
    //     //     decoder->packet = pkt;
    //     // }
    //     if(!decoder->DecodingEvent.scheduled()){
    //         decoder->Host2Device_busy = true;
    //         if(!decoder->retry){
    //             decoder->packet = pkt;
    //         }
    //         decoder->schedule(decoder->DecodingEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;

    // }

    bool 
    CXL_Decoder :: CXL_DecoderResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(decoder->Host2Device_busy || decoder->transmitList_host.size() > decoder->maxQueueSize){
        //esj 2025-03-02
        // if(decoder->transmitList_host.size() >= decoder->maxQueueSize -1){

        // esj 2025-05-19
        // if(decoder->transmitList_host.size() > decoder->maxQueueSize -1){
        //     DPRINTF(CXL_Decoder, "%s, decoder is busy, transmitList_host size = %d\n",__func__,decoder->transmitList_host.size());

        //     //esj 2024-12-07
        //     decoder->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += decoder->Decoding_packet(pkt);

        DPRINTF(CXL_Decoder, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09
        // decoder->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Decoder, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        decoder->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Decoder::CXL_DecoderRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2024-12-09
        // if(decoder->Device2Host_busy || decoder->transmitList_device.size() > decoder->maxQueueSize){
        //esj 2025-03-02
        // if(decoder->transmitList_device.size() >= decoder->maxQueueSize -1){

        // esj 2025-05-19
        // if(decoder->transmitList_device.size() > decoder->maxQueueSize -1){
        //     DPRINTF(CXL_Decoder, "%s, decoder is busy, transmitList_device size = %d\n",__func__,decoder->transmitList_device.size());

        //     //esj 2024-12-07
        //     decoder->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += decoder->Decoding_packet(pkt);

        DPRINTF(CXL_Decoder, "%s, packing delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // decoder->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Decoder, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        decoder->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }

    void
    CXL_Decoder :: CXL_DecoderResponsePort :: recvRespRetry(){
        DPRINTF(CXL_Decoder,"%s, retry Framing \n",__func__);
        // decoder->schedule(decoder->Decoding_respEvent,curTick()+2*decoder->clockPeriod());

        // decoder->retryResp_host = true;
        if(!decoder->sendEventResp.scheduled()){
            decoder->schedule(decoder->sendEventResp,curTick()+decoder->clockPeriod());
        }
    }

    void 
    CXL_Decoder :: CXL_DecoderResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Decoder :: CXL_DecoderResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Decoder,"%s\n",__func__);
        Tick delay = decoder->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Decoder::Decoding(){
        DPRINTF(CXL_Decoder,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
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
    CXL_Decoder::Decoding_resp(){
        DPRINTF(CXL_Decoder,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
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
    CXL_Decoder::Decoding_packet(PacketPtr packet){
        Tick delay = 0;
        // DPRINTF(CXLLink,"%s\n",__func__);
        if(packet->cxl_pkt.is_encoded){
            packet->cxl_pkt.packing_size -= 2;
            delay =  this->clockPeriod() * cycleperflit; 
            DPRINTF(CXL_Decoder,"%s, packing_size = %d, delay = %d\n",__func__,packet->cxl_pkt.packing_size,delay);
        }
        
        return delay;
    }
    
void
    CXL_Decoder::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_Decoder, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_Decoder, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-12-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    } 

    void
    CXL_Decoder::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Decoder, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    void
    CXL_Decoder::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Decoder, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();

            //esj 2024-12-09
            // Host2Device_busy = false;
            
            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_Decoder, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Decoder, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Decoder,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Decoder::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Decoder, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();

            //esj 2024-12-09
            // Device2Host_busy = false;
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_Decoder, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Decoder, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Decoder,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }

    void 
    CXL_Decoder::init()
    {
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Decoder Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Decoder::CXL_DecoderRequestPort&
    CXL_Decoder::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Decoder::CXL_DecoderResponsePort&
    CXL_Decoder::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Decoder::getPort(const std::string &if_name, PortID idx)
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
