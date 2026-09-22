
#include "dev/CXL/CXL_Unpacking.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Unpacking.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_Unpacking::CXL_UnpackingRequestPort::CXL_UnpackingRequestPort(const std::string& _name,CXL_Unpacking* unpacker)
        : RequestPort(_name, unpacker), unpacker(unpacker)
    {}
    

    CXL_Unpacking::CXL_UnpackingResponsePort::CXL_UnpackingResponsePort(const std::string& _name,CXL_Unpacking* unpacker)
        : ResponsePort(_name, unpacker), unpacker(unpacker)
    {}
    
    CXL_Unpacking :: CXL_Unpacking(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        UnpackingEvent([this]{Unpacking();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Host2Device_busy(false), Unpacking_respEvent([this]{Unpacking_resp();} , p.name),Device2Host_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name)
    {

    }

    // bool 
    // CXL_Unpacking::CXL_UnpackingRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += unpacker->UnPacking_packet(pkt);

    //     DPRINTF(CXL_Unpacking, "%s, Unpacking delay = %d \n",__func__, delay);

    //     if(unpacker->Device2Host_busy){
    //         DPRINTF(CXL_Unpacking, "%s, unpacker is busy\n",__func__);
    //         return false;
    //     }

    //     // unpacker->Device2Host_busy = true;

    //     // if(!unpacker->retry){
    //     //     unpacker->packet = pkt;
    //     // }
    //     if(!unpacker->Unpacking_respEvent.scheduled()){
    //         unpacker->Device2Host_busy = true;
    //         if(!unpacker->retry){
    //             unpacker->packet = pkt;
    //         }
    //         unpacker->schedule(unpacker->Unpacking_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;
    // }

    // bool 
    // CXL_Unpacking :: CXL_UnpackingResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += unpacker->UnPacking_packet(pkt);

    //     DPRINTF(CXL_Unpacking, "%s, Unpacking delay = %d \n",__func__, delay);

    //     if(unpacker->Host2Device_busy){
    //         DPRINTF(CXL_Unpacking, "%s, unpacker is busy\n",__func__);
    //         return false;
    //     }

    //     unpacker->Host2Device_busy = true;

    //     // if(!unpacker->retry){
    //     //     unpacker->packet = pkt;
    //     // }
    //     if(!unpacker->UnpackingEvent.scheduled()){
    //         if(!unpacker->retry){
    //             unpacker->packet = pkt;
    //         }
    //         unpacker->schedule(unpacker->UnpackingEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;

    // }

    bool 
    CXL_Unpacking :: CXL_UnpackingResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2025-02-27
        // if(unpacker->Host2Device_busy || unpacker->transmitList_host.size() >= unpacker->maxQueueSize -1){
        //esj 2025-03-02
        // if(unpacker->transmitList_host.size() >= unpacker->maxQueueSize -1){
        
        //esj 2025-06-27
        // if(unpacker->transmitList_host.size() > unpacker->maxQueueSize -1){
        //     DPRINTF(CXL_Unpacking, "%s, unpacker is busy, transmitList_host size = %d\n",__func__,unpacker->transmitList_host.size());

        //     //esj 2024-12-07
        //     unpacker->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += unpacker->UnPacking_packet(pkt);

        DPRINTF(CXL_Unpacking, "%s, Unpacking delay = %d \n",__func__, delay);

        unpacker->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Unpacking, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        unpacker->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Unpacking::CXL_UnpackingRequestPort::recvTimingResp(PacketPtr pkt)
    {   
        //esj 2024-12-09
        //  if(unpacker->Device2Host_busy || unpacker->transmitList_device.size() > unpacker->maxQueueSize){
        //esj 2025-03-02
        //  if(unpacker->transmitList_device.size() >= unpacker->maxQueueSize -1){

        //esj 2025-05-13
        // if(unpacker->transmitList_device.size() > unpacker->maxQueueSize -1){
        //     DPRINTF(CXL_Unpacking, "%s, unpacker is busy, transmitList_device size = %d\n",__func__,unpacker->transmitList_device.size());

        //     //esj 2024-12-09
        //     // unpacker->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += unpacker->UnPacking_packet(pkt);

        DPRINTF(CXL_Unpacking, "%s, Unpacking delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // unpacker->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Unpacking, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        if(!pkt->cxl_pkt.crc_check){
            DPRINTF(CXL_Unpacking, "%s, CRC check failed, passing unpacking\n",__func__);
            delay = 0;
        }

        unpacker->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }

    void
    CXL_Unpacking::schedTimingReq(PacketPtr pkt, Tick when)
    {
        // Link control terminates in CXL_ctrl, not at the DRAM endpoint.
        // Do not queue an ACK/NAK behind a normal request waiting for the
        // very replay space that this control flit will release.
        if (pkt->cxl_pkt.is_controlflit) {
            auto event = new EventFunctionWrapper([this, pkt] {
                DPRINTF(CXL_Unpacking,
                        "Control request bypass seq=%llu, data_waiting=%u\n",
                        pkt->cxl_pkt.seqNum, transmitList_host.size());
                fatal_if(!out_port.sendTimingReq(pkt),
                         "%s: local controller rejected a control request",
                         name());
            }, name() + ".controlReq", true);
            schedule(event, std::max(when, curTick()));
            return;
        }
        if (transmitList_host.empty()) {
            DPRINTF(CXL_Unpacking, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_Unpacking, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);
    } 

    void
    CXL_Unpacking::schedTimingResp(PacketPtr pkt, Tick when)
    {
        // Preserve normal-response FIFO order while allowing link control
        // to complete even when the host cannot accept the head response.
        if (pkt->cxl_pkt.is_controlflit) {
            auto event = new EventFunctionWrapper([this, pkt] {
                DPRINTF(CXL_Unpacking,
                        "Control response bypass seq=%llu, data_waiting=%u\n",
                        pkt->cxl_pkt.seqNum, transmitList_device.size());
                fatal_if(!in_port.sendTimingResp(pkt),
                         "%s: local controller rejected a control response",
                         name());
            }, name() + ".controlResp", true);
            schedule(event, std::max(when, curTick()));
            return;
        }
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Unpacking, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);
    }

    void
    CXL_Unpacking::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Unpacking, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.pop_front();

            Host2Device_busy = false;
            
            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_Unpacking, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }
    
        }
        // esj 2025-05-16
        // else{
        //     DPRINTF(CXL_Unpacking, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEvent, curTick()+this->clockPeriod());
        // }
        
        //esj 2025-06-29
        // else{
        //     transmitList_host.pop_front();
        // }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Unpacking,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Unpacking::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;
    
        DPRINTF(CXL_Unpacking, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.pop_front();

            //esj 2024-12-09
            // Device2Host_busy = false;    
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_Unpacking, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        //esj 2025-05-13
        // else{
        //     DPRINTF(CXL_Unpacking, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEventResp, curTick()+this->clockPeriod());
        // }

        //esj 2025-07-12
        // else{
        //     transmitList_device.pop_front();
        // }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Unpacking,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }


    //esj 2025-07-12
    // void
    // CXL_Unpacking :: CXL_UnpackingResponsePort :: recvRespRetry(){
    //     DPRINTF(CXL_Unpacking,"%s, retry Unpacking \n",__func__);
    //     // unpacker->schedule(unpacker->Unpacking_respEvent,curTick()+2*unpacker->clockPeriod());

    //     // unpacker->retryResp_host = true;
    //     if(!unpacker->sendEventResp.scheduled()){
    //         unpacker->schedule(unpacker->sendEventResp,curTick()+unpacker->clockPeriod());
    //     }
    // }

    void 
    CXL_Unpacking :: CXL_UnpackingResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Unpacking :: CXL_UnpackingResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Unpacking,"%s\n",__func__);
        Tick delay = unpacker->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Unpacking::Unpacking(){
        DPRINTF(CXL_Unpacking,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.is_packed);
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
    CXL_Unpacking::Unpacking_resp(){
        DPRINTF(CXL_Unpacking,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.is_packed);
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
    CXL_Unpacking::UnPacking_packet(PacketPtr packet){
        Tick delay = 0;
    
        if(packet->cxl_pkt.is_packed & packet->is_cxl_mem){
            // flit_total_num -= pkt->flit_num;
            delay = cycleperflit* this->clockPeriod()*packet->cxl_pkt.flit_num;
            packet->cxl_pkt.is_packed = false;
            DPRINTF(CXL_Unpacking,"%s, packing_size = %d, delay = %d\n",__func__,packet->cxl_pkt.packing_size,delay);
        }        

        return delay;
    }

    void 
    CXL_Unpacking::init()
    {
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Unpacking Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Unpacking::CXL_UnpackingRequestPort&
    CXL_Unpacking::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Unpacking::CXL_UnpackingResponsePort&
    CXL_Unpacking::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Unpacking::getPort(const std::string &if_name, PortID idx)
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
