
#include "dev/CXL/CXL_FlexBus.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_FlexBus.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    CXL_FlexBus::CXL_FlexBusRequestPort::CXL_FlexBusRequestPort(const std::string& _name,CXL_FlexBus* flexbus)
        : RequestPort(_name, flexbus), flexbus(flexbus)
    {}
    

    CXL_FlexBus::CXL_FlexBusResponsePort::CXL_FlexBusResponsePort(const std::string& _name,CXL_FlexBus* flexbus)
        : ResponsePort(_name, flexbus), flexbus(flexbus)
    {}
    
    CXL_FlexBus :: CXL_FlexBus(const Param &p) 
        : ClockedObject(p),
        Host_resp_port(p.name +".Host_resp_port", this),
        Host_req_port(p.name +".Host_req_port", this),
        Device_req_port(p.name +".Device_req_port", this),
        Device_resp_port(p.name +".Device_resp_port", this),
        PCIe_req_port(p.name +".PCIe_req_port", this),
        PCIe_resp_port(p.name +".PCIe_resp_port", this),
        CXL_out_port(p.name +".CXL_out_port", this),
        CXL_in_port(p.name +".CXL_in_port", this),

        // ArbitratingEvent([this]{Arbitrating();} , p.name), 
        retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        // Arbitrating_respEvent([this]{Arbitrating_resp();}, p.name),
        Device2Host_busy(false),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name),is_host(p.is_host)
    {

    }


    bool 
    CXL_FlexBus :: CXL_FlexBusResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(flexbus->Host2Device_busy || flexbus->transmitList_host.size() > flexbus->maxQueueSize){
        //esj 2025-03-02
        // if(flexbus->transmitList_host.size() >= flexbus->maxQueueSize -1){
        //esj 2025-06-27
        // if(flexbus->transmitList_host.size() > flexbus->maxQueueSize -1){
        //     DPRINTF(CXL_FlexBus, "%s, flexbus is busy, transmitList_host size = %d\n",__func__,flexbus->transmitList_host.size());

        //     //esj 2024-12-07
        //     flexbus->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += flexbus->Arbitrating_packet(pkt);

        DPRINTF(CXL_FlexBus, "%s, packing delay = %d \n",__func__, delay);


        //esj 2024-12-09
        // flexbus->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_FlexBus, "[%s] receive_delay = %d, delay = %d, port name = %s\n",__func__, receive_delay, delay, name());

        flexbus->schedTimingReq(pkt, curTick() + receive_delay + delay, name());

        return true;
    }

    bool 
    CXL_FlexBus::CXL_FlexBusRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2024-12-09
        // if(flexbus->Device2Host_busy || flexbus->transmitList_device.size() > flexbus->maxQueueSize){
        //esj 2025-03-02
        // if(flexbus->transmitList_device.size() >= flexbus->maxQueueSize -1){
        //esj 2025-06-27
        // if(flexbus->transmitList_device.size() > flexbus->maxQueueSize -1){
        //     DPRINTF(CXL_FlexBus, "%s, flexbus is busy, transmitList_device size = %d\n",__func__,flexbus->transmitList_device.size());

        //     //esj 2024-12-07
        //     flexbus->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += flexbus->Arbitrating_packet(pkt);

        DPRINTF(CXL_FlexBus, "%s, packing delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // flexbus->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_FlexBus, "[%s] receive_delay = %d, delay = %d, name = %s\n",__func__, receive_delay, delay, name());

        flexbus->schedTimingResp(pkt, curTick() + receive_delay + delay, name());

        return true;


    }

    void
    CXL_FlexBus :: CXL_FlexBusResponsePort :: recvRespRetry(){
        DPRINTF(CXL_FlexBus,"%s, retry Arbitrating \n",__func__);
        // flexbus->schedule(flexbus->Arbitrating_respEvent,curTick()+2*flexbus->clockPeriod());

        // flexbus->retryResp_host = true;
        if(!flexbus->sendEventResp.scheduled()){
            flexbus->schedule(flexbus->sendEventResp,curTick()+flexbus->clockPeriod());
        }
    }

    void 
    CXL_FlexBus :: CXL_FlexBusResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_FlexBus :: CXL_FlexBusResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_FlexBus,"%s\n",__func__);
        static unsigned flexbus_atomic_debug_prints = 0;
        if (flexbus_atomic_debug_prints < 128) {
            warn("esj CXL_FlexBus::recvAtomic port=%s addr=%#llx "
                 "size=%u cxl_flag=%d cxl_io=%d cxl_mem=%d "
                 "is_host=%d route=Device_req_port\n",
                 name().c_str(), (unsigned long long)pkt->getAddr(),
                 pkt->getSize(), pkt->cxl_flag, pkt->is_cxl_io,
                 pkt->is_cxl_mem, flexbus->is_host);
            flexbus_atomic_debug_prints++;
        }
        Tick delay = flexbus->Device_req_port.sendAtomic(pkt);
        return delay;
    }

    // void
    // CXL_FlexBus::Arbitrating(){
    //     DPRINTF(CXL_FlexBus,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
    //     bool success = this->Host_req_port.sendTimingReq(packet);
    //     Host2Device_busy = false;

    //     if(success){
    //         packet = NULL;
    //         retry = false;
    //     }
    //     else{
    //         retry = true;
    //         this->Host_req_port.recvReqRetry();//esj 2024-12-07
    //         // this->Host_resp_port.recvRespRetry();
    //     }
    // }

    // void
    // CXL_FlexBus::Arbitrating_resp(){
    //     DPRINTF(CXL_FlexBus,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr());
    //     bool success = this->Host_resp_port.sendTimingResp(packet);
    //     Device2Host_busy = false;

    //     if(success){
    //         packet = NULL;
    //         retry = false;
    //     }
    //     else{
    //         retry = true;
    //         this->Host_resp_port.recvRespRetry();
    //     }
    // }

    Tick
    CXL_FlexBus::Arbitrating_packet(PacketPtr packet){
        Tick delay = 0;
        // DPRINTF(CXLLink,"%s\n",__func__);

        DPRINTF(CXL_FlexBus,"%s\n",__func__);

        delay =  cycleperflit*this->clockPeriod()*packet->cxl_pkt.flit_num;
        DPRINTF(CXL_FlexBus,"%s,  delay = %d\n",__func__,delay);


        // DPRINTF(CXLLink,"%s\n",__func__);
        return delay;
    }


    void
    CXL_FlexBus::schedTimingReq(PacketPtr pkt, Tick when, std::string portname)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_FlexBus, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_FlexBus, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when, portname);
    } 

    void
    CXL_FlexBus::schedTimingResp(PacketPtr pkt, Tick when, std::string portname)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_FlexBus, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when, portname);
    }

    void
    CXL_FlexBus::trySendTiming()
    {
        assert(!transmitList_host.empty());

        DeferredPacket req = transmitList_host.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        std::string portname = req.portname;
    
        DPRINTF(CXL_FlexBus, "trySend request addr 0x%x, queue size %d, port name = %s\n",
                pkt->getAddr(), transmitList_host.size(),portname);

        bool success = false;

        if(portname.find("Host_req_port") != std::string::npos){
            if(is_host){
                if(pkt->cxl_flag)
                    success = this->Device_req_port.sendTimingReq(pkt);
                else
                    success = this->PCIe_req_port.sendTimingReq(pkt);
            }
            else{
                success = this->Device_req_port.sendTimingReq(pkt);
            }
        }
        else if(portname.find("PCIe_resp_port") != std::string::npos){
            success = this->CXL_out_port.sendTimingReq(pkt);
        }
        else if(portname.find("CXL_in_port") != std::string::npos){
            success = this->PCIe_req_port.sendTimingReq(pkt);
        }
        
        

        if (success) {
            // send successful
            transmitList_host.pop_front();
            
            if (!transmitList_host.empty()) {
                DeferredPacket next_req = transmitList_host.front();
                DPRINTF(CXL_FlexBus, "Scheduling next send\n");
                schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_FlexBus, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
        }

    }

    void
    CXL_FlexBus::trySendTimingResp(){
        assert(!transmitList_device.empty());

        DeferredPacket req = transmitList_device.front();

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        std::string portname = req.portname;
    
        DPRINTF(CXL_FlexBus, "trySend response addr 0x%x, queue size %d, port name = %s\n",
                    pkt->getAddr(), transmitList_device.size(), portname);

        bool success = false;

        if(portname.find("Device_resp_port") != std::string::npos){
            if(is_host){
                success = this->Host_resp_port.sendTimingResp(pkt);
            }
            else{
                if(pkt->cxl_flag)
                    success = this->Host_resp_port.sendTimingResp(pkt);
                else
                    success = this->PCIe_resp_port.sendTimingResp(pkt);
            }
            
        }
        else if(portname.find("PCIe_req_port") != std::string::npos){
            success = this->CXL_in_port.sendTimingResp(pkt);
        }
        else if(portname.find("CXL_out_port") != std::string::npos){
            success = this->PCIe_resp_port.sendTimingResp(pkt);
        }

        if (success) {
            transmitList_device.pop_front();
            
            if (!transmitList_device.empty()) {
                DeferredPacket next_req = transmitList_device.front();
                DPRINTF(CXL_FlexBus, "Scheduling next send\n");
                schedule(sendEventResp, std::max(next_req.tick,clockEdge()));
            }
    
        }
        else{
            DPRINTF(CXL_FlexBus, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
        }


    }

    void 
    CXL_FlexBus::init()
    {
        // if (!Host_resp_port.isConnected() || !Host_req_port.isConnected()) {
        
        //     fatal ("CXL_FlexBus Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_FlexBus::CXL_FlexBusRequestPort&
    CXL_FlexBus::getRequestPort(const std::string &if_name, PortID idx){
        
        if(if_name == "PCIe_req_port"){
            return PCIe_req_port;
        }
        else if(if_name == "Device_req_port"){
            return Device_req_port;
        }
        else if(if_name == "Device_resp_port"){
            return Device_resp_port;
        }
        else if(if_name == "CXL_out_port"){
            return CXL_out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_FlexBus::CXL_FlexBusResponsePort&
    CXL_FlexBus::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "Host_resp_port") {
            return Host_resp_port;
        }
        else if(if_name == "PCIe_resp_port"){
            return PCIe_resp_port;
        }
        else if (if_name == "Host_req_port") {
            return Host_req_port;
        }
        else if (if_name == "CXL_in_port") {
            return CXL_in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_FlexBus::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "Host_req_port") {
            return Host_req_port;
        }
        else if(if_name == "PCIe_req_port"){
            return PCIe_req_port;
        }
        else if(if_name == "Device_req_port"){
            return Device_req_port;
        }
        else if (if_name == "Host_resp_port") {
            return Host_resp_port;
        }
        else if(if_name == "PCIe_resp_port"){
            return PCIe_resp_port;
        }
        else if(if_name == "Device_resp_port"){
            return Device_resp_port;
        }
        else if(if_name == "CXL_out_port"){
            return CXL_out_port;
        }
        else if (if_name == "CXL_in_port") {
            return CXL_in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }

}
