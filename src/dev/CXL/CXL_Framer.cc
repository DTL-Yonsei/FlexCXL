
#include "dev/CXL/CXL_Framer.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Framer.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_Framer::CXL_FramerRequestPort::CXL_FramerRequestPort(const std::string& _name,CXL_Framer* framer)
        : RequestPort(_name, framer), framer(framer)
    {}
    

    CXL_Framer::CXL_FramerResponsePort::CXL_FramerResponsePort(const std::string& _name,CXL_Framer* framer)
        : ResponsePort(_name, framer), framer(framer)
    {}
    
    CXL_Framer :: CXL_Framer(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        FramingEvent([this]{Framing();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit)
        ,Framing_respEvent([this]{Framing_resp();}, p.name),Device2Host_busy(false),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name),logical_PHY_delay(p.logical_PHY_delay)
    {

    }

    // bool 
    // CXL_Framer::CXL_FramerRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += framer->Framing_packet(pkt);

    //     DPRINTF(CXL_Framer, "%s, Framing delay = %d \n",__func__, delay);

    //     if(framer->Device2Host_busy){
    //         DPRINTF(CXL_Framer, "%s, framer is busy\n",__func__);
    //         return false;
    //     }

    //     // framer->Device2Host_busy = true;

    //     // if(!framer->retry){
    //     //     framer->packet = pkt;
    //     // }
    //     if(!framer->Framing_respEvent.scheduled()){
    //         framer->Device2Host_busy = true;
    //         if(!framer->retry){
    //             framer->packet = pkt;
    //         }
    //         framer->schedule(framer->Framing_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }

    //     return true;
    // }

    // bool 
    // CXL_Framer :: CXL_FramerResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += framer->Framing_packet(pkt);

    //     DPRINTF(CXL_Framer, "%s, Framing delay = %d \n",__func__, delay);

    //     if(framer->Host2Device_busy){
    //         DPRINTF(CXL_Framer, "%s, framer is busy\n",__func__);
    //         return false;
    //     }
    //     // if(!framer->retry){
    //     //     framer->packet = pkt;
    //     // }
    //     if(!framer->FramingEvent.scheduled()){
    //         framer->Host2Device_busy = true;
    //         if(!framer->retry){
    //             framer->packet = pkt;
    //         }
    //         framer->schedule(framer->FramingEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;

    // }

    bool 
    CXL_Framer :: CXL_FramerResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(framer->Host2Device_busy || framer->transmitList_host.size() > framer->maxQueueSize){
        //esj 2025-03-02
        // if(framer->transmitList_host.size() >= framer->maxQueueSize -1){
        //esj 2025-06-27
        // if(framer->transmitList_host.size() > framer->maxQueueSize -1){
        //     DPRINTF(CXL_Framer, "%s, framer is busy, transmitList_host size = %d\n",__func__,framer->transmitList_host.size());

        //     //esj 2024-12-07
        //     framer->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += framer->Framing_packet(pkt);

        //esj 2025-02-19
        delay += framer->logical_PHY_delay;

        DPRINTF(CXL_Framer, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09
        // framer->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Framer, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        framer->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Framer::CXL_FramerRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2024-12-09
        // if(framer->Device2Host_busy || framer->transmitList_device.size() > framer->maxQueueSize){
        //esj 2025-03-02
        // if(framer->transmitList_device.size() >= framer->maxQueueSize -1){
        //esj 2025-06-27
        // if(framer->transmitList_device.size() > framer->maxQueueSize -1){
        //     DPRINTF(CXL_Framer, "%s, framer is busy, transmitList_device size = %d\n",__func__,framer->transmitList_device.size());

        //     //esj 2024-12-07
        //     framer->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += framer->Framing_packet(pkt);
        
        //esj 2025-02-19
        delay += framer->logical_PHY_delay;
        
        DPRINTF(CXL_Framer, "%s, packing delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // framer->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Framer, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        framer->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }

    void
    CXL_Framer :: CXL_FramerResponsePort :: recvRespRetry(){
        DPRINTF(CXL_Framer,"%s, retry Framing \n",__func__);
        // framer->schedule(framer->Framing_respEvent,curTick()+2*framer->clockPeriod());

        // framer->retryResp_host = true;
        if(!framer->sendEventResp.scheduled()){
            framer->schedule(framer->sendEventResp,curTick()+framer->clockPeriod());
        }
    }

    void 
    CXL_Framer :: CXL_FramerResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Framer :: CXL_FramerResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Framer,"%s\n",__func__);
        Tick delay = framer->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Framer::Framing(){
        DPRINTF(CXL_Framer,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
        bool success = this->out_port.sendTimingReq(packet);
        Host2Device_busy = false;

        if(success){
            packet = NULL;
            retry = false;
        }
        else{
            retry = true;
            this->out_port.recvReqRetry();//esj 2024-12-07
            // this->in_port.recvRespRetry();
        }
    }

    void
    CXL_Framer::Framing_resp(){
        DPRINTF(CXL_Framer,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
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
    CXL_Framer::Framing_packet(PacketPtr packet){
        Tick delay = 0;
        // DPRINTF(CXLLink,"%s\n",__func__);

        DPRINTF(CXL_Framer,"%s\n",__func__);

        delay =  cycleperflit*this->clockPeriod()*packet->cxl_pkt.flit_num;
        DPRINTF(CXL_Framer,"%s,  delay = %d\n",__func__,delay);


        // DPRINTF(CXLLink,"%s\n",__func__);
        return delay;
    }


    void
    CXL_Framer::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_Framer, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_Framer, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    } 

    void
    CXL_Framer::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Framer, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    void
    CXL_Framer::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Framer, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();

            //esj 2024-12-09
            // Host2Device_busy = false;
            
            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_Framer, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Framer, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Framer,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Framer::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Framer, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();

            //esj 2024-12-09
            // Device2Host_busy = false;
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_Framer, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Framer, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Framer,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }

    void 
    CXL_Framer::init()
    {
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Framer Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Framer::CXL_FramerRequestPort&
    CXL_Framer::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Framer::CXL_FramerResponsePort&
    CXL_Framer::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Framer::getPort(const std::string &if_name, PortID idx)
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
