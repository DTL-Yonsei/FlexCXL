#include "sim/protocol_validation_logger.hh"

#include <iomanip>
#include <sstream>

#include "base/logging.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

ProtocolValidationLogger *ProtocolValidationLogger::activeLogger = nullptr;

namespace
{

constexpr Addr
validationLineAddress(Addr address)
{
    // The single-host validation contract fixes the cache line at 64 bytes.
    return address & ~Addr(63);
}

template <typename T>
void
writeOptional(std::ostream &out, const std::optional<T> &value)
{
    if (value) {
        out << *value;
    } else {
        out << "null";
    }
}

void
writeOptionalBool(std::ostream &out, const std::optional<bool> &value)
{
    if (value) {
        out << (*value ? "true" : "false");
    } else {
        out << "null";
    }
}

} // anonymous namespace

ProtocolValidationLogger::ProtocolValidationLogger(const Params &params)
    : SimObject(params), loggingEnabled(params.enabled),
      corruptionEnabled(params.corruption_enabled), runId(params.run_id),
      corruptDirection(params.corrupt_direction),
      corruptAttempt(params.corrupt_attempt)
{
    fatal_if(activeLogger,
             "Only one ProtocolValidationLogger may be instantiated\n");
    activeLogger = this;

    for (const auto sequence : params.corrupt_seqs)
        corruptSeqs.insert(sequence);
    // Keep the original single-sequence parameter working for the directed
    // T3/T3-B commands and for old run scripts.
    if (corruptSeqs.empty() && params.corrupt_seq >= 0)
        corruptSeqs.insert(params.corrupt_seq);

    if (!loggingEnabled)
        return;

    fatal_if(params.output_file.empty(),
             "ProtocolValidationLogger requires output_file when enabled\n");
    fatal_if(runId.empty(),
             "ProtocolValidationLogger requires a non-empty run_id\n");
    output.open(params.output_file, std::ios::out | std::ios::trunc);
    fatal_if(!output.is_open(),
             "Cannot open protocol validation log %s\n", params.output_file);
}

ProtocolValidationLogger::~ProtocolValidationLogger()
{
    if (output.is_open())
        output.close();
    if (activeLogger == this)
        activeLogger = nullptr;
}

bool
ProtocolValidationLogger::enabled()
{
    return activeLogger && activeLogger->loggingEnabled;
}

uint64_t
ProtocolValidationLogger::assignTransaction(PacketPtr pkt)
{
    if (!enabled() || !pkt || !pkt->req)
        return 0;

    const Request *key = pkt->req.get();
    auto [it, inserted] = activeLogger->transactions.emplace(
        key, activeLogger->nextTransactionId);
    if (inserted)
        ++activeLogger->nextTransactionId;
    return it->second;
}

uint64_t
ProtocolValidationLogger::registerSourceTransaction(PacketPtr pkt)
{
    if (!enabled() || !pkt || !pkt->req)
        return 0;

    const Addr address = validationLineAddress(pkt->getAddr());
    const Request *key = pkt->req.get();
    const auto existing = activeLogger->transactions.find(key);
    if (existing != activeLogger->transactions.end()) {
        const auto source_address =
            activeLogger->sourceTransactionAddresses.find(existing->second);
        if (source_address ==
                activeLogger->sourceTransactionAddresses.end() ||
            source_address->second != address) {
            // Request objects come from an allocator and their raw addresses
            // can be reused while an older validation transaction is still
            // represented by other packet objects.  A missing source
            // registration or a different source line proves that this
            // pointer identity is stale.  Replace only this pointer's
            // association; the older transaction remains active.
            existing->second = activeLogger->nextTransactionId++;
        }
    }

    const uint64_t transaction_id = assignTransaction(pkt);

    // A retried TrafficGen packet retains its Request object.  Do not add a
    // second address-queue entry if the source was already registered.
    if (activeLogger->sourceTransactionAddresses.count(transaction_id) == 0) {
        const auto [address_it, inserted] =
            activeLogger->activeSourceTransactions.emplace(
                address, transaction_id);
        fatal_if(!inserted && address_it->second != transaction_id,
                 "Protocol validation cannot correlate concurrent "
                 "same-address source transactions at %#llx\n",
                 (unsigned long long)address);
        activeLogger->sourceTransactionAddresses.emplace(
            transaction_id, address);
    }
    return transaction_id;
}

std::optional<uint64_t>
ProtocolValidationLogger::bindSourceTransaction(
    PacketPtr pkt, Addr address)
{
    if (!enabled() || !pkt || !pkt->req)
        return std::nullopt;

    address = validationLineAddress(address);
    const auto existing = activeLogger->transactions.find(pkt->req.get());
    if (existing != activeLogger->transactions.end()) {
        const auto source_address =
            activeLogger->sourceTransactionAddresses.find(existing->second);
        if (source_address !=
                activeLogger->sourceTransactionAddresses.end() &&
            source_address->second == address) {
            return existing->second;
        }

        // The allocator reused a Request address, or the raw pointer retained
        // an internal assignment that was never source-registered.  Drop only
        // the stale pointer association and recover the new transaction
        // through the source-address bridge.
        activeLogger->transactions.erase(existing);
    }

    const auto address_it =
        activeLogger->activeSourceTransactions.find(address);
    if (address_it == activeLogger->activeSourceTransactions.end())
        return std::nullopt;

    const uint64_t transaction_id = address_it->second;
    fatal_if(activeLogger->boundSourceTransactions.count(transaction_id) != 0,
             "Protocol validation observed a second Ruby memory packet for "
             "active source transaction %llu at %#llx\n",
             (unsigned long long)transaction_id,
             (unsigned long long)address);
    activeLogger->transactions.emplace(pkt->req.get(), transaction_id);
    activeLogger->boundSourceTransactions.insert(transaction_id);
    return transaction_id;
}

std::optional<uint64_t>
ProtocolValidationLogger::transactionId(PacketPtr pkt)
{
    if (!enabled() || !pkt || !pkt->req)
        return std::nullopt;
    const auto it = activeLogger->transactions.find(pkt->req.get());
    if (it == activeLogger->transactions.end())
        return std::nullopt;
    return it->second;
}

void
ProtocolValidationLogger::releaseTransaction(PacketPtr pkt)
{
    if (!enabled() || !pkt || !pkt->req)
        return;

    const auto transaction_it =
        activeLogger->transactions.find(pkt->req.get());
    if (transaction_it == activeLogger->transactions.end())
        return;
    const uint64_t transaction_id = transaction_it->second;

    for (auto it = activeLogger->transactions.begin();
         it != activeLogger->transactions.end();) {
        if (it->second == transaction_id)
            it = activeLogger->transactions.erase(it);
        else
            ++it;
    }

    const auto address_it =
        activeLogger->sourceTransactionAddresses.find(transaction_id);
    if (address_it != activeLogger->sourceTransactionAddresses.end()) {
        const Addr address = address_it->second;
        const auto active_it =
            activeLogger->activeSourceTransactions.find(address);
        if (active_it != activeLogger->activeSourceTransactions.end() &&
            active_it->second == transaction_id) {
            activeLogger->activeSourceTransactions.erase(active_it);
        }
        activeLogger->sourceTransactionAddresses.erase(address_it);
    }
    activeLogger->boundSourceTransactions.erase(transaction_id);
}

std::string
ProtocolValidationLogger::escape(const std::string &value)
{
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
          case '\\': out << "\\\\"; break;
          case '"': out << "\\\""; break;
          case '\n': out << "\\n"; break;
          case '\r': out << "\\r"; break;
          case '\t': out << "\\t"; break;
          default: out << ch; break;
        }
    }
    return out.str();
}

void
ProtocolValidationLogger::write(const ProtocolValidationEvent &event)
{
    output << "{\"schema_version\":1"
           << ",\"run_id\":\"" << escape(runId) << "\""
           << ",\"event_id\":" << nextEventId++
           << ",\"tick\":" << curTick()
           << ",\"time_ns\":" << std::fixed << std::setprecision(3)
           << (static_cast<double>(curTick()) / 1000.0)
           << ",\"event\":\"" << escape(event.event) << "\""
           << ",\"component\":\"" << escape(event.component) << "\""
           << ",\"layer\":\"" << escape(event.layer) << "\""
           << ",\"direction\":\"" << escape(event.direction) << "\""
           << ",\"transaction_id\":";
    writeOptional(output, event.transactionId);
    output << ",\"packet_seq\":";
    writeOptional(output, event.packetSeq);
    output << ",\"flit_index\":";
    writeOptional(output, event.flitIndex);
    output << ",\"tx_attempt\":";
    writeOptional(output, event.txAttempt);
    output << ",\"ack_seq\":";
    writeOptional(output, event.ackSeq);
    output << ",\"nak_seq\":";
    writeOptional(output, event.nakSeq);
    output << ",\"rb_id\":\"" << escape(event.rbId) << "\""
           << ",\"rb_occupancy\":";
    writeOptional(output, event.rbOccupancy);
    output << ",\"rb_capacity\":";
    writeOptional(output, event.rbCapacity);
    output << ",\"queue_id\":\"" << escape(event.queueId) << "\""
           << ",\"queue_occupancy\":";
    writeOptional(output, event.queueOccupancy);
    output << ",\"queue_capacity\":";
    writeOptional(output, event.queueCapacity);
    output << ",\"wait_ticks\":";
    writeOptional(output, event.waitTicks);
    output << ",\"vnet\":";
    writeOptional(output, event.vnet);
    output << ",\"vc\":";
    writeOptional(output, event.vc);
    output << ",\"router_id\":";
    writeOptional(output, event.routerId);
    output << ",\"outport\":\"" << escape(event.outport) << "\""
           << ",\"credits_before\":";
    writeOptional(output, event.creditsBefore);
    output << ",\"credits_after\":";
    writeOptional(output, event.creditsAfter);
    output << ",\"eligible_vc_count\":";
    writeOptional(output, event.eligibleVcCount);
    output << ",\"active_vc_count\":";
    writeOptional(output, event.activeVcCount);
    output << ",\"idle_vc_count\":";
    writeOptional(output, event.idleVcCount);
    output << ",\"stall_reason\":\"" << escape(event.stallReason) << "\""
           << ",\"reason\":\"" << escape(event.reason) << "\""
           << ",\"path_stage\":\"" << escape(event.pathStage) << "\""
           << ",\"timer_id\":\"" << escape(event.timerId) << "\""
           << ",\"command\":\"" << escape(event.command) << "\""
           << ",\"garnet_packet_id\":";
    writeOptional(output, event.garnetPacketId);
    output << ",\"address\":";
    writeOptional(output, event.address);
    output << ",\"backend_address\":";
    writeOptional(output, event.backendAddress);
    output << ",\"dram_cycle\":";
    writeOptional(output, event.dramCycle);
    output << ",\"boundary_order\":";
    writeOptional(output, event.boundaryOrder);
    output << ",\"completion_order\":";
    writeOptional(output, event.completionOrder);
    output << ",\"request_size\":";
    writeOptional(output, event.requestSize);
    output << ",\"expiry_tick\":";
    writeOptional(output, event.expiryTick);
    output << ",\"is_control\":";
    writeOptionalBool(output, event.isControl);
    output << ",\"crc_ok\":";
    writeOptionalBool(output, event.crcOk);
    output << ",\"free_signal\":";
    writeOptionalBool(output, event.freeSignal);
    output << ",\"vc_idle\":";
    writeOptionalBool(output, event.vcIdle);
    output << ",\"accepted\":";
    writeOptionalBool(output, event.accepted);
    output << ",\"is_write\":";
    writeOptionalBool(output, event.isWrite);
    output << "}\n";
    output.flush();
}

void
ProtocolValidationLogger::record(ProtocolValidationEvent event)
{
    if (enabled())
        activeLogger->write(event);
}

void
ProtocolValidationLogger::recordPacket(
    ProtocolValidationEvent event, PacketPtr pkt)
{
    if (!enabled())
        return;
    if (!event.transactionId)
        event.transactionId = transactionId(pkt);
    if (pkt) {
        event.address = pkt->getAddr();
        event.command = pkt->cmdString();
        event.isControl = pkt->cxl_pkt.is_controlflit;
        event.crcOk = pkt->cxl_pkt.crc_check;
    }
    activeLogger->write(event);
}

bool
ProtocolValidationLogger::corruptionPlanned(
    const std::string &direction, int64_t sequence, int64_t attempt)
{
    return activeLogger && activeLogger->corruptionEnabled &&
        !activeLogger->corruptDirection.empty() &&
        activeLogger->corruptDirection == direction &&
        activeLogger->corruptSeqs.count(sequence) != 0 &&
        activeLogger->appliedCorruptSeqs.count(sequence) == 0 &&
        activeLogger->corruptAttempt == attempt;
}

bool
ProtocolValidationLogger::takeCorruption(
    const std::string &direction, int64_t sequence, int64_t attempt)
{
    if (!corruptionPlanned(direction, sequence, attempt)) {
        return false;
    }
    activeLogger->appliedCorruptSeqs.insert(sequence);
    return true;
}

} // namespace gem5
