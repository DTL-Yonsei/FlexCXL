/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include "mem/cxl_home_agent.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/CXLHomeAgent.hh"
#include "sim/protocol_validation_logger.hh"

namespace gem5
{

namespace memory
{

CXLHomeAgent::RubySidePort::RubySidePort(const std::string &name,
                                         CXLHomeAgent &agent)
    : QueuedResponsePort(name, queue), queue(agent, *this, true),
      agent(agent)
{
}

Tick
CXLHomeAgent::RubySidePort::recvAtomic(PacketPtr pkt)
{
    return agent.recvAtomic(pkt);
}

void
CXLHomeAgent::RubySidePort::recvFunctional(PacketPtr pkt)
{
    agent.recvFunctional(pkt);
}

bool
CXLHomeAgent::RubySidePort::recvTimingReq(PacketPtr pkt)
{
    return agent.recvTimingReq(pkt);
}

AddrRangeList
CXLHomeAgent::RubySidePort::getAddrRanges() const
{
    return agent.getAddrRanges();
}

CXLHomeAgent::CxlSidePort::CxlSidePort(const std::string &name,
                                       CXLHomeAgent &agent)
    : QueuedRequestPort(name, reqQueue, snoopRespQueue),
      reqQueue(agent, *this), snoopRespQueue(agent, *this),
      agent(agent)
{
}

bool
CXLHomeAgent::CxlSidePort::recvTimingResp(PacketPtr pkt)
{
    return agent.recvTimingResp(pkt);
}

CXLHomeAgent::CXLHomeAgent(const CXLHomeAgentParams &p)
    : ClockedObject(p),
      rubySidePort(name() + ".ruby_side", *this),
      rubySidePort1(name() + ".ruby_side_1", *this),
      cxlSidePort(name() + ".cxl_side", *this),
      cxlResponseSidePort(name() + ".cxl_response_side", *this),
      cxlMemStart(p.cxl_mem_start),
      cxlMemSize(p.cxl_mem_size),
      cxlBarStart(p.cxl_bar_start),
      cxlBarSize(p.cxl_bar_size),
      responseLatency(p.response_latency),
      validationForceCxlReady(p.validation_force_cxl_ready)
{
    // TrafficGen microvalidation does not boot Linux and therefore cannot
    // perform the PCI configuration read that normally marks the CXL path as
    // ready. This opt-in hook models only that completed-enumeration state;
    // all requests still traverse the production home-agent/controller path.
    if (p.validation_force_cxl_ready) {
        gem5::trace::cxl_mode = true;
        gem5::trace::cxl_check = 1;
        // A no-CPU TrafficGen harness has no BaseCPU::switchOut() call to
        // publish timing-mode readiness to the legacy CXLDRAMsim3 wrapper.
        // The System itself is still configured in timing mode; this flag
        // only mirrors that state for the validation-only no-boot path.
        gem5::trace::is_timing_mode = true;
    }
}

void
CXLHomeAgent::init()
{
    if (!rubySidePort.isConnected()) {
        fatal("%s.ruby_side is unconnected\n", name());
    }

    if (!rubySidePort1.isConnected()) {
        fatal("%s.ruby_side_1 is unconnected\n", name());
    }

    if (!cxlSidePort.isConnected()) {
        fatal("%s.cxl_side is unconnected\n", name());
    }

    if (validationForceCxlReady && !cxlResponseSidePort.isConnected()) {
        fatal("%s.cxl_response_side is required by standalone validation\n",
              name());
    }

    rubySidePort.sendRangeChange();
    rubySidePort1.sendRangeChange();
}

Port &
CXLHomeAgent::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "ruby_side") {
        return rubySidePort;
    }

    if (if_name == "ruby_side_1") {
        return rubySidePort1;
    }

    if (if_name == "cxl_side") {
        return cxlSidePort;
    }

    if (if_name == "cxl_response_side") {
        return cxlResponseSidePort;
    }

    return ClockedObject::getPort(if_name, idx);
}

AddrRangeList
CXLHomeAgent::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.emplace_back(cxlMemStart, cxlMemStart + cxlMemSize);
    return ranges;
}

bool
CXLHomeAgent::isCxlMemAddr(PacketPtr pkt) const
{
    const bool cxl_ready = validationForceCxlReady ||
        (gem5::trace::cxl_mode && gem5::trace::cxl_check >= 1);
    return cxl_ready &&
        pkt->getAddr() >= cxlMemStart &&
        pkt->getAddr() < cxlMemStart + cxlMemSize;
}

bool
CXLHomeAgent::isCxlIoAddr(PacketPtr pkt) const
{
    const bool cxl_ready = validationForceCxlReady ||
        (gem5::trace::cxl_mode && gem5::trace::cxl_check >= 1);
    return cxl_ready &&
        pkt->getAddr() >= cxlBarStart &&
        pkt->getAddr() < cxlBarStart + cxlBarSize;
}

void
CXLHomeAgent::mem2cxl(PacketPtr pkt) const
{
    if (isCxlMemAddr(pkt) && !pkt->is_retry) {
        const Addr origin_addr = pkt->getAddr();
        Addr cxl_addr = cxlBarStart;

        pkt->cxl_flag = true;
        pkt->is_cxl_mem = true;
        pkt->origin_dest = origin_addr - cxlMemStart;

        panic_if(cxlBarSize == 0,
                 "CXLHomeAgent requires a non-zero CXL BAR size\n");

        // The BAR address is only the routing window for CXL.mem traffic.
        // Keep the real CXL memory offset in origin_dest so aliased BAR
        // addresses are disambiguated by the downstream CXL device path.
        const Addr bar_offset = pkt->origin_dest % cxlBarSize;

        cxl_addr += bar_offset;
        pkt->setAddr(cxl_addr);

        DPRINTF(CXLHomeAgent,
                "mem2cxl hpa=%#llx bar_addr=%#llx origin_dest=%#llx\n",
                (unsigned long long)origin_addr,
                (unsigned long long)cxl_addr,
                (unsigned long long)pkt->origin_dest);
    } else if (isCxlIoAddr(pkt) && !pkt->is_retry) {
        pkt->is_cxl_io = true;
        pkt->cxl_flag = true;
    }
}

void
CXLHomeAgent::checkCxlMeta(PacketPtr pkt) const
{
    if (pkt->cxl_flag) {
        switch (pkt->cmdToIndex()) {
          case MemCmd::ReadReq:
            pkt->MetaField = MemCmd::No_Op;
            pkt->MetaValue = MemCmd::Invalid;
            break;
          case MemCmd::ReadSharedReq:
            pkt->MetaField = MemCmd::Meta_0_State;
            pkt->MetaValue = MemCmd::Shared;
            break;
          case MemCmd::ReadExReq:
            pkt->MetaField = MemCmd::Meta_0_State;
            pkt->MetaValue = MemCmd::Any;
            break;
          case MemCmd::WriteReq:
            if (!pkt->isExpressSnoop()) {
                pkt->MetaField = MemCmd::Meta_0_State;
                pkt->MetaValue = MemCmd::Invalid;
            } else {
                pkt->MetaField = MemCmd::Meta_0_State;
                pkt->MetaValue = MemCmd::Any;
            }
            break;
          default:
            pkt->MetaField = MemCmd::Meta_0_State;
            pkt->MetaValue = MemCmd::Any;
            break;
        }
    } else {
        pkt->MetaField = MemCmd::No_Op;
        pkt->MetaValue = MemCmd::Invalid;
    }
}

void
CXLHomeAgent::restoreHpa(PacketPtr pkt) const
{
    if (pkt->cxl_flag && pkt->is_cxl_mem && pkt->origin_addr != 0) {
        pkt->setAddr(pkt->origin_addr);
    }
}

bool
CXLHomeAgent::recvTimingReq(PacketPtr pkt)
{
    const Addr hpa = pkt->getAddr();

    const auto source_transaction =
        ProtocolValidationLogger::bindSourceTransaction(pkt, hpa);
    const uint64_t transaction_id = source_transaction.value_or(
        ProtocolValidationLogger::assignTransaction(pkt));
    if (!source_transaction) {
        ProtocolValidationEvent accepted;
        accepted.event = "REQ_ACCEPTED";
        accepted.component = name();
        accepted.layer = "TRAFFIC_SOURCE";
        accepted.direction = "H2D";
        accepted.pathStage = "REQUEST";
        accepted.transactionId = transaction_id;
        ProtocolValidationLogger::recordPacket(accepted, pkt);
    }

    pkt->origin_addr = pkt->getAddr();
    mem2cxl(pkt);
    checkCxlMeta(pkt);

    if (!pkt->cxl_flag) {
        warn("%s received non-CXL timing request addr=%#llx cmd=%s\n",
             name(), (unsigned long long)hpa, pkt->cmdString());
        return false;
    }

    if (pkt->cxl_flag && pkt->is_cxl_mem) {
        pkt->cxl_pkt.start_time = curTick();
    }

    ProtocolValidationEvent forwarded;
    forwarded.event = "HA_FORWARD";
    forwarded.component = name();
    forwarded.layer = "CXL_HOME_AGENT";
    forwarded.direction = "H2D";
    forwarded.pathStage = "HA";
    forwarded.transactionId = transaction_id;
    ProtocolValidationLogger::recordPacket(forwarded, pkt);

    cxlSidePort.schedTimingReq(pkt, clockEdge(Cycles(1)));
    return true;
}

bool
CXLHomeAgent::recvTimingResp(PacketPtr pkt)
{
    const Tick response_time =
        clockEdge() + pkt->headerDelay + pkt->payloadDelay + responseLatency;

    pkt->headerDelay = pkt->payloadDelay = 0;
    restoreHpa(pkt);

    ProtocolValidationEvent response;
    response.event = "RESPONSE_TX";
    response.component = name();
    response.layer = "CXL_HOME_AGENT";
    response.direction = "D2H";
    response.pathStage = "RESPONSE";
    response.packetSeq = pkt->cxl_pkt.seqNum;
    ProtocolValidationLogger::recordPacket(response, pkt);

    rubySidePort1.schedTimingResp(pkt, response_time);
    return true;
}

Tick
CXLHomeAgent::recvAtomic(PacketPtr pkt)
{
    const Addr hpa = pkt->getAddr();

    pkt->origin_addr = pkt->getAddr();
    mem2cxl(pkt);
    checkCxlMeta(pkt);

    if (!pkt->cxl_flag) {
        warn("%s received non-CXL atomic request addr=%#llx cmd=%s\n",
             name(), (unsigned long long)hpa, pkt->cmdString());
        return 0;
    }

    if (pkt->cxl_flag && pkt->is_cxl_mem) {
        pkt->cxl_pkt.start_time = curTick();
    }

    const Tick latency = cxlSidePort.sendAtomic(pkt);
    restoreHpa(pkt);
    return latency + responseLatency;
}

void
CXLHomeAgent::recvFunctional(PacketPtr pkt)
{
    if (rubySidePort1.trySatisfyFunctional(pkt) ||
        cxlSidePort.trySatisfyFunctional(pkt) ||
        (cxlResponseSidePort.isConnected() &&
         cxlResponseSidePort.trySatisfyFunctional(pkt))) {
        return;
    }

    pkt->origin_addr = pkt->getAddr();
    mem2cxl(pkt);
    checkCxlMeta(pkt);
    cxlSidePort.sendFunctional(pkt);
    restoreHpa(pkt);
}

} // namespace memory
} // namespace gem5
