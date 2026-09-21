/*
 * Copyright (c) 2010-2013, 2015 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/simple_mem.hh"

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
namespace memory
{

SimpleMemory::SimpleMemory(const SimpleMemoryParams &p) :
    AbstractMemory(p),
    port(name() + ".port", *this), latency(p.latency),
    // Request_port(name() + ".Request_port", *this),
    latency_var(p.latency_var), bandwidth(p.bandwidth), isBusy(false),
    retryReq(false), retryResp(false),
    releaseEvent([this]{ release(); }, name()),
    dequeueEvent([this]{ dequeue(); }, name())
{
    gem5::trace::pmemAddr_cxl = pmemAddr;
}

void
SimpleMemory::init()
{
    AbstractMemory::init();
    gem5::trace::cxl_check = 0;
    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    // if (port.isConnected() && Request_port.isConnected()) {
    if (port.isConnected() ) {
        port.sendRangeChange();
    }
}

Tick
SimpleMemory::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    Addr offset = 0xB0000000;
    Addr offset2 = 0x10000000;
    Addr cureent_pkt_addr = pkt->getAddr();

    // gem5::trace::pmemAddr_cxl = pmemAddr;
    // // warn("esj Simplememory addr =%x range.start() = %x\n",pkt->getAddr(),range.start());
    // access(pkt);

    if(!trace::is_timing_mode){
        gem5::trace::pmemAddr_cxl = pmemAddr;
        DPRINTF(Drain, "pmemaddr = %0x\n",gem5::trace::pmemAddr_cxl);
        access(pkt);
    }
    else{
        switch(gem5::trace::cxl_check){
            case 0:{
                //if( pkt->isWrite() )
                gem5::trace::pmemAddr_cxl = pmemAddr;
                // if( pkt->isWrite() && pkt->isRequest() )
                //     gem5::trace::packets.push_back(copyPacket(pkt));
                access(pkt);
                break;
            }
            case 1:{
                // cureent_pkt_addr += offset;
                gem5::trace::pmemAddr_cxl = pmemAddr;
                // memcpy_to_cxl(); 
                gem5::trace::cxl_check=2;
                warn("first CXL init window");
                pkt->cxl_flag = true;
                //warn("DEXTER || before simple memory current addr = %08x after addr = %08x write? = %d read? = %d data? = %d request? = %08x\n",cureent_pkt_addr,pkt->getAddr(),pkt->isWrite(),pkt->isRead(), pkt->hasData(), pkt->isRequest());
                // pkt->setAddr(cureent_pkt_addr + offset);
                // warn("DEXTER || after simple memory current addr = %08x after addr = %08x write? = %d read? = %d data? = %d request? = %08x\n",cureent_pkt_addr,pkt->getAddr(),pkt->isWrite(),pkt->isRead(), pkt->hasData(), pkt->isRequest());
                // sendPacket(Request_port, pkt);
                access(pkt);
                break;
            }
            // case 2:{
            //     pkt->setAddr(cureent_pkt_addr + offset);
            //     sendPacket(Request_port, pkt);
            //     break;
            // }
            default:{
                panic("something wrong");
            }
        }
    }
    
    return getLatency();
}

// /// DEXTER
// /// @brief Save the data from dram at cxl window during linux booting to CXL memory
// /// @param  
// void
// SimpleMemory::memcpy_to_cxl()
// {
//     Addr offset = 0xB0000000;
//     warn("memcpy_to_cxl is start");
//     // warn("memcpy_to_cxl \t size : %d ",gem5::trace::packets.size());
//     while(!gem5::trace::packets.empty()){
//         // warn("memcpy_to_cxl 1");
//         PacketPtr pkt = gem5::trace::packets.front();
//         // if(pkt->getAddr()>=0xcfff9000){
//         //     continue;
//         //     gem5::trace::packets.pop_front();
//         // }
//         // warn("memcpy_to_cxl 2");
//         // Addr cureent_pkt_addr=pkt->getAddr(); //여기에서 죽음 (packet 전달 완료되서 죽이는 코드가 있을 것 같다??)
//         // warn("memcpy_to_cxl 3");
//         // cureent_pkt_addr += offset;
//         // warn("memcpy_to_cxl 4");
//         // pkt->setAddr(cureent_pkt_addr);
//         // warn("memcpy_to_cxl 4 || pkt addr : %08x req paddr : %08x ",pkt->getAddr(), pkt->req->getPaddr());
//         // warn("memcpy_to_cxl 5 || PKT size is %d", pkt->getSize());
//         //debug위해 try문으로 변경
//             sendPacket(Request_port, pkt);      
//         // sendPacket(Request_port, pkt);
//         // warn("memcpy_to_cxl 6");
//         gem5::trace::packets.pop_front();
//     }
    
//     warn("memcpy_to_cxl is END@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
//     //문제를 해결했으나 공간이 좁아 주석을 달지 않았다. -DEXTER

// }
// //DEXTER


//DEXTER
/// @brief 
/// @param  
PacketPtr
SimpleMemory::copyPacket(PacketPtr srcPkt)
{ //it is for memcpy_to_cxl
    srcPkt->req->setPaddr(srcPkt->req->getPaddr()+0xB0000000);
    PacketPtr newPkt = new Packet(srcPkt->req, srcPkt->cmd, srcPkt->getSize());
    newPkt->cxl_flag=1;
    // warn("PacketPtr is working");
    if (srcPkt->hasData()) {
        newPkt->dataDynamic(srcPkt->getConstPtr<uint8_t>());
    }
    return newPkt;

}
//DEXTER

Tick
SimpleMemory::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    warn("CXL recvAtomicBackdoor");
    Tick latency = recvAtomic(pkt);
    getBackdoor(_backdoor);
    return latency;
}

void
SimpleMemory::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    bool done = false;
    auto p = packetQueue.begin();
    // potentially update the packets in our packet queue as well
    while (!done && p != packetQueue.end()) {
        done = pkt->trySatisfyFunctional(p->pkt);
        ++p;
    }

    pkt->popLabel();
}

void
SimpleMemory::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &_backdoor)
{
    getBackdoor(_backdoor);
}

bool
SimpleMemory::recvTimingReq(PacketPtr pkt)
{
    gem5::trace::pmemAddr_cxl = pmemAddr;
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    // if we are busy with a read or write, remember that we have to
    // retry
    if (isBusy) {
        retryReq = true;
        return false;
    }

    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    Tick duration = pkt->getSize() * bandwidth;

    // only consider ourselves busy if there is any need to wait
    // to avoid extra events being scheduled for (infinitely) fast
    // memories
    if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }

    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);

    // turn packet around to go back to requestor if response expected
    if(!pkt->cxl_flag){
        if (needsResponse) {
            // recvAtomic() should already have turned packet into
            // atomic response
            assert(pkt->isResponse());

            Tick when_to_send = curTick() + receive_delay + getLatency();

            // typically this should be added at the end, so start the
            // insertion sort with the last element, also make sure not to
            // re-order in front of some existing packet with the same
            // address, the latter is important as this memory effectively
            // hands out exclusive copies (shared is not asserted)
            auto i = packetQueue.end();
            --i;
            while (i != packetQueue.begin() && when_to_send < i->tick &&
                !i->pkt->matchAddr(pkt))
                --i;

            // emplace inserts the element before the position pointed to by
            // the iterator, so advance it one step
            packetQueue.emplace(++i, pkt, when_to_send);

            if (!retryResp && !dequeueEvent.scheduled())
                schedule(dequeueEvent, packetQueue.back().tick);
            } 
        else {
            pendingDelete.reset(pkt);
        }

    return true;
    }
    else
        return true;
}

void
SimpleMemory::release()
{
    assert(isBusy);
    isBusy = false;
    if (retryReq) {
        retryReq = false;
        port.sendRetryReq();
    }
}

void
SimpleMemory::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    retryResp = !port.sendTimingResp(deferred_pkt.pkt);

    if (!retryResp) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainState() == DrainState::Draining) {
            DPRINTF(Drain, "Draining of SimpleMemory complete\n");
            signalDrainDone();
        }
    }
}

Tick
SimpleMemory::getLatency() const //esj 2024-07-12
{
    if(gem5::trace::numa_mode){
        return latency + 150*sim_clock::as_int::ns+
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
    }
    else{
        return latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
    }
}

    
void
SimpleMemory::recvRespRetry()
{
    assert(retryResp);

    dequeue();
}

// Port &
// SimpleMemory::getPort(const std::string &if_name, PortID idx)
// {
//     if (if_name != "port") {
//         return AbstractMemory::getPort(if_name, idx);
//     } else {
//         return port;
//     }
// }

Port &
SimpleMemory::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port") {
        return port;
    }
    // else if(if_name == "Request_port"){
    //     return Request_port;
    // }
    else {
        return AbstractMemory::getPort(if_name, idx);
    }
}

DrainState
SimpleMemory::drain()
{
    if (!packetQueue.empty()) {
        DPRINTF(Drain, "SimpleMemory Queue has requests, waiting to drain\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

SimpleMemory::MemoryPort::MemoryPort(const std::string& _name,
                                     SimpleMemory& _memory)
    : ResponsePort(_name), mem(_memory)
{ }

AddrRangeList
SimpleMemory::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
SimpleMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return mem.recvAtomic(pkt);
}

Tick
SimpleMemory::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    return mem.recvAtomicBackdoor(pkt, _backdoor);
}

void
SimpleMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    mem.recvFunctional(pkt);
}

void
SimpleMemory::MemoryPort::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &backdoor)
{
    mem.recvMemBackdoorReq(req, backdoor);
}

bool
SimpleMemory::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt);
}

void
SimpleMemory::MemoryPort::recvRespRetry()
{
    mem.recvRespRetry();
}

///////////////////////////////////////////////////
// SimpleMemory::MemoryRequestPort::MemoryRequestPort(const std::string& _name,
//                                      SimpleMemory& _memory)
//     :RequestPort(_name), mem(_memory)
// {}

// Tick
// SimpleMemory::sendPacket(RequestPort &port, const PacketPtr &pkt)
// {
//     // return port.sendAtomic(pkt);
//     return port.sendTimingReq(pkt);
// }

} // namespace memory
} // namespace gem5
