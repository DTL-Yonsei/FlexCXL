/*
 * Validation-only structured event logger.  With no enabled logger SimObject,
 * every call is a no-op and production behavior is unchanged.
 */

#ifndef __SIM_PROTOCOL_VALIDATION_LOGGER_HH__
#define __SIM_PROTOCOL_VALIDATION_LOGGER_HH__

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "base/types.hh"
#include "mem/packet.hh"
#include "params/ProtocolValidationLogger.hh"
#include "sim/sim_object.hh"

namespace gem5
{

struct ProtocolValidationEvent
{
    std::string event;
    std::string component;
    std::string layer;
    std::string direction;
    std::string rbId;
    std::string queueId;
    std::string outport;
    std::string stallReason;
    std::string reason;
    std::string pathStage;
    std::string timerId;
    std::string command;

    std::optional<uint64_t> transactionId;
    std::optional<int64_t> packetSeq;
    std::optional<int64_t> flitIndex;
    std::optional<int64_t> txAttempt;
    std::optional<int64_t> ackSeq;
    std::optional<int64_t> nakSeq;
    std::optional<int64_t> rbOccupancy;
    std::optional<int64_t> rbCapacity;
    std::optional<int64_t> queueOccupancy;
    std::optional<int64_t> queueCapacity;
    std::optional<Tick> waitTicks;
    std::optional<int64_t> vnet;
    std::optional<int64_t> vc;
    std::optional<int64_t> routerId;
    std::optional<int64_t> creditsBefore;
    std::optional<int64_t> creditsAfter;
    std::optional<int64_t> eligibleVcCount;
    std::optional<int64_t> activeVcCount;
    std::optional<int64_t> idleVcCount;
    std::optional<int64_t> garnetPacketId;
    std::optional<Addr> address;
    /** DRAMSim3 API-boundary metadata used only by backend validation. */
    std::optional<Addr> backendAddress;
    std::optional<uint64_t> dramCycle;
    std::optional<uint64_t> boundaryOrder;
    std::optional<uint64_t> completionOrder;
    std::optional<uint64_t> requestSize;
    std::optional<Tick> expiryTick;
    std::optional<bool> isControl;
    std::optional<bool> crcOk;
    std::optional<bool> freeSignal;
    std::optional<bool> vcIdle;
    std::optional<bool> accepted;
    std::optional<bool> isWrite;
};

class ProtocolValidationLogger : public SimObject
{
  private:
    static ProtocolValidationLogger *activeLogger;

    std::ofstream output;
    const bool loggingEnabled;
    const bool corruptionEnabled;
    const std::string runId;
    const std::string corruptDirection;
    const int64_t corruptAttempt;
    std::unordered_set<int64_t> corruptSeqs;
    std::unordered_set<int64_t> appliedCorruptSeqs;

    uint64_t nextEventId = 0;
    uint64_t nextTransactionId = 0;
    std::unordered_map<const Request *, uint64_t> transactions;
    // The integrated T0 deliberately uses one cold request per HPA.  Keep
    // this bridge strictly one-to-one instead of guessing how multiple
    // same-address CPU requests map onto a coalesced Ruby memory miss.
    std::unordered_map<Addr, uint64_t> activeSourceTransactions;
    std::unordered_map<uint64_t, Addr> sourceTransactionAddresses;
    std::unordered_set<uint64_t> boundSourceTransactions;

    static std::string escape(const std::string &value);
    void write(const ProtocolValidationEvent &event);

  public:
    PARAMS(ProtocolValidationLogger);
    ProtocolValidationLogger(const Params &params);
    ~ProtocolValidationLogger() override;

    static bool enabled();
    static uint64_t assignTransaction(PacketPtr pkt);
    static uint64_t registerSourceTransaction(PacketPtr pkt);
    static std::optional<uint64_t> bindSourceTransaction(
        PacketPtr pkt, Addr address);
    static std::optional<uint64_t> transactionId(PacketPtr pkt);
    static void releaseTransaction(PacketPtr pkt);

    static void record(ProtocolValidationEvent event);
    static void recordPacket(ProtocolValidationEvent event, PacketPtr pkt);

    static bool corruptionPlanned(
        const std::string &direction, int64_t sequence, int64_t attempt);
    static bool takeCorruption(
        const std::string &direction, int64_t sequence, int64_t attempt);
};

} // namespace gem5

#endif // __SIM_PROTOCOL_VALIDATION_LOGGER_HH__
