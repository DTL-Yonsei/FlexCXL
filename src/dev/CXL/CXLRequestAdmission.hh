#ifndef __DEV_CXL_REQUEST_ADMISSION_HH__
#define __DEV_CXL_REQUEST_ADMISSION_HH__

#include "mem/packet.hh"

namespace gem5
{

/** Optional CXL arbitration query, not a timing-port send/tryTiming.
 * A negative answer neither transfers packet ownership nor commits the port
 * to a recvReqRetry handshake. The caller must arbitrate again later. Packet
 * contents, reservations and queues are unchanged (observation is allowed).
 */
class CXLRequestAdmission
{
  public:
    virtual ~CXLRequestAdmission() = default;
    virtual bool canAcceptCxlRequest(PacketPtr pkt) = 0;
};

} // namespace gem5

#endif
