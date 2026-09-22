
#include "dev/CXL/CXL_Packing.hh"
#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_Packing.hh"
#include "sim/core.hh"
#include "sim/protocol_validation_logger.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
    namespace
    {

    bool
    isRetryControl(PacketPtr pkt)
    {
        return pkt != nullptr && pkt->cxl_pkt.is_controlflit &&
            (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp ||
             pkt->cxl_pkt.retry_ack);
    }

    bool
    isRetryData(PacketPtr pkt)
    {
        return pkt != nullptr && !pkt->cxl_pkt.is_controlflit &&
            (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp);
    }

    bool
    hasRetryPriority(PacketPtr pkt)
    {
        return isRetryControl(pkt) || isRetryData(pkt);
    }

    } // anonymous namespace

    CXL_Packing::CXL_PackingRequestPort::CXL_PackingRequestPort(const std::string& _name,CXL_Packing* packer)
        : RequestPort(_name, packer), packer(packer)
    {}
    

    CXL_Packing::CXL_PackingResponsePort::CXL_PackingResponsePort(const std::string& _name,CXL_Packing* packer)
        : ResponsePort(_name, packer), packer(packer)
    {}
    
    CXL_Packing :: CXL_Packing(const Param &p) 
        : ClockedObject(p),
        in_port(p.name +".in_port", this), 
        out_port(p.name +".out_port", this),
        PackingEvent([this]{packing();} , p.name), retry(false),packet(NULL),cycleperflit(p.cycleperflit),
        Packing_respEvent([this]{packing_resp();}, p.name),Device2Host_busy(false),Host2Device_busy(false),
        maxQueueSize(p.max_queue_size),sendEvent([this]{trySendTiming();}, p.name),retryReq_host(false),retryResp_host(false),
        sendEventResp([this]{trySendTimingResp();}, p.name)

    {

    }

    // bool 
    // CXL_Packing::CXL_PackingRequestPort::recvTimingResp(PacketPtr pkt)
    // {
    //     Tick delay = 0;

    //     delay += packer->packing_packet(pkt);

    //     DPRINTF(CXL_Packing, "%s, packing delay = %d \n",__func__, delay);

    //     if(packer->Device2Host_busy){
    //         DPRINTF(CXL_Packing, "%s, packer is busy\n",__func__);
    //         return false;
    //     }
    //     // if(!packer->retry){
    //     //     packer->packet = pkt;
    //     // }
    //     if(!packer->Packing_respEvent.scheduled()){
    //         packer->Device2Host_busy = true;
    //         if(!packer->retry){
    //             packer->packet = pkt;
    //         }
    //         packer->schedule(packer->Packing_respEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
        
    //     return true;
    // }

    // bool 
    // CXL_Packing :: CXL_PackingResponsePort :: recvTimingReq(PacketPtr pkt) 
    // {  
    //     Tick delay = 0;

    //     delay += packer->packing_packet(pkt);

    //     DPRINTF(CXL_Packing, "%s, packing delay = %d \n",__func__, delay);

    //     if(packer->Host2Device_busy){
    //         DPRINTF(CXL_Packing, "%s, packer is busy\n",__func__);
    //         return false;
    //     }

    //     //esj 2024-12-07
    //     // if(!packer->retry){
    //     //     packer->packet = pkt;
    //     // }

    //     if(!packer->PackingEvent.scheduled()){
    //         //esj 2024-12-07
    //         packer->Host2Device_busy = true;
    //         if(!packer->retry){
    //             packer->packet = pkt;
    //         }

    //         packer->schedule(packer->PackingEvent,curTick()+delay);
    //     }
    //     else{
    //         return false;
    //     }
    //     return true;

    // }

    bool 
    CXL_Packing :: CXL_PackingResponsePort :: recvTimingReq(PacketPtr pkt) 
    {  
        //esj 2024-12-09
        // if(packer->Host2Device_busy || packer->transmitList_host.size() > packer->maxQueueSize){
        //esj 2025-03-02
        // if(packer->transmitList_host.size() >= packer->maxQueueSize -1){
        
        // esj 2025-05-16
        // if(packer->transmitList_host.size() > packer->maxQueueSize -1){
        //     DPRINTF(CXL_Packing, "%s, packer is busy, transmitList_host size = %d\n",__func__,packer->transmitList_host.size());

        //     //esj 2024-12-09
        //     // packer->retryReq_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += packer->packing_packet(pkt);

        // For merged request parent, process child packet packing as well.
        // Response path is intentionally excluded.
        if (pkt->isRequest() && pkt->cxlMergedParent &&
            (pkt->cxlMergedPkt != nullptr)) {
            Tick child_delay = packer->packing_packet(pkt->cxlMergedPkt);
            delay += child_delay;
            DPRINTF(CXL_Packing,
                    "[%s] merged req child packing: parent=0x%x child=0x%x "
                    "child_delay=%d total_delay=%d\n",
                    __func__, pkt->getAddr(), pkt->cxlMergedPkt->getAddr(),
                    child_delay, delay);
        }

        DPRINTF(CXL_Packing, "%s, packing delay = %d \n",__func__, delay);

        // packer->Host2Device_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Packing, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        packer->schedTimingReq(pkt, curTick() + receive_delay + delay);

        return true;
    }

    bool 
    CXL_Packing::CXL_PackingRequestPort::recvTimingResp(PacketPtr pkt)
    {
        //esj 2024-12-09
        //  if(packer->Device2Host_busy || packer->transmitList_device.size() > packer->maxQueueSize){
        //esj 2025-03-02
        // if(packer->transmitList_device.size() >= packer->maxQueueSize -1){

        // esj 2025-05-16
        // if(packer->transmitList_device.size() > packer->maxQueueSize -1){
        //     DPRINTF(CXL_Packing, "%s, packer is busy, transmitList_device size = %d\n",__func__,packer->transmitList_device.size());

        //     //esj 2024-12-09
        //     // packer->retryResp_host = true;
        //     return false;
        // }

        Tick delay = 0;

        delay += packer->packing_packet(pkt);

        DPRINTF(CXL_Packing, "%s, packing delay = %d \n",__func__, delay);

        //esj 2024-12-09
        // packer->Device2Host_busy = true;

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        DPRINTF(CXL_Packing, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);

        packer->schedTimingResp(pkt, curTick() + receive_delay + delay);

        return true;


    }


    void
    CXL_Packing :: CXL_PackingResponsePort :: recvRespRetry(){
        DPRINTF(CXL_Packing,"%s, retry packing \n",__func__);
        // packer->schedule(packer->Packing_respEvent,curTick()+2*packer->clockPeriod());

        // packer->retryResp_host = true;
        if(!packer->sendEventResp.scheduled()){
            packer->schedule(packer->sendEventResp,curTick()+packer->clockPeriod());
        }
    }

    void 
    CXL_Packing :: CXL_PackingResponsePort :: recvFunctional(PacketPtr pkt) 
    {  
        recvTimingReq(pkt); 
    }

    Tick 
    CXL_Packing :: CXL_PackingResponsePort :: recvAtomic(PacketPtr pkt) 
    {  
        DPRINTF(CXL_Packing,"%s\n",__func__);
        Tick delay = packer->out_port.sendAtomic(pkt);
        return delay;
    }

    void
    CXL_Packing::packing(){
        DPRINTF(CXL_Packing,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.is_packed);
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
    CXL_Packing::packing_resp(){
        DPRINTF(CXL_Packing,"%s, pkt addr = %0x, packed = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.is_packed);
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
    CXL_Packing::packing_packet(PacketPtr packet){
        Tick delay = 0;
        if(!packet->cxl_pkt.is_packed & packet->is_cxl_mem){
            if(packet->cxl_pkt.isReq | packet->cxl_pkt.isNDR){
                //esj 2025-07-28
                packet->cxl_pkt.packing_size = 64;
                // packet->cxl_pkt.packing_size = 16;
                packet->cxl_pkt.flit_num = 1;
                delay = cycleperflit*this->clockPeriod();
                DPRINTF(CXL_Packing,"%s isReq? = %d, isNDR?= %d, packing_size = %d, flit_num = %d, delay = %d\n",__func__,packet->cxl_pkt.isReq,packet->cxl_pkt.isNDR,packet->cxl_pkt.packing_size,packet->cxl_pkt.flit_num,delay);
            }
            else if(packet->cxl_pkt.isRwD | packet->cxl_pkt.isDRS){
                unsigned int size = packet->getSize();
                
                if(size <= 48){
                    packet->cxl_pkt.packing_size = 64;
                    packet->cxl_pkt.flit_num = 1;
                    delay = cycleperflit*this->clockPeriod();
                }
                else{
                    if((size-48)%64 == 0){
                        packet->cxl_pkt.flit_num = (size-48)/64;
                    }
                    else{
                        packet->cxl_pkt.flit_num = ((size-48)/64 + 1);
                    }
                    
                    packet->cxl_pkt.packing_size = 64 * packet->cxl_pkt.flit_num;
                    delay = cycleperflit*this->clockPeriod();
                }
                // pkt->packing_size += (2*pkt->flit_num); //protocol id add
                DPRINTF(CXL_Packing,"%s isRwD? = %d, isDRS?= %d, packing_size = %d, flit_num = %d, delay = %d\n",__func__,packet->cxl_pkt.isRwD,packet->cxl_pkt.isDRS,packet->cxl_pkt.packing_size,packet->cxl_pkt.flit_num,delay);
                
            }
            packet->cxl_pkt.is_packed = true;
        }
        else if(packet->cxl_pkt.is_controlflit){
            //esj 2025-07-28
            // packet->cxl_pkt.packing_size = 16;
            packet->cxl_pkt.packing_size = 64;
            packet->cxl_pkt.flit_num = 1;
            delay = cycleperflit*this->clockPeriod();
            //esj 2025-03-13
            // packet->cxl_pkt.is_packed = true;
        }
        return delay;
    }

    void
    CXL_Packing::schedTimingReq(PacketPtr pkt, Tick when)
    {
        if (transmitList_host.empty()) {
            DPRINTF(CXL_Packing, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_host.size());
            schedule(sendEvent, when);
        }
        DPRINTF(CXL_Packing, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_host.size());

        //esj 2025-11-29
        // assert(transmitList_host.size() != maxQueueSize);

        transmitList_host.emplace_back(pkt, when);

        if (hasRetryPriority(pkt) && sendEvent.scheduled() &&
            when < sendEvent.when()) {
            reschedule(sendEvent, std::max(when, curTick()), true);
        }
    } 

    void
    CXL_Packing::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if (transmitList_device.empty()) {
            DPRINTF(CXL_Packing, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_device.size());
            schedule(sendEventResp, when);
        }
        transmitList_device.emplace_back(pkt, when);

        if (hasRetryPriority(pkt) && sendEventResp.scheduled() &&
            when < sendEventResp.when()) {
            reschedule(sendEventResp, std::max(when, curTick()), true);
        }
    }

    size_t
    CXL_Packing::selectHostTxIndex() const
    {
        for (size_t index = 0; index < transmitList_host.size(); ++index) {
            const DeferredPacket &candidate = transmitList_host[index];
            if (isRetryControl(candidate.pkt) &&
                candidate.tick <= curTick()) {
                return index;
            }
        }

        for (size_t index = 0; index < transmitList_host.size(); ++index) {
            const DeferredPacket &candidate = transmitList_host[index];
            if (isRetryData(candidate.pkt) && candidate.tick <= curTick()) {
                return index;
            }
        }

        return 0;
    }

    size_t
    CXL_Packing::selectDeviceTxIndex() const
    {
        for (size_t index = 0; index < transmitList_device.size(); ++index) {
            const DeferredPacket &candidate = transmitList_device[index];
            if (isRetryControl(candidate.pkt) &&
                candidate.tick <= curTick()) {
                return index;
            }
        }

        for (size_t index = 0; index < transmitList_device.size(); ++index) {
            const DeferredPacket &candidate = transmitList_device[index];
            if (isRetryData(candidate.pkt) && candidate.tick <= curTick()) {
                return index;
            }
        }

        return 0;
    }

    Tick
    CXL_Packing::nextHostTxWakeup() const
    {
        assert(!transmitList_host.empty());

        Tick wakeup = transmitList_host.front().tick;
        for (const DeferredPacket &candidate : transmitList_host) {
            if (hasRetryPriority(candidate.pkt))
                wakeup = std::min(wakeup, candidate.tick);
        }
        return wakeup;
    }

    Tick
    CXL_Packing::nextDeviceTxWakeup() const
    {
        assert(!transmitList_device.empty());

        Tick wakeup = transmitList_device.front().tick;
        for (const DeferredPacket &candidate : transmitList_device) {
            if (hasRetryPriority(candidate.pkt))
                wakeup = std::min(wakeup, candidate.tick);
        }
        return wakeup;
    }

    void
    CXL_Packing::trySendTiming()
    {
        assert(!transmitList_host.empty());

        const size_t selected_index = selectHostTxIndex();
        DeferredPacket req = transmitList_host[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (hasRetryPriority(pkt)) {
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "H2D";
            arbitration.packetSeq = pkt->cxl_pkt.seqNum;
            arbitration.queueId = "packing_h2d_tx";
            arbitration.queueOccupancy = transmitList_host.size();
            arbitration.reason = isRetryControl(pkt) ?
                (selected_index == 0 ? "LRSM_CONTROL_PRIORITY_HEAD" :
                                       "LRSM_CONTROL_PRIORITY_BYPASS") :
                (selected_index == 0 ? "RRSM_RETRY_PRIORITY_HEAD" :
                                       "RRSM_RETRY_PRIORITY_BYPASS");
            ProtocolValidationLogger::recordPacket(arbitration, pkt);
        }
    
        DPRINTF(CXL_Packing, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList_host.size());

        if (this->out_port.sendTimingReq(pkt)) {
            // send successful
            transmitList_host.erase(
                transmitList_host.begin() + selected_index);

            //esj 2024-12-09
            // Host2Device_busy = false;
            
            if (!transmitList_host.empty()) {
                DPRINTF(CXL_Packing, "Scheduling next send\n");
                schedule(sendEvent,
                    std::max(nextHostTxWakeup(), clockEdge()));
            }
    
        }
        // esj 2025-05-19
        // else{
        //     DPRINTF(CXL_Packing, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEvent, curTick()+this->clockPeriod());
        // }
        else{
            transmitList_host.erase(
                transmitList_host.begin() + selected_index);
        }

        // if (retryReq_host) {  
        //     DPRINTF(CXL_Packing,"%s, retryreq \n",__func__);
        //     retryReq_host = false ;
        //     in_port.sendRetryReq();
        // }
    }

    void
    CXL_Packing::trySendTimingResp(){
        assert(!transmitList_device.empty());

        const size_t selected_index = selectDeviceTxIndex();
        DeferredPacket req = transmitList_device[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (hasRetryPriority(pkt)) {
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "D2H";
            arbitration.packetSeq = pkt->cxl_pkt.seqNum;
            arbitration.queueId = "packing_d2h_tx";
            arbitration.queueOccupancy = transmitList_device.size();
            arbitration.reason = isRetryControl(pkt) ?
                (selected_index == 0 ? "LRSM_CONTROL_PRIORITY_HEAD" :
                                       "LRSM_CONTROL_PRIORITY_BYPASS") :
                (selected_index == 0 ? "RRSM_RETRY_PRIORITY_HEAD" :
                                       "RRSM_RETRY_PRIORITY_BYPASS");
            ProtocolValidationLogger::recordPacket(arbitration, pkt);
        }
    
        DPRINTF(CXL_Packing, "trySend response addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_device.size());

        if (this->in_port.sendTimingResp(pkt)) {
            // send successful
            transmitList_device.erase(
                transmitList_device.begin() + selected_index);

            //esj 2024-12-09
            // Device2Host_busy = false;
            
            if (!transmitList_device.empty()) {
                DPRINTF(CXL_Packing, "Scheduling next send\n");
                schedule(sendEventResp,
                    std::max(nextDeviceTxWakeup(), clockEdge()));
            }
    
        }
        // esj 2025-05-19
        // else{
        //     DPRINTF(CXL_Packing, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEventResp, curTick()+this->clockPeriod());
        // }
        else{
            transmitList_device.erase(
                transmitList_device.begin() + selected_index);
        }

        // if (retryResp_host) {  
        //     DPRINTF(CXL_Packing,"%s, retryresp \n",__func__);
        //     retryResp_host = false ;
        //     out_port.sendRetryResp();
        // }
    }


    void 
    CXL_Packing::init()
    {
        // if (!in_port.isConnected() || !out_port.isConnected()) {
        
        //     fatal ("CXL_Packing Link Ports must be connected !!\n") ;
        // } 
    }
    
    CXL_Packing::CXL_PackingRequestPort&
    CXL_Packing::getRequestPort(const std::string &if_name, PortID idx){
        if (if_name == "out_port") {
            return out_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_Packing::CXL_PackingResponsePort&
    CXL_Packing::getResponsePort(const std::string &if_name, PortID idx)
    {
        if (if_name == "in_port") {
            return in_port;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    Port & CXL_Packing::getPort(const std::string &if_name, PortID idx)
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
