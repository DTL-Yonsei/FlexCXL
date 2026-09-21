/*
 * Copyright (c) 2019-2021 ARM Limited
 * All rights reserved.
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
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
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

#include "mem/ruby/system/CXLSequencer.hh"

#include "arch/x86/ldstflags.hh"
#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/str.hh"
#include "cpu/testers/rubytest/RubyTester.hh"
#include "debug/LLSC.hh"
#include "debug/MemoryAccess.hh"
#include "debug/ProtocolTrace.hh"
#include "debug/RubyHitMiss.hh"
#include "debug/CXLSequencer.hh"
#include "debug/RubyStats.hh"
#include "mem/packet.hh"
#include "mem/ruby/profiler/Profiler.hh"
#include "mem/ruby/protocol/PrefetchBit.hh"
#include "mem/ruby/protocol/RubyAccessMode.hh"
#include "mem/ruby/slicc_interface/RubyRequest.hh"
#include "mem/ruby/slicc_interface/RubySlicc_Util.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "sim/system.hh"

namespace gem5
{

namespace ruby
{

CXLSequencer::CXLSequencer(const Params &p)
    : RubyPort(p), m_IncompleteTimes(MachineType_NUM),
    deadlockCheckEvent([this]{ wakeup(); }, "CXLSequencer deadlock check")
{
    m_outstanding_count = 0;

//  m_dataCache_ptr = p.dcache;
    m_max_outstanding_requests = p.max_outstanding_requests;
    m_deadlock_threshold = p.deadlock_threshold;

    m_coreId = p.coreid; // for tracking the two CorePair CXLSequencers
    assert(m_max_outstanding_requests > 0);
    assert(m_deadlock_threshold > 0);

    m_unaddressedTransactionCnt = 0;

    m_runningGarnetStandalone = p.garnet_standalone;


    // These statistical variables are not for display.
    // The profiler will collate these across different
    // CXLSequencers and display those collated statistics.
    m_outstandReqHist.init(10);
    m_latencyHist.init(10);
    m_hitLatencyHist.init(10);
    m_missLatencyHist.init(10);

    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_typeLatencyHist.push_back(new statistics::Histogram());
        m_typeLatencyHist[i]->init(10);

        m_hitTypeLatencyHist.push_back(new statistics::Histogram());
        m_hitTypeLatencyHist[i]->init(10);

        m_missTypeLatencyHist.push_back(new statistics::Histogram());
        m_missTypeLatencyHist[i]->init(10);
    }

    for (int i = 0; i < MachineType_NUM; i++) {
        m_hitMachLatencyHist.push_back(new statistics::Histogram());
        m_hitMachLatencyHist[i]->init(10);

        m_missMachLatencyHist.push_back(new statistics::Histogram());
        m_missMachLatencyHist[i]->init(10);

        m_IssueToInitialDelayHist.push_back(new statistics::Histogram());
        m_IssueToInitialDelayHist[i]->init(10);

        m_InitialToForwardDelayHist.push_back(new statistics::Histogram());
        m_InitialToForwardDelayHist[i]->init(10);

        m_ForwardToFirstResponseDelayHist.push_back(
            new statistics::Histogram());
        m_ForwardToFirstResponseDelayHist[i]->init(10);

        m_FirstResponseToCompletionDelayHist.push_back(
            new statistics::Histogram());
        m_FirstResponseToCompletionDelayHist[i]->init(10);
    }

    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_hitTypeMachLatencyHist.push_back(
            std::vector<statistics::Histogram *>());
        m_missTypeMachLatencyHist.push_back(
            std::vector<statistics::Histogram *>());

        for (int j = 0; j < MachineType_NUM; j++) {
            m_hitTypeMachLatencyHist[i].push_back(new statistics::Histogram());
            m_hitTypeMachLatencyHist[i][j]->init(10);

            m_missTypeMachLatencyHist[i].push_back(
                new statistics::Histogram());
            m_missTypeMachLatencyHist[i][j]->init(10);
        }
    }

}

CXLSequencer::~CXLSequencer()
{
}



void
CXLSequencer::wakeup()
{
    // assert(drainState() != DrainState::Draining);

    // // Check for deadlock of any of the requests
    // Cycles current_time = curCycle();

    // // Check across all outstanding requests
    // [[maybe_unused]] int total_outstanding = 0;

    // for (const auto &table_entry : m_RequestTable) {
    //     for (const auto &seq_req : table_entry.second) {
    //         if (current_time - seq_req.issue_time < m_deadlock_threshold)
    //             continue;
    //         // 12-10 DEXTER EDITED FOR PANIC PASSING
    //         // panic("Possible Deadlock detected. Aborting!\n version: %d "
    //         //       "request.paddr: 0x%x m_readRequestTable: %d current time: "
    //         //       "%u issue_time: %d difference: %d\n", m_version,
    //         //       seq_req.pkt->getAddr(), table_entry.second.size(),
    //         //       current_time * clockPeriod(), seq_req.issue_time
    //         //       * clockPeriod(), (current_time * clockPeriod())
    //         //       - (seq_req.issue_time * clockPeriod()));
    //     }
    //     total_outstanding += table_entry.second.size();
    // }

    // DPRINTF(CXLSequencer, "m_outstanding_count = %d, total_outstanding %d\n",m_outstanding_count,total_outstanding);

    // assert(m_outstanding_count == total_outstanding);

    // if (m_outstanding_count > 0) {
    //     // If there are still outstanding requests, keep checking
    //     schedule(deadlockCheckEvent, clockEdge(m_deadlock_threshold));
    // }
}

int
CXLSequencer::functionalWrite(Packet *func_pkt)
{
    int num_written = RubyPort::functionalWrite(func_pkt);

    for (const auto &table_entry : m_RequestTable) {
        for (const auto& seq_req : table_entry.second) {
            if (seq_req.functionalWrite(func_pkt))
                ++num_written;
        }
    }

    return num_written;
}

void CXLSequencer::resetStats()
{
    m_outstandReqHist.reset();
    m_latencyHist.reset();
    m_hitLatencyHist.reset();
    m_missLatencyHist.reset();
    for (int i = 0; i < RubyRequestType_NUM; i++) {
        m_typeLatencyHist[i]->reset();
        m_hitTypeLatencyHist[i]->reset();
        m_missTypeLatencyHist[i]->reset();
        for (int j = 0; j < MachineType_NUM; j++) {
            m_hitTypeMachLatencyHist[i][j]->reset();
            m_missTypeMachLatencyHist[i][j]->reset();
        }
    }

    for (int i = 0; i < MachineType_NUM; i++) {
        m_missMachLatencyHist[i]->reset();
        m_hitMachLatencyHist[i]->reset();

        m_IssueToInitialDelayHist[i]->reset();
        m_InitialToForwardDelayHist[i]->reset();
        m_ForwardToFirstResponseDelayHist[i]->reset();
        m_FirstResponseToCompletionDelayHist[i]->reset();

        m_IncompleteTimes[i] = 0;
    }
}

// Insert the request in the request table. Return RequestStatus_Aliased
// if the entry was already present.
RequestStatus
CXLSequencer::insertRequest(PacketPtr pkt, RubyRequestType primary_type,
                        RubyRequestType secondary_type)
{
    // See if we should schedule a deadlock check
    if (!deadlockCheckEvent.scheduled() &&
        drainState() != DrainState::Draining) {
        schedule(deadlockCheckEvent, clockEdge(m_deadlock_threshold));
    }

    if (isTlbiCmdRequest(primary_type)) {
        assert(primary_type == secondary_type);

        switch (primary_type) {
        case RubyRequestType_TLBI_EXT_SYNC_COMP:
            // Don't have to store any data on this
            break;
        case RubyRequestType_TLBI:
        case RubyRequestType_TLBI_SYNC:
            {
                incrementUnaddressedTransactionCnt();

                // returns pair<inserted element, was inserted>
                [[maybe_unused]] auto insert_data = \
                    m_UnaddressedRequestTable.emplace(
                        getCurrentUnaddressedTransactionID(),
                        CXLSequencerRequest(
                            pkt, primary_type, secondary_type, curCycle()));

                // if insert_data.second is false, wasn't inserted
                assert(insert_data.second &&
                    "Another TLBI request with the same ID exists");

                DPRINTF(CXLSequencer, "Inserting TLBI request %016x\n",
                        getCurrentUnaddressedTransactionID());

                break;
            }

        default:
            panic("Unexpected TLBI RubyRequestType");
        }

        return RequestStatus_Ready;
    }

    // Addr line_addr = makeLineAddress(pkt->getAddr());
    // // Check if there is any outstanding request for the same cache line.
    // auto &seq_req_list = m_RequestTable[line_addr];
    // // Create a default entry
    // seq_req_list.emplace_back(pkt, primary_type,
    //     secondary_type, curCycle());

    //esj 2025-04-12
    // if(!pkt->cxl_pkt.is_controlflit)
    //     m_outstanding_count++;

    // if (seq_req_list.size() > 1) {
    //     return RequestStatus_Aliased;
    // }

    //esj 2025-04-12
    // m_outstandReqHist.sample(m_outstanding_count);

    return RequestStatus_Ready;
}

void
CXLSequencer::markRemoved()
{
    m_outstanding_count--;
}

void
CXLSequencer::recordMissLatency(CXLSequencerRequest* srequest, bool llscSuccess,
                            const MachineType respondingMach,
                            bool isExternalHit, Cycles initialRequestTime,
                            Cycles forwardRequestTime,
                            Cycles firstResponseTime)
{
    RubyRequestType type = srequest->m_type;
    Cycles issued_time = srequest->issue_time;
    Cycles completion_time = curCycle();

    assert(curCycle() >= issued_time);
    Cycles total_lat = completion_time - issued_time;

    if ((initialRequestTime != 0) && (initialRequestTime < issued_time)) {
        // if the request was combined in the protocol with an earlier request
        // for the same address, it is possible that it will return an
        // initialRequestTime corresponding the earlier request.  Since Cycles
        // is unsigned, we can't let this request get profiled below.

        total_lat = Cycles(0);
    }

    DPRINTFR(ProtocolTrace, "%15s %3s %10s%20s %6s>%-6s %s %d cycles\n",
            curTick(), m_version, "Seq", llscSuccess ? "Done" : "SC_Failed",
            "", "", printAddress(srequest->pkt->getAddr()), total_lat);

    m_latencyHist.sample(total_lat);
    m_typeLatencyHist[type]->sample(total_lat);

    if (isExternalHit) {
        m_missLatencyHist.sample(total_lat);
        m_missTypeLatencyHist[type]->sample(total_lat);

        if (respondingMach != MachineType_NUM) {
            m_missMachLatencyHist[respondingMach]->sample(total_lat);
            m_missTypeMachLatencyHist[type][respondingMach]->sample(total_lat);

            if ((issued_time <= initialRequestTime) &&
                (initialRequestTime <= forwardRequestTime) &&
                (forwardRequestTime <= firstResponseTime) &&
                (firstResponseTime <= completion_time)) {

                m_IssueToInitialDelayHist[respondingMach]->sample(
                    initialRequestTime - issued_time);
                m_InitialToForwardDelayHist[respondingMach]->sample(
                    forwardRequestTime - initialRequestTime);
                m_ForwardToFirstResponseDelayHist[respondingMach]->sample(
                    firstResponseTime - forwardRequestTime);
                m_FirstResponseToCompletionDelayHist[respondingMach]->sample(
                    completion_time - firstResponseTime);
            } else {
                m_IncompleteTimes[respondingMach]++;
            }
        }
    } else {
        m_hitLatencyHist.sample(total_lat);
        m_hitTypeLatencyHist[type]->sample(total_lat);

        if (respondingMach != MachineType_NUM) {
            m_hitMachLatencyHist[respondingMach]->sample(total_lat);
            m_hitTypeMachLatencyHist[type][respondingMach]->sample(total_lat);
        }
    }
}

// void
// CXLSequencer::writeCallbackScFail(Addr address, DataBlock& data)
// {
// //  llscClearMonitor(address);
//     writeCallback(address, data);
// }

//esj 2025-04-02
void
// CXLSequencer::writeCallback(Addr address, DataBlock& data,
//                         const bool externalHit, const MachineType mach,
//                         const Cycles initialRequestTime,
//                         const Cycles forwardRequestTime,
//                         const Cycles firstResponseTime,
//                         const bool noCoales)
CXLSequencer::writeCallback(PacketPtr pkt,
                        const bool externalHit, const MachineType mach,
                        const Cycles initialRequestTime,
                        const Cycles forwardRequestTime,
                        const Cycles firstResponseTime,
                        const bool noCoales)
{
    //
    // Free the whole list as we assume we have had the exclusive access
    // to this cache line when response for the write comes back
    //
    DPRINTF(CXLSequencer, "CXLSequencer writeCallback \n");
    DPRINTF(CXLSequencer,"%s, pkt addr = %x, cxl_flag =  %d, is response = %d, is write = %d\n",__func__,pkt->getAddr(),pkt->cxl_flag,pkt->isResponse(),pkt->isWrite());

    if(pkt->isWrite() && pkt->isResponse()){
        //esj 2025-04-12
        // if(!pkt->cxl_pkt.is_controlflit){
        //     markRemoved();
        // }
        hitCallback(RubyRequestType_ST, pkt, true, mach, externalHit,
            initialRequestTime, forwardRequestTime,
            firstResponseTime, false);
    }

    //esj 2025-04-11
    //esj 2025-05-11
    // m_RequestTable.erase(pkt->origin_dest);
    m_RequestTable.erase(pkt->origin_addr);
}

//esj 2025-04-02
void
// CXLSequencer::readCallback(Addr address, DataBlock& data,
//                         bool externalHit, const MachineType mach,
//                         Cycles initialRequestTime,
//                         Cycles forwardRequestTime,
//                         Cycles firstResponseTime)
CXLSequencer::readCallback(PacketPtr pkt,
                            bool externalHit, const MachineType mach,
                            Cycles initialRequestTime,
                            Cycles forwardRequestTime,
                            Cycles firstResponseTime)
{

    //
    // Free up read requests until we hit the first Write request
    // or end of the corresponding list.
    //
    DPRINTF(CXLSequencer, "CXLSequencer readCallback \n");

    if(pkt->isRead() && pkt->isResponse()){
        //esj 2025-04-12
        // if(!pkt->cxl_pkt.is_controlflit){
        //     markRemoved();
        // }
        hitCallback(RubyRequestType_LD, pkt, true, mach, externalHit,
            initialRequestTime, forwardRequestTime,
            firstResponseTime, false);
    }

    //esj 2025-04-11
    //esj 2025-05-11
    // m_RequestTable.erase(pkt->origin_dest);
    m_RequestTable.erase(pkt->origin_addr);

}

bool
CXLSequencer::isRetryResponse(PacketPtr pkt) const
{
    return pkt && pkt->cxl_pkt.retry_resp;
}

void
CXLSequencer::hitCallback(RubyRequestType type, PacketPtr pkt,
                    bool llscSuccess,
                    const MachineType mach, const bool externalHit,
                    const Cycles initialRequestTime,
                    const Cycles forwardRequestTime,
                    const Cycles firstResponseTime,
                    const bool was_coalesced)
{
    DPRINTF(CXLSequencer,"%s, type = %d, pkt addr = %x\n",__func__,type,pkt->getAddr());

    DPRINTF(CXLSequencer,"%s, response_ports size = %d\n",__func__,response_ports.size());
    for(int i=0; i<response_ports.size();i++){
        DPRINTF(CXLSequencer,"%s, response_ports[%d] = %s\n",__func__,i,response_ports[i]->name());
    }

    //esj 2025-04-09
    //esj 2025-07-21
    // if(pkt->senderState != NULL){
    if(!pkt->cxl_pkt.retry_resp && pkt->senderState != NULL){
        DPRINTF(CXLSequencer,"%s, pop senderstate\n",__func__);
        //esj 2025-07-21
        delete pkt->popSenderState();
        // SenderState *ss = safe_cast<SenderState *>(pkt->popSenderState());
        // delete ss;
    }
    DPRINTF(CXLSequencer,"%s, pkt->cxl_pkt.retry_resp = %d\n",__func__,pkt->cxl_pkt.retry_resp);

    DPRINTF(CXLSequencer,"%s, pkt addr = %x, cxl_flag =  %d, is response = %d, is write = %d\n",__func__,pkt->getAddr(),pkt->cxl_flag,pkt->isResponse(),pkt->isWrite());

    // esj 2025-05-19
    // response_ports[0]->schedTimingResp(pkt,curTick());
    Port& peer = response_ports[0]->getPeer();
    auto* req_port = dynamic_cast<gem5::RequestPort*>(&peer);
    Port& peer2 = response_ports[1]->getPeer();
    auto* req_port2 = dynamic_cast<gem5::RequestPort*>(&peer2);
    //esj 2025-06-22
    // if (req_port) {
    //     req_port->recvTimingResp(pkt);
    // }
    //
    DPRINTF(CXLSequencer,"%s, req_port name = %s\n",__func__,req_port->name());
    DPRINTF(CXLSequencer,"%s, req_port2 name = %s\n",__func__,req_port2->name());
    DPRINTF(CXLSequencer,"%s, response_ports[0] name = %s\n",__func__,response_ports[0]->name());
    DPRINTF(CXLSequencer,"%s, response_ports[1] name = %s\n",__func__,response_ports[1]->name());

    if(req_port2->name().find("request2_1") != std::string::npos){
        DPRINTF(CXLSequencer,"%s, req_port2 name = %s\n",__func__,req_port2->name());
        response_ports[1]->schedTimingResp(pkt,curTick());
    }
    else{
        DPRINTF(CXLSequencer,"%s, req_port name = %s\n",__func__,req_port->name());
        response_ports[0]->schedTimingResp(pkt,curTick());
    }

    // ruby_hit_callback(pkt);
    // testDrainComplete();
}


bool
CXLSequencer::empty() const
{
    return m_RequestTable.empty() &&
        m_UnaddressedRequestTable.empty();
}

RequestStatus
CXLSequencer::makeRequest(PacketPtr pkt)
{
    DPRINTF(CXLSequencer, "CXLSequencer::makeRequest(PacketPtr pkt) isWrite: %d\n",pkt->isWrite());
    DPRINTF(CXLSequencer, "m_outstanding_count/m_max_outstanding_requests: %d/%d\n",m_outstanding_count,m_max_outstanding_requests);
    // HTM abort signals must be allowed to reach the CXLSequencer
    // the same cycle they are issued. They cannot be retried.

    //esj 2025-04-12
    // if ((m_outstanding_count >= m_max_outstanding_requests) &&
    //     !pkt->req->isHTMAbort()) {
    //     return RequestStatus_BufferFull;
    // }

    RubyRequestType primary_type = RubyRequestType_NULL;
    RubyRequestType secondary_type = RubyRequestType_NULL;

    if (pkt->isLLSC()) {
        // LL/SC instructions need to be handled carefully by the cache
        // coherence protocol to ensure they follow the proper semantics. In
        // particular, by identifying the operations as atomic, the protocol
        // should understand that migratory sharing optimizations should not
        // be performed (i.e. a load between the LL and SC should not steal
        // away exclusive permission).
        //
        // The following logic works correctly with the semantics
        // of armV8 LDEX/STEX instructions.

        if (pkt->isWrite()) {
            DPRINTF(CXLSequencer, "Issuing SC\n");
            primary_type = RubyRequestType_Store_Conditional;
#if defined (PROTOCOL_MESI_Three_Level) || defined (PROTOCOL_MESI_Three_Level_HTM)
            secondary_type = RubyRequestType_Store_Conditional;
#else
            secondary_type = RubyRequestType_ST;
#endif
        } else {
            DPRINTF(CXLSequencer, "Issuing LL\n");
            assert(pkt->isRead());
            primary_type = RubyRequestType_Load_Linked;
            secondary_type = RubyRequestType_LD;
        }
    } else if (pkt->req->isLockedRMW()) {
        //
        // x86 locked instructions are translated to store cache coherence
        // requests because these requests should always be treated as read
        // exclusive operations and should leverage any migratory sharing
        // optimization built into the protocol.
        //
        if (pkt->isWrite()) {
            DPRINTF(CXLSequencer, "Issuing Locked RMW Write\n");
            primary_type = RubyRequestType_Locked_RMW_Write;
        } else {
            DPRINTF(CXLSequencer, "Issuing Locked RMW Read\n");
            assert(pkt->isRead());
            primary_type = RubyRequestType_Locked_RMW_Read;
        }
        secondary_type = RubyRequestType_ST;
    } else if (pkt->req->isTlbiCmd()) {
        primary_type = secondary_type = tlbiCmdToRubyRequestType(pkt);
        DPRINTF(CXLSequencer, "Issuing TLBI\n");
    } else {
        //
        // To support SwapReq, we need to check isWrite() first: a SwapReq
        // should always be treated like a write, but since a SwapReq implies
        // both isWrite() and isRead() are true, check isWrite() first here.
        //
        if (pkt->isWrite()) {
            //
            // Note: M5 packets do not differentiate ST from RMW_Write
            //
            DPRINTF(CXLSequencer, "pkt->isWrite()\n");

            primary_type = secondary_type = RubyRequestType_ST;
        } else if (pkt->isRead()) {
            // hardware transactional memory commands
            DPRINTF(CXLSequencer, "pkt->isRead()\n");
            if (pkt->req->isHTMCmd()) {
                primary_type = secondary_type = htmCmdToRubyRequestType(pkt);
            } else if (pkt->req->isInstFetch()) {
                primary_type = secondary_type = RubyRequestType_IFETCH;
            } else {
                if (pkt->req->isReadModifyWrite()) {
                    primary_type = RubyRequestType_RMW_Read;
                    secondary_type = RubyRequestType_ST;
                } else {
                    primary_type = secondary_type = RubyRequestType_LD;
                }
            }
        } else if (pkt->isFlush()) {
        primary_type = secondary_type = RubyRequestType_FLUSH;
        } else {
            panic("Unsupported ruby packet type\n");
        }
    }

    // Check if the line is blocked for a Locked_RMW
    if (!pkt->req->isMemMgmt() &&
        m_controller->isBlocked(makeLineAddress(pkt->getAddr())) &&
        (primary_type != RubyRequestType_Locked_RMW_Write)) {
        // Return that this request's cache line address aliases with
        // a prior request that locked the cache line. The request cannot
        // proceed until the cache line is unlocked by a Locked_RMW_Write
        return RequestStatus_Aliased;
    }

    //esj 2025-04-06
    if(pkt->cxl_pkt.is_controlflit){
        primary_type = RubyRequestType_CXL_CTRL_FLIT;
        secondary_type = RubyRequestType_CXL_CTRL_FLIT;
    }
    else if (pkt->cxl_pkt.retry_req) {
        // A link-level retry is the original outstanding CXL transaction,
        // not a second coherent request. Give the protocol a distinct event
        // so it can retransmit through the existing same-line TBE.
        if (pkt->isWrite()) {
            primary_type = RubyRequestType_CXL_RETRY_ST;
            secondary_type = RubyRequestType_CXL_RETRY_ST;
        } else {
            primary_type = RubyRequestType_CXL_RETRY_LD;
            secondary_type = RubyRequestType_CXL_RETRY_LD;
        }
    }

    //esj 2025-03-26
    DPRINTF(CXLSequencer, "primary_type = %s, secondary_type = %s \n",RubyRequestType_to_string(primary_type),RubyRequestType_to_string(secondary_type));


    RequestStatus status = insertRequest(pkt, primary_type, secondary_type);

    // It is OK to receive RequestStatus_Aliased, it can be considered Issued
    if (status != RequestStatus_Ready && status != RequestStatus_Aliased)
        return status;
    // non-aliased with any existing request in the request table, just issue
    // to the cache
    if (status != RequestStatus_Aliased)
        issueRequest(pkt, secondary_type);

    DPRINTF(CXLSequencer, "pkt status RequestStatus_Issued\n");

    // TODO: issue hardware prefetches here
    return RequestStatus_Issued;
}

void
CXLSequencer::issueRequest(PacketPtr pkt, RubyRequestType secondary_type)
{
    assert(pkt != NULL);
    ContextID proc_id = pkt->req->hasContextId() ?
        pkt->req->contextId() : InvalidContextID;

    ContextID core_id = coreId();

    // If valid, copy the pc to the ruby request
    Addr pc = 0;
    if (pkt->req->hasPC()) {
        pc = pkt->req->getPC();
    }

    // check if the packet has data as for example prefetch and flush
    // requests do not
    std::shared_ptr<RubyRequest> msg;
    if (pkt->req->isMemMgmt()) {
        msg = std::make_shared<RubyRequest>(clockEdge(),
                                            pc, secondary_type,
                                            RubyAccessMode_Supervisor, pkt,
                                            proc_id, core_id);

        DPRINTFR(ProtocolTrace, "%15s %3s %10s%20s %6s>%-6s %s\n",
                curTick(), m_version, "Seq", "Begin", "", "",
                RubyRequestType_to_string(secondary_type));

        if (pkt->req->isTlbiCmd()) {
            msg->m_isTlbi = true;
            switch (secondary_type) {
            case RubyRequestType_TLBI_EXT_SYNC_COMP:
                msg->m_tlbiTransactionUid = pkt->req->getExtraData();
                break;
            case RubyRequestType_TLBI:
            case RubyRequestType_TLBI_SYNC:
                msg->m_tlbiTransactionUid = \
                    getCurrentUnaddressedTransactionID();
                break;
            default:
                panic("Unexpected TLBI RubyRequestType");
            }
            DPRINTF(CXLSequencer, "Issuing TLBI %016x\n",
                    msg->m_tlbiTransactionUid);
        }
    } else {
        //esj 2025-04-11
        // msg = std::make_shared<RubyRequest>(clockEdge(), pkt->getAddr(),
        //esj 2025-05-11
        // msg = std::make_shared<RubyRequest>(clockEdge(), pkt->origin_dest,
        msg = std::make_shared<RubyRequest>(clockEdge(), pkt->origin_addr,
                                            pkt->getSize(), pc, secondary_type,
                                            RubyAccessMode_Supervisor, pkt,
                                            PrefetchBit_No, proc_id, core_id);

        DPRINTFR(ProtocolTrace, "%15s %3s %10s%20s %6s>%-6s %#x %s\n",
                curTick(), m_version, "Seq", "Begin", "", "",
                printAddress(msg->getPhysicalAddress()),
                RubyRequestType_to_string(secondary_type));
    }

    // hardware transactional memory
    // If the request originates in a transaction,
    // then mark the Ruby message as such.
    if (pkt->isHtmTransactional()) {
        msg->m_htmFromTransaction = true;
        msg->m_htmTransactionUid = pkt->getHtmTransactionUid();
    }

    Tick latency = cyclesToTicks(
                        m_controller->mandatoryQueueLatency(secondary_type));
    assert(latency > 0);

    assert(m_mandatory_q_ptr != NULL);
    m_mandatory_q_ptr->enqueue(msg, clockEdge(), latency);
}

template <class KEY, class VALUE>
std::ostream &
operator<<(std::ostream &out, const std::unordered_map<KEY, VALUE> &map)
{
    for (const auto &table_entry : map) {
        out << "[ " << table_entry.first << " =";
        for (const auto &seq_req : table_entry.second) {
            out << " " << RubyRequestType_to_string(seq_req.m_second_type);
        }
    }
    out << " ]";

    return out;
}

void
CXLSequencer::print(std::ostream& out) const
{
    out << "[CXLSequencer: " << m_version
        << ", outstanding requests: " << m_outstanding_count
        << ", request table: " << m_RequestTable
        << "]";
}

void
CXLSequencer::recordRequestType(SequencerRequestType requestType) {
    DPRINTF(RubyStats, "Recorded statistic: %s\n",
            SequencerRequestType_to_string(requestType));
}

void
CXLSequencer::evictionCallback(Addr address)
{
//  llscClearMonitor(address);
    ruby_eviction_callback(address);
}

void
CXLSequencer::incrementUnaddressedTransactionCnt()
{
    m_unaddressedTransactionCnt++;
    // Limit m_unaddressedTransactionCnt to 32 bits,
    // top 32 bits should always be zeroed out
    uint64_t aligned_txid = \
        m_unaddressedTransactionCnt << RubySystem::getBlockSizeBits();

    if (aligned_txid > 0xFFFFFFFFull) {
        m_unaddressedTransactionCnt = 0;
    }
}

uint64_t
CXLSequencer::getCurrentUnaddressedTransactionID() const
{
    return (
        uint64_t(m_version & 0xFFFFFFFF) << 32) |
        (m_unaddressedTransactionCnt << RubySystem::getBlockSizeBits()
    );
}

} // namespace ruby
} // namespace gem5
