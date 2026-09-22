/*
 * Deterministic fixed-latency endpoint for CXL protocol validation.
 */

#ifndef __MEM_CXL_FIXED_LATENCY_ENDPOINT_HH__
#define __MEM_CXL_FIXED_LATENCY_ENDPOINT_HH__

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "mem/abstract_mem.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/CXLFixedLatencyEndpoint.hh"
#include "sim/eventq.hh"

namespace gem5
{

namespace memory
{

class CXLFixedLatencyEndpoint : public AbstractMemory
{
  private:
    class RequestIngressPort : public ResponsePort
    {
      private:
        CXLFixedLatencyEndpoint &endpoint;

      public:
        RequestIngressPort(const std::string &name,
                           CXLFixedLatencyEndpoint &endpoint);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        bool tryTiming(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    class ResponseEgressPort : public ResponsePort
    {
      private:
        CXLFixedLatencyEndpoint &endpoint;

      public:
        ResponseEgressPort(const std::string &name,
                           CXLFixedLatencyEndpoint &endpoint);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    struct PendingRequest
    {
        PacketPtr pkt;
        Tick readyTick;
        bool serviced;

        PendingRequest(PacketPtr pkt, Tick ready_tick)
            : pkt(pkt), readyTick(ready_tick), serviced(false)
        {
        }
    };

    RequestIngressPort requestPort;
    ResponseEgressPort responsePort;

    const Tick responseLatency;
    const uint64_t capacity;

    std::deque<PendingRequest> pendingRequests;
    EventFunctionWrapper responseEvent;
    bool requestRetryPending;
    bool responseRetryPending;
    bool requestQueueStallActive;
    std::optional<uint64_t> stalledTransactionId;
    std::optional<int64_t> stalledPacketSeq;
    std::optional<int64_t> stalledTxAttempt;
    std::optional<Addr> stalledAddress;
    std::string stalledCommand;
    std::optional<bool> stalledIsControl;
    std::optional<bool> stalledCrcOk;

    bool canAcceptTiming() const;
    void validateAccess(PacketPtr pkt, bool require_response) const;
    bool recvTimingReq(PacketPtr pkt);
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    void recvRespRetry();
    void processResponse();
    void scheduleResponse();
    void recordEndpointEvent(const char *event, const char *direction,
                             const char *path_stage, PacketPtr pkt) const;
    void beginRequestQueueStall(PacketPtr pkt);
    void endRequestQueueStall();
    void recordResponseEgressStall(const char *event, PacketPtr pkt) const;

  public:
    PARAMS(CXLFixedLatencyEndpoint);
    CXLFixedLatencyEndpoint(const Params &params);

    void init() override;
    DrainState drain() override;
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_FIXED_LATENCY_ENDPOINT_HH__
