/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
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


 #include "mem/ruby/network/garnet/NetworkInterface.hh"

 #include <cassert>
 #include <cmath>

 #include "base/cast.hh"
 #include "debug/RubyNetwork.hh"
 #include "mem/ruby/network/MessageBuffer.hh"
 #include "mem/ruby/network/garnet/Credit.hh"
 #include "mem/ruby/network/garnet/flitBuffer.hh"
 #include "mem/ruby/slicc_interface/Message.hh"
 #include "sim/protocol_validation_logger.hh"

 namespace gem5
 {

 namespace ruby
 {

 namespace garnet
 {

 NetworkInterface::NetworkInterface(const Params &p)
   : ClockedObject(p), Consumer(this), m_id(p.id),
     m_virtual_networks(p.virt_nets), m_vc_per_vnet(0),
     m_vc_allocator(m_virtual_networks, 0),
     m_deadlock_threshold(p.garnet_deadlock_threshold),
     vc_busy_counter(m_virtual_networks, 0),
     m_cpu_num(p.cpu_num), //esj 2025-09-05
     m_dir_num(p.dir_num) //esj 2025-09-05
 {
     m_stall_count.resize(m_virtual_networks);
     m_validation_vc_alloc_waits.resize(m_virtual_networks);
     niOutVcs.resize(0);
 }

 void
 NetworkInterface::addInPort(NetworkLink *in_link,
                               CreditLink *credit_link)
 {
     InputPort *newInPort = new InputPort(in_link, credit_link);
     inPorts.push_back(newInPort);
     DPRINTF(RubyNetwork, "Adding input port:%s with vnets %s\n",
     in_link->name(), newInPort->printVnets());

     in_link->setLinkConsumer(this);
     credit_link->setSourceQueue(newInPort->outCreditQueue(), this);
     if (m_vc_per_vnet != 0) {
         in_link->setVcsPerVnet(m_vc_per_vnet);
         credit_link->setVcsPerVnet(m_vc_per_vnet);
     }

 }

 void
 NetworkInterface::addOutPort(NetworkLink *out_link,
                              CreditLink *credit_link,
                              SwitchID router_id, uint32_t consumerVcs)
 {
     OutputPort *newOutPort = new OutputPort(out_link, credit_link, router_id);
     outPorts.push_back(newOutPort);

     assert(consumerVcs > 0);
     // We are not allowing different physical links to have different vcs
     // If it is required that the Network Interface support different VCs
     // for every physical link connected to it. Then they need to change
     // the logic within outport and inport.
     if (niOutVcs.size() == 0) {
         m_vc_per_vnet = consumerVcs;
         int m_num_vcs = consumerVcs * m_virtual_networks;
         niOutVcs.resize(m_num_vcs);
         outVcState.reserve(m_num_vcs);
         m_ni_out_vcs_enqueue_time.resize(m_num_vcs);
         m_validation_credit_flits.resize(m_num_vcs);
         m_validation_credit_stalled.assign(m_num_vcs, false);
         m_validation_credit_stall_start.assign(m_num_vcs, 0);
         m_validation_stall_flits.resize(m_num_vcs);
         m_validation_free_credit_witnesses.resize(m_num_vcs);
         // instantiating the NI flit buffers
         for (int i = 0; i < m_num_vcs; i++) {
             m_ni_out_vcs_enqueue_time[i] = Tick(INFINITE_);
             outVcState.emplace_back(i, m_net_ptr, consumerVcs);
         }

         // Reset VC Per VNET for input links already instantiated
         for (auto &iPort: inPorts) {
             NetworkLink *inNetLink = iPort->inNetLink();
             inNetLink->setVcsPerVnet(m_vc_per_vnet);
             credit_link->setVcsPerVnet(m_vc_per_vnet);
         }
     } else {
         fatal_if(consumerVcs != m_vc_per_vnet,
         "%s: Connected Physical links have different vc requests: %d and %d\n",
         name(), consumerVcs, m_vc_per_vnet);
     }

     DPRINTF(RubyNetwork, "OutputPort:%s Vnet: %s\n",
     out_link->name(), newOutPort->printVnets());

     out_link->setSourceQueue(newOutPort->outFlitQueue(), this);
     out_link->setVcsPerVnet(m_vc_per_vnet);
     credit_link->setLinkConsumer(this);
     credit_link->setVcsPerVnet(m_vc_per_vnet);
 }

 void
 NetworkInterface::addNode(std::vector<MessageBuffer *>& in,
                           std::vector<MessageBuffer *>& out)
 {
     inNode_ptr = in;
     outNode_ptr = out;

     for (auto& it : in) {
         if (it != nullptr) {
             it->setConsumer(this);
         }
     }
 }

 void
 NetworkInterface::dequeueCallback()
 {
     // An output MessageBuffer has dequeued something this cycle and there
     // is now space to enqueue a stalled message. However, we cannot wake
     // on the same cycle as the dequeue. Schedule a wake at the soonest
     // possible time (next cycle).
     scheduleEventAbsolute(clockEdge(Cycles(1)));
 }

 void
 NetworkInterface::incrementStats(flit *t_flit)
 {
     int vnet = t_flit->get_vnet();

     // Latency
     m_net_ptr->increment_received_flits(vnet);
     Tick network_delay =
         t_flit->get_dequeue_time() -
         t_flit->get_enqueue_time() - cyclesToTicks(Cycles(1));
     Tick src_queueing_delay = t_flit->get_src_delay();
     Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
     Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

     m_net_ptr->increment_flit_network_latency(network_delay, vnet);
     m_net_ptr->increment_flit_queueing_latency(queueing_delay, vnet);

     if (t_flit->get_type() == TAIL_ || t_flit->get_type() == HEAD_TAIL_) {
         m_net_ptr->increment_received_packets(vnet);
         m_net_ptr->increment_packet_network_latency(network_delay, vnet);
         m_net_ptr->increment_packet_queueing_latency(queueing_delay, vnet);
     }

     // Hops
     m_net_ptr->increment_total_hops(t_flit->get_route().hops_traversed);
 }

 NetworkInterface::ValidationFlitInfo
 NetworkInterface::validationMessageInfo(MsgPtr msg_ptr, int vnet) const
 {
     ValidationFlitInfo info;
     if (!msg_ptr)
         return info;

     PacketPtr pkt = msg_ptr->getPacketPtr();
     if (!pkt || !pkt->cxl_flag)
         return info;

     info.isCxl = true;
     info.transactionId = ProtocolValidationLogger::transactionId(pkt);
     info.packetSeq = static_cast<int64_t>(pkt->cxl_pkt.seqNum);
     info.txAttempt =
         (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp ||
          pkt->cxl_pkt.is_retry_req || pkt->cxl_pkt.is_retry_resp) ? 1 : 0;
     info.vnet = vnet;
     info.address = pkt->getAddr();
     info.direction = pkt->cxl_pkt.is_from_device ? "D2H" : "H2D";
     info.command = pkt->cmdString();
     info.isControl = pkt->cxl_pkt.is_controlflit;
     info.crcOk = pkt->cxl_pkt.crc_check;
     return info;
 }

 NetworkInterface::ValidationFlitInfo
 NetworkInterface::validationFlitInfo(flit *t_flit) const
 {
     if (!t_flit)
         return ValidationFlitInfo();

     ValidationFlitInfo info = validationMessageInfo(
         t_flit->get_msg_ptr(), t_flit->get_vnet());
     if (!info.isCxl)
         return info;

     info.flitIndex = t_flit->get_id();
     info.garnetPacketId = t_flit->getPacketID();
     return info;
 }

 void
 NetworkInterface::recordValidationGarnetEvent(
     const char *event_name, const ValidationFlitInfo &info,
     OutputPort *oPort, int vc, int credits_before, int credits_after,
     const char *reason, const char *stall_reason,
     std::optional<bool> free_signal, std::optional<bool> vc_idle)
 {
     if (!ProtocolValidationLogger::enabled() || !info.isCxl)
         return;

     ProtocolValidationEvent event;
     event.event = event_name;
     event.component = name();
     event.layer = "GARNET";
     event.direction = info.direction;
     event.transactionId = info.transactionId;
     event.packetSeq = info.packetSeq;
     event.flitIndex = info.flitIndex;
     event.txAttempt = info.txAttempt;
     event.vnet = info.vnet;
     event.vc = vc;
     event.routerId = oPort->routerID();
     event.outport = oPort->outNetLink()->name();
     event.creditsBefore = credits_before;
     event.creditsAfter = credits_after;
     event.garnetPacketId = info.garnetPacketId;
     event.address = info.address;
     event.command = info.command;
     event.isControl = info.isControl;
     event.crcOk = info.crcOk;
     event.freeSignal = free_signal;
     event.vcIdle = vc_idle;
     if (event.event == "GARNET_FLIT_LAUNCH")
         event.pathStage = "GARNET";
     if (reason)
         event.reason = reason;
     if (stall_reason)
         event.stallReason = stall_reason;
     ProtocolValidationLogger::record(event);
 }

 NetworkInterface::ValidationVcStateSnapshot
 NetworkInterface::validationVcStateSnapshot(int vnet)
 {
     ValidationVcStateSnapshot snapshot;
     if (vnet < 0 || vnet >= m_virtual_networks || m_vc_per_vnet <= 0)
         return snapshot;

     snapshot.eligible = m_vc_per_vnet;
     const int vc_base = vnet * m_vc_per_vnet;
     for (int offset = 0; offset < m_vc_per_vnet; ++offset) {
         const int vc = vc_base + offset;
         if (outVcState[vc].isInState(ACTIVE_, curTick()))
             ++snapshot.active;
         if (outVcState[vc].isInState(IDLE_, curTick()))
             ++snapshot.idle;
     }
     return snapshot;
 }

 void
 NetworkInterface::recordValidationVcAllocWaitEvent(
     const char *event_name, const ValidationFlitInfo &info,
     OutputPort *oPort, int vc, const ValidationVcStateSnapshot &snapshot,
     Tick wait_ticks, const ValidationFreeCreditWitness &witness,
     const char *reason)
 {
     if (!ProtocolValidationLogger::enabled() || !info.isCxl || !oPort)
         return;

     ProtocolValidationEvent event;
     event.event = event_name;
     event.component = name();
     event.layer = "GARNET";
     event.direction = info.direction;
     event.transactionId = info.transactionId;
     event.packetSeq = info.packetSeq;
     event.txAttempt = info.txAttempt;
     event.vnet = info.vnet;
     event.vc = vc;
     event.routerId = oPort->routerID();
     event.outport = oPort->outNetLink()->name();
     event.address = info.address;
     event.command = info.command;
     event.isControl = info.isControl;
     event.crcOk = info.crcOk;
     event.eligibleVcCount = snapshot.eligible;
     event.activeVcCount = snapshot.active;
     event.idleVcCount = snapshot.idle;
     event.waitTicks = wait_ticks;
     event.freeSignal = witness.valid;
     event.vcIdle = vc >= 0 &&
         outVcState[vc].isInState(IDLE_, curTick());
     if (witness.valid) {
         event.creditsBefore = witness.creditsBefore;
         event.creditsAfter = witness.creditsAfter;
     }
     event.stallReason = "GARNET_VC_ALLOCATION";
     event.reason = reason;
     ProtocolValidationLogger::record(event);
 }

 void
 NetworkInterface::beginValidationVcAllocWait(
     MsgPtr msg_ptr, int vnet, OutputPort *oPort)
 {
     if (!ProtocolValidationLogger::enabled() ||
         vnet < 0 || vnet >= m_virtual_networks)
         return;

     ValidationFlitInfo info = validationMessageInfo(msg_ptr, vnet);
     if (!info.isCxl)
         return;

     ValidationVcAllocWait &wait = m_validation_vc_alloc_waits[vnet];
     if (wait.active)
         return;

     const ValidationVcStateSnapshot snapshot =
         validationVcStateSnapshot(vnet);
     if (snapshot.eligible <= 0 ||
         snapshot.active != snapshot.eligible || snapshot.idle != 0)
         return;

     wait.active = true;
     wait.startTick = curTick();
     wait.messagePtr = msg_ptr.get();
     wait.message = info;
     recordValidationVcAllocWaitEvent(
         "VC_ALLOC_WAIT_BEGIN", info, oPort, -1, snapshot, 0,
         ValidationFreeCreditWitness(), "ALL_ELIGIBLE_VCS_ACTIVE");
 }

 void
 NetworkInterface::endValidationVcAllocWait(
     MsgPtr msg_ptr, int vnet, OutputPort *oPort, int vc)
 {
     if (!ProtocolValidationLogger::enabled() ||
         vnet < 0 || vnet >= m_virtual_networks || vc < 0 ||
         static_cast<size_t>(vc) >= m_validation_free_credit_witnesses.size())
         return;

     ValidationVcAllocWait &wait = m_validation_vc_alloc_waits[vnet];
     if (!wait.active || wait.messagePtr != msg_ptr.get())
         return;

     const ValidationVcStateSnapshot snapshot =
         validationVcStateSnapshot(vnet);
     const ValidationFreeCreditWitness &last_witness =
         m_validation_free_credit_witnesses[vc];
     ValidationFreeCreditWitness witness;
     if (last_witness.valid && last_witness.tick >= wait.startTick)
         witness = last_witness;

     recordValidationVcAllocWaitEvent(
         "VC_ALLOC_WAIT_END", wait.message, oPort, vc, snapshot,
         curTick() - wait.startTick, witness,
         witness.valid ? "IDLE_VC_ALLOCATED_AFTER_FREE_CREDIT" :
             "IDLE_VC_ALLOCATED_WITHOUT_RECORDED_FREE_CREDIT");
     wait = ValidationVcAllocWait();
 }

 void
 NetworkInterface::beginValidationCreditStall(OutputPort *oPort, int vc)
 {
     if (vc < 0 ||
         static_cast<size_t>(vc) >= m_validation_credit_stalled.size() ||
         m_validation_credit_stalled[vc] ||
         !outVcState[vc].isInState(ACTIVE_, curTick()) ||
         !niOutVcs[vc].isReady(curTick()) ||
         outVcState[vc].has_credit()) {
         return;
     }

     ValidationFlitInfo info =
         validationFlitInfo(niOutVcs[vc].peekTopFlit());
     if (!info.isCxl)
         return;

     m_validation_credit_stalled[vc] = true;
     m_validation_credit_stall_start[vc] = curTick();
     m_validation_stall_flits[vc] = info;
     m_net_ptr->beginCxlCreditStall();
     recordValidationGarnetEvent(
         "CREDIT_STALL_BEGIN", info, oPort, vc, 0, 0,
         "ALLOCATED_VC_READY_FLIT_ZERO_CREDIT", "GARNET_CREDIT");
 }

 void
 NetworkInterface::endValidationCreditStall(
     OutputPort *oPort, int vc, int credits_before, int credits_after)
 {
     if (vc < 0 ||
         static_cast<size_t>(vc) >= m_validation_credit_stalled.size() ||
         !m_validation_credit_stalled[vc] || credits_before != 0 ||
         credits_after <= 0) {
         return;
     }

     recordValidationGarnetEvent(
         "CREDIT_STALL_END", m_validation_stall_flits[vc], oPort, vc,
         credits_before, credits_after, "CREDIT_RETURN",
         "GARNET_CREDIT");
     m_net_ptr->endCxlCreditStall(
         curTick() - m_validation_credit_stall_start[vc]);
     m_validation_credit_stalled[vc] = false;
     m_validation_credit_stall_start[vc] = 0;
     m_validation_stall_flits[vc] = ValidationFlitInfo();
 }

 unsigned int
 NetworkInterface::rebaseCxlCreditStallsForStatsReset()
 {
     unsigned int active = 0;
     for (size_t vc = 0; vc < m_validation_credit_stalled.size(); ++vc) {
         if (!m_validation_credit_stalled[vc])
             continue;
         m_validation_credit_stall_start[vc] = curTick();
         ++active;
     }
     return active;
 }

 /*
  * The NI wakeup checks whether there are any ready messages in the protocol
  * buffer. If yes, it picks that up, flitisizes it into a number of flits and
  * puts it into an output buffer and schedules the output link. On a wakeup
  * it also checks whether there are flits in the input link. If yes, it picks
  * them up and if the flit is a tail, the NI inserts the corresponding message
  * into the protocol buffer. It also checks for credits being sent by the
  * downstream router.
  */

 void
 NetworkInterface::wakeup()
 {
     std::ostringstream oss;
     for (auto &oPort: outPorts) {
         oss << oPort->routerID() << "[" << oPort->printVnets() << "] ";
     }
     DPRINTF(RubyNetwork, "Network Interface %d connected to router:%s "
             "woke up. Period: %ld, inNode_ptr.size() = %d\n", m_id, oss.str(), clockPeriod(),inNode_ptr.size());

     assert(curTick() == clockEdge());
     MsgPtr msg_ptr;
     Tick curTime = clockEdge();

     // Checking for messages coming from the protocol
     // can pick up a message/cycle for each virtual net
     for (int vnet = 0; vnet < inNode_ptr.size(); ++vnet) {
         MessageBuffer *b = inNode_ptr[vnet];
         if (b == nullptr) {
             continue;
         }

         if (b->isReady(curTime)) { // Is there a message waiting
             msg_ptr = b->peekMsgPtr();
             if (flitisizeMessage(msg_ptr, vnet)) {
                 b->dequeue(curTime);
             }
         }
     }

     scheduleOutputLink();

     // Check if there are flits stalling a virtual channel. Track if a
     // message is enqueued to restrict ejection to one message per cycle.
     checkStallQueue();

     /*********** Check the incoming flit link **********/
     DPRINTF(RubyNetwork, "Number of input ports: %d\n", inPorts.size());
     for (auto &iPort: inPorts) {
         NetworkLink *inNetLink = iPort->inNetLink();
         if (inNetLink->isReady(curTick())) {
             flit *t_flit = inNetLink->consumeLink();
             DPRINTF(RubyNetwork, "Recieved flit:%s\n", *t_flit);
             assert(t_flit->m_width == iPort->bitWidth());

             int vnet = t_flit->get_vnet();
             t_flit->set_dequeue_time(curTick());

             // If a tail flit is received, enqueue into the protocol buffers
             // if space is available. Otherwise, exchange non-tail flits for
             // credits.
             if (t_flit->get_type() == TAIL_ ||
                 t_flit->get_type() == HEAD_TAIL_) {
                 if (!iPort->messageEnqueuedThisCycle &&
                     outNode_ptr[vnet]->areNSlotsAvailable(1, curTime)) {
                     // Space is available. Enqueue to protocol buffer.
                     outNode_ptr[vnet]->enqueue(t_flit->get_msg_ptr(), curTime,
                                                cyclesToTicks(Cycles(1)));

                     // Simply send a credit back since we are not buffering
                     // this flit in the NI
                     Credit *cFlit = new Credit(t_flit->get_vc(),
                                                true, curTick());
                     iPort->sendCredit(cFlit);
                     // Update stats and delete flit pointer
                     incrementStats(t_flit);
                     delete t_flit;
                 } else {
                     // No space available- Place tail flit in stall queue and
                     // set up a callback for when protocol buffer is dequeued.
                     // Stat update and flit pointer deletion will occur upon
                     // unstall.
                     iPort->m_stall_queue.push_back(t_flit);
                     m_stall_count[vnet]++;

                     outNode_ptr[vnet]->registerDequeueCallback([this]() {
                         dequeueCallback(); });
                 }
             } else {
                 // Non-tail flit. Send back a credit but not VC free signal.
                 Credit *cFlit = new Credit(t_flit->get_vc(), false,
                                                curTick());
                 // Simply send a credit back since we are not buffering
                 // this flit in the NI
                 iPort->sendCredit(cFlit);

                 // Update stats and delete flit pointer.
                 incrementStats(t_flit);
                 delete t_flit;
             }
         }
     }

     /****************** Check the incoming credit link *******/

     for (auto &oPort: outPorts) {
         CreditLink *inCreditLink = oPort->inCreditLink();
         if (inCreditLink->isReady(curTick())) {
             Credit *t_credit = (Credit*) inCreditLink->consumeLink();
             const int vc = t_credit->get_vc();
             const int credits_before =
                 outVcState[vc].get_credit_count();
             ValidationFlitInfo credit_info;
             if (ProtocolValidationLogger::enabled() &&
                 static_cast<size_t>(vc) <
                     m_validation_credit_flits.size() &&
                 !m_validation_credit_flits[vc].empty()) {
                 credit_info = m_validation_credit_flits[vc].front();
                 m_validation_credit_flits[vc].pop_front();
             }

             outVcState[vc].increment_credit();
             const int credits_after =
                 outVcState[vc].get_credit_count();
             const bool free_signal = t_credit->is_free_signal();
             if (free_signal) {
                 outVcState[vc].setState(IDLE_, curTick());
                 ValidationFreeCreditWitness &witness =
                     m_validation_free_credit_witnesses[vc];
                 witness.valid = true;
                 witness.tick = curTick();
                 witness.creditsBefore = credits_before;
                 witness.creditsAfter = credits_after;
             }

             const int vnet = get_vnet(vc);
             if (!credit_info.isCxl &&
                 vnet >= 0 && vnet < m_virtual_networks &&
                 m_validation_vc_alloc_waits[vnet].active) {
                 credit_info = m_validation_vc_alloc_waits[vnet].message;
             }
             recordValidationGarnetEvent(
                 "CREDIT_UPDATE", credit_info, oPort, vc, credits_before,
                 credits_after,
                 free_signal ? "NI_TAIL_FREE_CREDIT_RETURN" :
                     "NI_CREDIT_RETURN_INCREMENT",
                 nullptr, free_signal,
                 outVcState[vc].isInState(IDLE_, curTick()));
             endValidationCreditStall(
                 oPort, vc, credits_before, credits_after);
             delete t_credit;
         }
     }


     // It is possible to enqueue multiple outgoing credit flits if a message
     // was unstalled in the same cycle as a new message arrives. In this
     // case, we should schedule another wakeup to ensure the credit is sent
     // back.
     for (auto &iPort: inPorts) {
         if (iPort->outCreditQueue()->getSize() > 0) {
             DPRINTF(RubyNetwork, "Sending a credit %s via %s at %ld\n",
             *(iPort->outCreditQueue()->peekTopFlit()),
             iPort->outCreditLink()->name(), clockEdge(Cycles(1)));
             iPort->outCreditLink()->
                 scheduleEventAbsolute(clockEdge(Cycles(1)));
         }
     }
     checkReschedule();
 }

 void
 NetworkInterface::checkStallQueue()
 {
     // Check all stall queues.
     // There is one stall queue for each input link
     for (auto &iPort: inPorts) {
         iPort->messageEnqueuedThisCycle = false;
         Tick curTime = clockEdge();

         if (!iPort->m_stall_queue.empty()) {
             for (auto stallIter = iPort->m_stall_queue.begin();
                  stallIter != iPort->m_stall_queue.end(); ) {
                 flit *stallFlit = *stallIter;
                 int vnet = stallFlit->get_vnet();

                 // If we can now eject to the protocol buffer,
                 // send back credits
                 if (outNode_ptr[vnet]->areNSlotsAvailable(1,
                     curTime)) {
                     outNode_ptr[vnet]->enqueue(stallFlit->get_msg_ptr(),
                         curTime, cyclesToTicks(Cycles(1)));

                     // Send back a credit with free signal now that the
                     // VC is no longer stalled.
                     Credit *cFlit = new Credit(stallFlit->get_vc(), true,
                                                    curTick());
                     iPort->sendCredit(cFlit);

                     // Update Stats
                     incrementStats(stallFlit);

                     // Flit can now safely be deleted and removed from stall
                     // queue
                     delete stallFlit;
                     iPort->m_stall_queue.erase(stallIter);
                     m_stall_count[vnet]--;

                     // If there are no more stalled messages for this vnet, the
                     // callback on it's MessageBuffer is not needed.
                     if (m_stall_count[vnet] == 0)
                         outNode_ptr[vnet]->unregisterDequeueCallback();

                     iPort->messageEnqueuedThisCycle = true;
                     break;
                 } else {
                     ++stallIter;
                 }
             }
         }
     }
 }

 // Embed the protocol message into flits
 bool
 NetworkInterface::flitisizeMessage(MsgPtr msg_ptr, int vnet)
 {
    Message *net_msg_ptr = msg_ptr.get();
    NetDest net_msg_dest = net_msg_ptr->getDestination();

    // gets all the destinations associated with this message.
    std::vector<NodeID> dest_nodes = net_msg_dest.getAllDest();

    // Number of flits is dependent on the link bandwidth available.
    // This is expressed in terms of bytes/cycle or the flit size
    OutputPort *oPort = getOutportForVnet(vnet);
    DPRINTF(RubyNetwork,"vnet = %d, oPort router id = %d\n",vnet,oPort->routerID());



    assert(oPort);

    int num_flits = (int)divCeil((float) m_net_ptr->messageSizeTypeToBytes(
        net_msg_ptr->getMessageSize()), (float)oPort->bitWidth());

    DPRINTF(RubyNetwork, "Message Size:%d vnet:%d bitWidth:%d\n",
        m_net_ptr->messageSizeTypeToBytes(net_msg_ptr->getMessageSize()),
        vnet, oPort->bitWidth());

    // loop to convert all multicast messages into unicast messages
    DPRINTF(RubyNetwork,"esj dest_nodes size = %d\n",dest_nodes.size());

    for (int ctr = 0; ctr < dest_nodes.size(); ctr++) {

        // this will return a free output virtual channel
        int vc = calculateVC(vnet);

        if (vc == -1) {
            beginValidationVcAllocWait(msg_ptr, vnet, oPort);
            return false ;
        }
        endValidationVcAllocWait(msg_ptr, vnet, oPort, vc);
        MsgPtr new_msg_ptr = msg_ptr->clone();
        NodeID destID = dest_nodes[ctr];

        //esj 2025-03-26 //2025-03-30
        DPRINTF(RubyNetwork,"esj destID = %d, src id = %d\n",destID,m_id);
        DPRINTF(RubyNetwork,"esj owner name = %s\n",name());
        if(name().find("cxl") != std::string::npos){


            int index = m_cpu_num + m_dir_num + 3;

            if(destID == index){
                destID = 0;
            }
            else{
                destID = 1;
            }
            DPRINTF(RubyNetwork,"esj m_cpu_num = %d, m_dir_num = %d, index = %d, destID = %d\n",m_cpu_num,m_dir_num,index,destID);

            switch (destID)
            {
            case 4:
                destID = 1;
                break;
            case 3:
                destID = 0;
                break;
            //esj 2025-06-14
            case 7:
                destID = 1;
                break;
            case 6:
                destID = 0;
                break;

            case 9:
                destID = 1;
                break;
            case 8:
                destID = 0;
                break;
            default:
                break;
            }

            DPRINTF(RubyNetwork,"%s, changed destID = %d, \n",__func__,destID);
        }

        Message *new_net_msg_ptr = new_msg_ptr.get();
        if (dest_nodes.size() > 1) {
            NetDest personal_dest;
            for (int m = 0; m < (int) MachineType_NUM; m++) {
                if ((destID >= MachineType_base_number((MachineType) m)) &&
                    destID < MachineType_base_number((MachineType) (m+1))) {
                    // calculating the NetDest associated with this destID
                    personal_dest.clear();
                    personal_dest.add((MachineID) {(MachineType) m, (destID -
                        MachineType_base_number((MachineType) m))});
                    new_net_msg_ptr->getDestination() = personal_dest;
                    break;
                }
            }
            net_msg_dest.removeNetDest(personal_dest);
            // removing the destination from the original message to reflect
            // that a message with this particular destination has been
            // flitisized and an output vc is acquired
            net_msg_ptr->getDestination().removeNetDest(personal_dest);
        }

        // Embed Route into the flits
        // NetDest format is used by the routing table
        // Custom routing algorithms just need destID

        RouteInfo route;
        route.vnet = vnet;
        route.net_dest = new_net_msg_ptr->getDestination();
        route.src_ni = m_id;
        route.src_router = oPort->routerID();
        route.dest_ni = destID;
        route.dest_router = m_net_ptr->get_router_id(destID, vnet);

        DPRINTF(RubyNetwork,"%s, route.dest_router = %d\n",__func__,route.dest_router);

        // initialize hops_traversed to -1
        // so that the first router increments it to 0
        route.hops_traversed = -1;

        m_net_ptr->increment_injected_packets(vnet);
        m_net_ptr->update_traffic_distribution(route);
        int packet_id = m_net_ptr->getNextPacketID();
        for (int i = 0; i < num_flits; i++) {
            m_net_ptr->increment_injected_flits(vnet);
            flit *fl = new flit(packet_id,
                i, vc, vnet, route, num_flits, new_msg_ptr,
                m_net_ptr->messageSizeTypeToBytes(
                net_msg_ptr->getMessageSize()),
                oPort->bitWidth(), curTick());

            fl->set_src_delay(curTick() - msg_ptr->getTime());
            niOutVcs[vc].insert(fl);
        }

        m_ni_out_vcs_enqueue_time[vc] = curTick();
        outVcState[vc].setState(ACTIVE_, curTick());
    }
    return true ;
 }

 // Looking for a free output vc
 int
 NetworkInterface::calculateVC(int vnet)
 {
     for (int i = 0; i < m_vc_per_vnet; i++) {
         int delta = m_vc_allocator[vnet];
         m_vc_allocator[vnet]++;
         if (m_vc_allocator[vnet] == m_vc_per_vnet)
             m_vc_allocator[vnet] = 0;

         if (outVcState[(vnet*m_vc_per_vnet) + delta].isInState(
                     IDLE_, curTick())) {
             vc_busy_counter[vnet] = 0;
             return ((vnet*m_vc_per_vnet) + delta);
         }
     }

     vc_busy_counter[vnet] += 1;
     panic_if(vc_busy_counter[vnet] > m_deadlock_threshold,
         "%s: Possible network deadlock in vnet: %d at time: %llu \n",
         name(), vnet, curTick());

     return -1;
 }

 void
 NetworkInterface::scheduleOutputPort(OutputPort *oPort)
 {
    int vc = oPort->vcRoundRobin();

    for (int i = 0; i < niOutVcs.size(); i++) {
        vc++;
        if (vc == niOutVcs.size())
            vc = 0;

        int t_vnet = get_vnet(vc);
        if (oPort->isVnetSupported(t_vnet)) {
            const bool ready = niOutVcs[vc].isReady(curTick());
            if (ready && !outVcState[vc].has_credit()) {
                beginValidationCreditStall(oPort, vc);
                continue;
            }

            // model buffer backpressure
            if (ready && outVcState[vc].has_credit()) {

                bool is_candidate_vc = true;
                int vc_base = t_vnet * m_vc_per_vnet;

                if (m_net_ptr->isVNetOrdered(t_vnet)) {
                    for (int vc_offset = 0; vc_offset < m_vc_per_vnet;
                         vc_offset++) {
                        int t_vc = vc_base + vc_offset;
                        if (niOutVcs[t_vc].isReady(curTick())) {
                            if (m_ni_out_vcs_enqueue_time[t_vc] <
                                m_ni_out_vcs_enqueue_time[vc]) {
                                is_candidate_vc = false;
                                break;
                            }
                        }
                    }
                }
                if (!is_candidate_vc)
                    continue;

                // Update the round robin arbiter
                oPort->vcRoundRobin(vc);

                const int credits_before =
                    outVcState[vc].get_credit_count();
                outVcState[vc].decrement_credit();
                const int credits_after =
                    outVcState[vc].get_credit_count();

                // Just removing the top flit
                flit *t_flit = niOutVcs[vc].getTopFlit();
                t_flit->set_time(clockEdge(Cycles(1)));

                // Scheduling the flit
                scheduleFlit(t_flit);

                ValidationFlitInfo info = validationFlitInfo(t_flit);
                if (info.isCxl)
                    m_net_ptr->incrementCxlFlitLaunch(info.isControl);
                if (ProtocolValidationLogger::enabled()) {
                    m_validation_credit_flits[vc].push_back(info);
                    recordValidationGarnetEvent(
                        "GARNET_FLIT_LAUNCH", info, oPort, vc,
                        credits_before, credits_after, "NI_LINK_LAUNCH");
                    recordValidationGarnetEvent(
                        "CREDIT_UPDATE", info, oPort, vc,
                        credits_before, credits_after,
                        "NI_FLIT_LAUNCH_DECREMENT");
                }

                if (t_flit->get_type() == TAIL_ ||
                   t_flit->get_type() == HEAD_TAIL_) {
                    m_ni_out_vcs_enqueue_time[vc] = Tick(INFINITE_);
                }

                beginValidationCreditStall(oPort, vc);

                // Done with this port, continue to schedule
                // other ports
                return;
            }
        }
    }
 }



 /** This function looks at the NI buffers
  *  if some buffer has flits which are ready to traverse the link in the next
  *  cycle, and the downstream output vc associated with this flit has buffers
  *  left, the link is scheduled for the next cycle
  */

 void
 NetworkInterface::scheduleOutputLink()
 {
     // Schedule each output link
     for (auto &oPort: outPorts) {
         scheduleOutputPort(oPort);
     }
 }

 NetworkInterface::InputPort *
 NetworkInterface::getInportForVnet(int vnet)
 {
     for (auto &iPort : inPorts) {
         if (iPort->isVnetSupported(vnet)) {
             return iPort;
         }
     }

     return nullptr;
 }

 /*
  * This function returns the outport which supports the given vnet.
  * Currently, HeteroGarnet does not support multiple outports to
  * support same vnet. Thus, this function returns the first-and
  * only outport which supports the vnet.
  */
 NetworkInterface::OutputPort *
 NetworkInterface::getOutportForVnet(int vnet)
 {
     for (auto &oPort : outPorts) {
         if (oPort->isVnetSupported(vnet)) {
             return oPort;
         }
     }

     return nullptr;
 }
 void
 NetworkInterface::scheduleFlit(flit *t_flit)
 {
     OutputPort *oPort = getOutportForVnet(t_flit->get_vnet());

     if (oPort) {
         DPRINTF(RubyNetwork, "Scheduling at %s time:%ld flit:%s Message:%s\n",
         oPort->outNetLink()->name(), clockEdge(Cycles(1)),
         *t_flit, *(t_flit->get_msg_ptr()));
         oPort->outFlitQueue()->insert(t_flit);
         oPort->outNetLink()->scheduleEventAbsolute(clockEdge(Cycles(1)));
         return;
     }

     panic("No output port found for vnet:%d\n", t_flit->get_vnet());
     return;
 }

 int
 NetworkInterface::get_vnet(int vc)
 {
     for (int i = 0; i < m_virtual_networks; i++) {
         if (vc >= (i*m_vc_per_vnet) && vc < ((i+1)*m_vc_per_vnet)) {
             return i;
         }
     }
     fatal("Could not determine vc");
 }


 // Wakeup the NI in the next cycle if there are waiting
 // messages in the protocol buffer, or waiting flits in the
 // output VC buffer.
 // Also check if we have to reschedule because of a clock period
 // difference.
 void
 NetworkInterface::checkReschedule()
 {
     for (const auto& it : inNode_ptr) {
         if (it == nullptr) {
             continue;
         }

         while (it->isReady(clockEdge())) { // Is there a message waiting
             scheduleEvent(Cycles(1));
             return;
         }
     }

     for (auto& ni_out_vc : niOutVcs) {
         if (ni_out_vc.isReady(clockEdge(Cycles(1)))) {
             scheduleEvent(Cycles(1));
             return;
         }
     }

     // Check if any input links have flits to be popped.
     // This can happen if the links are operating at
     // a higher frequency.
     for (auto &iPort : inPorts) {
         NetworkLink *inNetLink = iPort->inNetLink();
         if (inNetLink->isReady(curTick())) {
             scheduleEvent(Cycles(1));
             return;
         }
     }

     for (auto &oPort : outPorts) {
         CreditLink *inCreditLink = oPort->inCreditLink();
         if (inCreditLink->isReady(curTick())) {
             scheduleEvent(Cycles(1));
             return;
         }
     }
 }

 void
 NetworkInterface::print(std::ostream& out) const
 {
     out << "[Network Interface]";
 }

 bool
 NetworkInterface::functionalRead(Packet *pkt, WriteMask &mask)
 {
     bool read = false;
     for (auto& ni_out_vc : niOutVcs) {
         if (ni_out_vc.functionalRead(pkt, mask))
             read = true;
     }

     for (auto &oPort: outPorts) {
         if (oPort->outFlitQueue()->functionalRead(pkt, mask))
             read = true;
     }

     return read;
 }

 uint32_t
 NetworkInterface::functionalWrite(Packet *pkt)
 {
     uint32_t num_functional_writes = 0;
     for (auto& ni_out_vc : niOutVcs) {
         num_functional_writes += ni_out_vc.functionalWrite(pkt);
     }

     for (auto &oPort: outPorts) {
         num_functional_writes += oPort->outFlitQueue()->functionalWrite(pkt);
     }
     return num_functional_writes;
 }

 } // namespace garnet
 } // namespace ruby
 } // namespace gem5
