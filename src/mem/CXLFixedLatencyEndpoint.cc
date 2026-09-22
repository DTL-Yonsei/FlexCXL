/*
 * Deterministic fixed-latency endpoint for CXL protocol validation.
 */

#include "mem/CXLFixedLatencyEndpoint.hh"

#include <algorithm>
#include <cassert>

#include "base/logging.hh"
#include "sim/protocol_validation_logger.hh"

namespace gem5
{

namespace memory
{

CXLFixedLatencyEndpoint::RequestIngressPort::RequestIngressPort(
    const std::string &name, CXLFixedLatencyEndpoint &endpoint)
    : ResponsePort(name), endpoint(endpoint)
{
}

Tick
CXLFixedLatencyEndpoint::RequestIngressPort::recvAtomic(PacketPtr pkt)
{
    return endpoint.recvAtomic(pkt);
}

void
CXLFixedLatencyEndpoint::RequestIngressPort::recvFunctional(PacketPtr pkt)
{
    endpoint.recvFunctional(pkt);
}

bool
CXLFixedLatencyEndpoint::RequestIngressPort::recvTimingReq(PacketPtr pkt)
{
    return endpoint.recvTimingReq(pkt);
}

bool
CXLFixedLatencyEndpoint::RequestIngressPort::tryTiming(PacketPtr pkt)
{
    return endpoint.canAcceptTiming();
}

void
CXLFixedLatencyEndpoint::RequestIngressPort::recvRespRetry()
{
    panic("%s received a response retry on its request-only port\n", name());
}

AddrRangeList
CXLFixedLatencyEndpoint::RequestIngressPort::getAddrRanges() const
{
    return {endpoint.getAddrRange()};
}

CXLFixedLatencyEndpoint::ResponseEgressPort::ResponseEgressPort(
    const std::string &name, CXLFixedLatencyEndpoint &endpoint)
    : ResponsePort(name), endpoint(endpoint)
{
}

Tick
CXLFixedLatencyEndpoint::ResponseEgressPort::recvAtomic(PacketPtr pkt)
{
    panic("%s received an atomic request on its response-only port\n", name());
}

void
CXLFixedLatencyEndpoint::ResponseEgressPort::recvFunctional(PacketPtr pkt)
{
    panic("%s received a functional request on its response-only port\n",
          name());
}

bool
CXLFixedLatencyEndpoint::ResponseEgressPort::recvTimingReq(PacketPtr pkt)
{
    panic("%s received a timing request on its response-only port\n", name());
}

void
CXLFixedLatencyEndpoint::ResponseEgressPort::recvRespRetry()
{
    endpoint.recvRespRetry();
}

AddrRangeList
CXLFixedLatencyEndpoint::ResponseEgressPort::getAddrRanges() const
{
    return {endpoint.getAddrRange()};
}

CXLFixedLatencyEndpoint::CXLFixedLatencyEndpoint(const Params &params)
    : AbstractMemory(params),
      requestPort(name() + ".request_port", *this),
      responsePort(name() + ".response_port", *this),
      responseLatency(params.response_latency), capacity(params.capacity),
      responseEvent([this] { processResponse(); }, name() + ".response"),
      requestRetryPending(false), responseRetryPending(false),
      requestQueueStallActive(false)
{
    fatal_if(capacity == 0, "%s capacity must be greater than zero\n", name());
}

void
CXLFixedLatencyEndpoint::init()
{
    AbstractMemory::init();

    fatal_if(!requestPort.isConnected(), "%s.request_port is unconnected\n",
             name());
    fatal_if(!responsePort.isConnected(), "%s.response_port is unconnected\n",
             name());

    requestPort.sendRangeChange();
}

Port &
CXLFixedLatencyEndpoint::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "request_port")
        return requestPort;
    if (if_name == "response_port")
        return responsePort;
    return AbstractMemory::getPort(if_name, idx);
}

bool
CXLFixedLatencyEndpoint::canAcceptTiming() const
{
    return !requestRetryPending && pendingRequests.size() < capacity;
}

void
CXLFixedLatencyEndpoint::validateAccess(
    PacketPtr pkt, const bool require_response) const
{
    panic_if(!pkt, "%s received a null packet\n", name());
    panic_if(!pkt->isRequest(), "%s expected a request, got %s\n", name(),
             pkt->cmdString());
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "%s accepts only reads and writes, got %s\n", name(),
             pkt->cmdString());
    panic_if(pkt->cacheResponding(),
             "%s received a packet already satisfied by a cache\n", name());
    panic_if(!pkt->getAddrRange().isSubset(getAddrRange()),
             "%s access %s is outside BAR range %s\n", name(),
             pkt->getAddrRange().to_string().c_str(),
             getAddrRange().to_string().c_str());
    panic_if(require_response && !pkt->needsResponse(),
             "%s timing request %s does not require a response\n", name(),
             pkt->cmdString());
}

bool
CXLFixedLatencyEndpoint::recvTimingReq(PacketPtr pkt)
{
    validateAccess(pkt, true);

    if (!canAcceptTiming()) {
        if (!requestRetryPending)
            beginRequestQueueStall(pkt);
        requestRetryPending = true;
        return false;
    }

    // Latency is measured exactly from endpoint acceptance. Any transport
    // delay has already been modeled before this boundary.
    pkt->headerDelay = 0;
    pkt->payloadDelay = 0;
    pendingRequests.emplace_back(pkt, curTick() + responseLatency);

    recordEndpointEvent(
        "ENDPOINT_ACCEPT", "H2D", "ENDPOINT", pkt);
    scheduleResponse();
    return true;
}

Tick
CXLFixedLatencyEndpoint::recvAtomic(PacketPtr pkt)
{
    validateAccess(pkt, false);
    access(pkt);
    return responseLatency;
}

void
CXLFixedLatencyEndpoint::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);

    bool done = false;
    for (auto &pending : pendingRequests) {
        if (done)
            break;
        done = pkt->trySatisfyFunctional(pending.pkt);
    }

    pkt->popLabel();
}

void
CXLFixedLatencyEndpoint::scheduleResponse()
{
    if (pendingRequests.empty() || responseRetryPending ||
        responseEvent.scheduled()) {
        return;
    }

    schedule(responseEvent,
             std::max(curTick(), pendingRequests.front().readyTick));
}

void
CXLFixedLatencyEndpoint::processResponse()
{
    assert(!pendingRequests.empty());
    assert(!responseRetryPending);

    PendingRequest &pending = pendingRequests.front();
    assert(pending.readyTick <= curTick());

    if (!pending.serviced) {
        access(pending.pkt);
        pending.serviced = true;
        recordEndpointEvent(
            "ENDPOINT_SIDE_EFFECT", "H2D", "ENDPOINT", pending.pkt);
    }

    PacketPtr pkt = pending.pkt;

    // A successful send transfers ownership to the peer. Capture every log
    // field before the attempt so the packet is never dereferenced afterward.
    const bool log_response = pkt->cxl_flag;
    ProtocolValidationEvent response_event;
    if (log_response) {
        response_event.event = "ENDPOINT_RESPONSE_TX";
        response_event.component = name();
        response_event.layer = "ENDPOINT";
        response_event.direction = "D2H";
        response_event.pathStage = "ENDPOINT_RESPONSE";
        response_event.transactionId =
            ProtocolValidationLogger::transactionId(pkt);
        response_event.packetSeq = pkt->cxl_pkt.seqNum;
        response_event.txAttempt =
            (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp) ? 1 : 0;
        response_event.address = pkt->getAddr();
        response_event.command = pkt->cmdString();
        response_event.isControl = pkt->cxl_pkt.is_controlflit;
        response_event.crcOk = pkt->cxl_pkt.crc_check;
    }

    if (!responsePort.sendTimingResp(pkt)) {
        recordResponseEgressStall("ENDPOINT_RESPONSE_STALL_BEGIN", pkt);
        responseRetryPending = true;
        return;
    }

    if (log_response)
        ProtocolValidationLogger::record(response_event);
    pendingRequests.pop_front();

    const bool send_request_retry =
        requestRetryPending && pendingRequests.size() < capacity;
    if (send_request_retry) {
        requestRetryPending = false;
        endRequestQueueStall();
    }

    scheduleResponse();

    // A retry is issued only after the response was accepted and the FIFO slot
    // was actually released. The peer may synchronously re-enter recvTimingReq.
    if (send_request_retry)
        requestPort.sendRetryReq();

    if (pendingRequests.empty() && drainState() == DrainState::Draining)
        signalDrainDone();
}

void
CXLFixedLatencyEndpoint::recvRespRetry()
{
    panic_if(!responseRetryPending,
             "%s received an unexpected response retry\n", name());
    panic_if(pendingRequests.empty(),
             "%s response retry arrived with no pending response\n", name());
    recordResponseEgressStall(
        "ENDPOINT_RESPONSE_STALL_END", pendingRequests.front().pkt);
    responseRetryPending = false;
    scheduleResponse();
}

void
CXLFixedLatencyEndpoint::recordEndpointEvent(
    const char *event, const char *direction, const char *path_stage,
    PacketPtr pkt) const
{
    if (!pkt || !pkt->cxl_flag)
        return;

    ProtocolValidationEvent validation_event;
    validation_event.event = event;
    validation_event.component = name();
    validation_event.layer = "ENDPOINT";
    validation_event.direction = direction;
    validation_event.pathStage = path_stage;
    validation_event.packetSeq = pkt->cxl_pkt.seqNum;
    validation_event.txAttempt =
        (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp) ? 1 : 0;
    ProtocolValidationLogger::recordPacket(validation_event, pkt);
}

void
CXLFixedLatencyEndpoint::beginRequestQueueStall(PacketPtr pkt)
{
    panic_if(requestQueueStallActive,
             "%s request queue stall began twice\n", name());
    requestQueueStallActive = true;

    stalledTransactionId = ProtocolValidationLogger::transactionId(pkt);
    stalledPacketSeq = pkt->cxl_pkt.seqNum;
    stalledTxAttempt =
        (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp) ? 1 : 0;
    stalledAddress = pkt->getAddr();
    stalledCommand = pkt->cmdString();
    stalledIsControl = pkt->cxl_pkt.is_controlflit;
    stalledCrcOk = pkt->cxl_pkt.crc_check;

    ProtocolValidationEvent event;
    event.event = "ENDPOINT_STALL_BEGIN";
    event.component = name();
    event.layer = "ENDPOINT";
    event.direction = "H2D";
    event.pathStage = "ENDPOINT_QUEUE";
    event.queueId = "endpoint_pending";
    event.queueOccupancy = pendingRequests.size();
    event.queueCapacity = capacity;
    event.stallReason = "ENDPOINT_REQUEST_QUEUE_FULL";
    event.reason = "CAPACITY_REJECT";
    event.packetSeq = pkt->cxl_pkt.seqNum;
    event.txAttempt =
        (pkt->cxl_pkt.retry_req || pkt->cxl_pkt.retry_resp) ? 1 : 0;
    ProtocolValidationLogger::recordPacket(event, pkt);
}

void
CXLFixedLatencyEndpoint::endRequestQueueStall()
{
    panic_if(!requestQueueStallActive,
             "%s request queue stall ended without begin\n", name());

    ProtocolValidationEvent event;
    event.event = "ENDPOINT_STALL_END";
    event.component = name();
    event.layer = "ENDPOINT";
    event.direction = "H2D";
    event.pathStage = "ENDPOINT_QUEUE";
    event.queueId = "endpoint_pending";
    event.queueOccupancy = pendingRequests.size();
    event.queueCapacity = capacity;
    event.stallReason = "ENDPOINT_REQUEST_QUEUE_FULL";
    event.reason = "SLOT_RELEASED";
    event.transactionId = stalledTransactionId;
    event.packetSeq = stalledPacketSeq;
    event.txAttempt = stalledTxAttempt;
    event.address = stalledAddress;
    event.command = stalledCommand;
    event.isControl = stalledIsControl;
    event.crcOk = stalledCrcOk;
    ProtocolValidationLogger::record(event);

    requestQueueStallActive = false;
    stalledTransactionId.reset();
    stalledPacketSeq.reset();
    stalledTxAttempt.reset();
    stalledAddress.reset();
    stalledCommand.clear();
    stalledIsControl.reset();
    stalledCrcOk.reset();
}

void
CXLFixedLatencyEndpoint::recordResponseEgressStall(
    const char *event_name, PacketPtr pkt) const
{
    ProtocolValidationEvent event;
    event.event = event_name;
    event.component = name();
    event.layer = "ENDPOINT";
    event.direction = "D2H";
    event.pathStage = "ENDPOINT_RESPONSE_EGRESS";
    event.queueId = "endpoint_response_egress";
    event.queueOccupancy = pendingRequests.size();
    event.queueCapacity = capacity;
    event.stallReason = "ENDPOINT_RESPONSE_EGRESS";
    event.reason =
        std::string(event_name).find("BEGIN") != std::string::npos ?
        "TIMING_RESPONSE_REJECTED" : "RESPONSE_RETRY_RECEIVED";
    ProtocolValidationLogger::recordPacket(event, pkt);
}

DrainState
CXLFixedLatencyEndpoint::drain()
{
    return pendingRequests.empty() ? DrainState::Drained :
                                     DrainState::Draining;
}

} // namespace memory
} // namespace gem5
