/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#ifndef __MEM_CXL_HOME_AGENT_HH__
#define __MEM_CXL_HOME_AGENT_HH__

#include "mem/packet.hh"
#include "mem/packet_queue.hh"
#include "mem/port.hh"
#include "mem/qport.hh"
#include "params/CXLHomeAgent.hh"
#include "sim/clocked_object.hh"

namespace gem5
{

namespace memory
{

class CXLHomeAgent : public ClockedObject
{
  private:
    class RubySidePort : public QueuedResponsePort
    {
      private:
        RespPacketQueue queue;
        CXLHomeAgent &agent;

      public:
        RubySidePort(const std::string &name, CXLHomeAgent &agent);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        AddrRangeList getAddrRanges() const override;
    };

    class CxlSidePort : public QueuedRequestPort
    {
      private:
        ReqPacketQueue reqQueue;
        SnoopRespPacketQueue snoopRespQueue;
        CXLHomeAgent &agent;

      public:
        CxlSidePort(const std::string &name, CXLHomeAgent &agent);

        void schedTimingReq(PacketPtr pkt, Tick when)
        {
            QueuedRequestPort::schedTimingReq(pkt, when);
        }

        bool trySatisfyFunctional(PacketPtr pkt)
        {
            return QueuedRequestPort::trySatisfyFunctional(pkt);
        }

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
    };

    RubySidePort rubySidePort;
    RubySidePort rubySidePort1;
    CxlSidePort cxlSidePort;
    CxlSidePort cxlResponseSidePort;

    const Addr cxlMemStart;
    const uint64_t cxlMemSize;
    const Addr cxlDeviceOffset;
    const Addr cxlBarStart;
    const uint64_t cxlBarSize;
    const Tick responseLatency;
    const bool validationForceCxlReady;
    const uint8_t validationHostId;

    bool isCxlMemAddr(PacketPtr pkt) const;
    bool isCxlIoAddr(PacketPtr pkt) const;
    void mem2cxl(PacketPtr pkt) const;
    void checkCxlMeta(PacketPtr pkt) const;
    void restoreHpa(PacketPtr pkt) const;

    bool recvTimingReq(PacketPtr pkt);
    bool recvTimingResp(PacketPtr pkt);
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);

    AddrRangeList getAddrRanges() const;

  public:
    CXLHomeAgent(const CXLHomeAgentParams &p);

    void init() override;
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_HOME_AGENT_HH__
