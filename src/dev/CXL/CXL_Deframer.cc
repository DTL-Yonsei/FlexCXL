
#include "dev/CXL/CXL_Deframer.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Deframer.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_Deframer::CXL_DeframerRequestPort::CXL_DeframerRequestPort(const std::string& _name,CXL_Deframer* deframer)
        : RequestPort(_name, deframer), deframer(deframer)
    {}
    

    CXL_Deframer::CXL_DeframerResponsePort::CXL_DeframerResponsePort(const std::string& _name,CXL_Deframer* deframer)
        : ResponsePort(_name, deframer), deframer(deframer)
    {}
    
    CXL_Deframer :: CXL_Deframer(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        DeframingEvent([this]{Deframing();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit),Host2Device_busy(false),
        Deframing_respEvent([this]{Deframing_resp();} , p.name),Device2Host_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name),logical_PHY_delay(p.logical_PHY_delay)
    {

    }

    // bool 
    // CXL_Deframer::CXL_DeframerRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += pkt->cxl_pkt.flit_num * deframer->cycleperflit;

    //     DPRINTF(CXL_Deframer, "%s, Deframing delay = %d \n",__func__, delay);

    //     if(deframer->Device2Host_busy){
    //         DPRINTF(CXL_Deframer, "%s, deframer is busy\n",__func__);
    //         return false;
    //     }

    //     // deframer->Host2Device_busy = true;

    //     // if(!deframer->retry){
    //     //     deframer->packet = pkt;
    //     // }   
    //     if(!deframer->Deframing_respEvent.scheduled()){
    //         deframer->Device2Host_busy = true;
    //         if(!deframer->retry){
    //             deframer->packet = pkt;
    //         }
    //         deframer->schedule(deframer->Deframing_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;
    // }

    // bool 
    // CXL_Deframer :: CXL_DeframerResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += pkt->cxl_pkt.flit_num * deframer->cycleperflit;

    //     DPRINTF(CXL_Deframer, "%s, Deframing delay = %d \n",__func__, delay);

    //     if(deframer->Host2Device_busy){
    //         DPRINTF(CXL_Deframer, "%s, deframer is busy\n",__func__);
    //         return false;
    //     }

    //     deframer->Host2Device_busy = true;

    //     // if(!deframer->retry){
    //     //     deframer->packet = pkt;
    //     // }
    //     if(!deframer->DeframingEvent.scheduled()){  
    //         deframer->Host2Device_busy = true; //esj 2024-12-07
    //         if(!deframer->retry){
    //             deframer->packet = pkt;
    //         }
    //         deframer->schedule(deframer->DeframingEvent,curTick()+delay);
    //     }   
    //     else{
    //         return false;
    //     }
        
    //     return true;

    // }

bool 
    CXL_Deframer :: CXL_DeframerResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(deframer->Host2Device_busy || deframer->transmitList_host.size() > deframer->maxQueueSize){
        //esj 2025-03-02
        // if(deframer->transmitList_host.size() >= deframer->maxQueueSize -1){
        //esj 2025-06-27
        // if(deframer->transmitList_host.size() > deframer->maxQueueSize -1){
        //     DPRINTF(CXL_Deframer, "%s, deframer is busy, transmitList_host size = %d\n",__func__,deframer->transmitList_host.size());

        //     //esj 2024-12-07
        //     deframer->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += deframer->Deframing_packet(pkt);

        //esj 2025-02-19
        delay += deframer->logical_PHY_delay;

        DPRINTF(CXL_Deframer, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09    
        // deframer->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Deframer, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        deframer->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Deframer::CXL_DeframerRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2024-12-09
        // if(deframer->Device2Host_busy || deframer->transmitList_device.size() > deframer->maxQueueSize){
        //esj 2025-03-02
        // if(deframer->transmitList_device.size() >= deframer->maxQueueSize -1){
        
        //esj 2025-06-27
        // if(deframer->transmitList_device.size() > deframer->maxQueueSize -1){
        //     DPRINTF(CXL_Deframer, "%s, deframer is busy, transmitList_device size = %d\n",__func__,deframer->transmitList_device.size());

        //     //esj 2024-12-07
        //     deframer->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += deframer->Deframing_packet(pkt);

        //esj 2025-02-19
        delay += deframer->logical_PHY_delay;

        DPRINTF(CXL_Deframer, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09
        // deframer->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Deframer, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        deframer->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }

    void
    CXL_Deframer :: CXL_DeframerResponsePort :: recvRespRetry(){
        DPRINTF(CXL_Deframer,"%s, retry Deframing \n",__func__);
        // deframer->schedule(deframer->Deframing_respEvent,curTick()+2*deframer->clockPeriod());

        // deframer->retryResp_host = true;
        if(!deframer->sendEventResp.scheduled()){
            deframer->schedule(deframer->sendEventResp,curTick()+deframer->clockPeriod());
        }
    }

    void 
    CXL_Deframer :: CXL_DeframerResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Deframer :: CXL_DeframerResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Deframer,"%s\n",__func__);
        Tick delay = deframer->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Deframer::Deframing(){
        DPRINTF(CXL_Deframer,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
        bool success = this->out_port.sendTimingReq(packet);

        if(success){
            packet = NULL;
            retry = false;
            Host2Device_busy = false;
        }
        else{
            retry = true;
            // this->in_port.recvRespRetry();
            this->out_port.recvReqRetry();//esj 2024-12-07
        }
    }

    void
    CXL_Deframer::Deframing_resp(){
        DPRINTF(CXL_Deframer,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
        bool success = this->in_port.sendTimingResp(packet);

        if(success){
            packet = NULL;
            retry = false;
            Device2Host_busy = false;
        }
        else{
            retry = true;
            this->in_port.recvRespRetry();
        }
    }

    Tick
    CXL_Deframer::Deframing_packet(PacketPtr pkt){
        Tick delay = 0;
        DPRINTF(CXL_Deframer,"%s\n",__func__);

        delay =  cycleperflit*this->clockPeriod()*pkt->cxl_pkt.flit_num;
        DPRINTF(CXL_Deframer,"%s,  delay = %d\n",__func__,delay);

        return delay;
    }

    void
    CXL_Deframer::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_Deframer, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_Deframer, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    } 

    void
    CXL_Deframer::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Deframer, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    void
    CXL_Deframer::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Deframer, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();

            //esj 2024-12-09
            // Host2Device_busy = false;
            
            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_Deframer, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Deframer, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Deframer,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Deframer::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Deframer, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();

            //esj 2024-12-09
            // Device2Host_busy = false;
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_Deframer, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_Deframer, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Deframer,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }

    void 
    CXL_Deframer::init()
    {
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Deframer Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Deframer::CXL_DeframerRequestPort&
    CXL_Deframer::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Deframer::CXL_DeframerResponsePort&
    CXL_Deframer::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Deframer::getPort(const std::string &if_name, PortID idx)
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
