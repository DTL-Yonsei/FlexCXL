/*
 * Copyright (c) 2017 ARM Limited
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

#include "mem/ruby/network/Network.hh"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "base/logging.hh"
#include "mem/ruby/common/MachineID.hh"
#include "mem/ruby/network/BasicLink.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "debug/RubyNetwork.hh"
#include "base/trace.hh"

namespace gem5
{

namespace ruby
{

namespace
{

bool
decodeGlobalNodeID(NodeID global_id, MachineID &decoded_mid)
{
    for (int t = MachineType_FIRST; t < MachineType_NUM; ++t) {
        const auto mach = static_cast<MachineType>(t);
        const int base = MachineType_base_number(mach);
        const int count = MachineType_base_count(mach);
        if (count <= 0) {
            continue;
        }
        if (global_id >= base && global_id < (base + count)) {
            decoded_mid = MachineID(mach, global_id - base);
            return true;
        }
    }
    return false;
}

} // namespace

uint32_t Network::m_virtual_networks;
uint32_t Network::m_control_msg_size;
uint32_t Network::m_data_msg_size;

Network::Network(const Params &p)
    : ClockedObject(p)
{
    m_virtual_networks = p.number_of_virtual_networks;
    m_control_msg_size = p.control_msg_size;

    fatal_if(p.data_msg_size > p.ruby_system->getBlockSizeBytes(),
            "%s: data message size > cache line size", name());
    m_data_msg_size = p.data_msg_size + m_control_msg_size;

    params().ruby_system->registerNetwork(this);

    // Build exact global-to-local mapping in ext_link order.
    // Keep this map strict: each global controller id must resolve to one
    // local endpoint in this network.
    NodeID local_node_id = 0;
    for (auto &it : params().ext_links) {
        AbstractController *cntrl = it->params().ext_node;
        params().ruby_system->registerMachineID(cntrl->getMachineID(), this);

        MachineType mach = cntrl->getType();
        NodeID ver = cntrl->getVersion();
        NodeID global_node_id = MachineType_base_number(mach) + ver;
        MachineID mid = cntrl->getMachineID();

        auto inserted = globalToLocalMap.emplace(global_node_id, local_node_id);
        fatal_if(
            !inserted.second && inserted.first->second != local_node_id,
            "%s: duplicate global node id %d (type=%s, ver=%d) while building local map",
            name(),
            global_node_id,
            MachineType_to_string(mach),
            ver);

        auto local_inserted = machineToLocalMap.emplace(mid, local_node_id);
        fatal_if(
            !local_inserted.second && local_inserted.first->second != local_node_id,
            "%s: duplicate machine id %s while building local map",
            name(),
            MachineIDToString(mid));

        DPRINTF(RubyNetwork, "%s, ctrl name = %s\n", __func__, cntrl->name());
        DPRINTF(RubyNetwork, "%s, ctrl type = %s, localver = %d\n",
                __func__, MachineType_to_string(mach), ver);
        DPRINTF(RubyNetwork, "Global ID: %d -> Local ID: %d\n",
                global_node_id, local_node_id);

        ++local_node_id;
    }

    // Total nodes/controllers in network is equal to the local node count
    // Must make sure this is called after the State Machine constructors
    m_nodes = local_node_id;

    assert(m_nodes != 0);
    assert(m_virtual_networks != 0);

    m_topology_ptr = new Topology(m_nodes, p.routers.size(),
                                m_virtual_networks,
                                p.ext_links, p.int_links);

    // Allocate to and from queues
    // Queues that are getting messages from protocol
    m_toNetQueues.resize(m_nodes);

    // Queues that are feeding the protocol
    m_fromNetQueues.resize(m_nodes);

    m_ordered.resize(m_virtual_networks);
    m_vnet_type_names.resize(m_virtual_networks);

    for (int i = 0; i < m_virtual_networks; i++) {
        m_ordered[i] = false;
    }

    // Initialize the controller's network pointers
    for (std::vector<BasicExtLink*>::const_iterator i = p.ext_links.begin();
        i != p.ext_links.end(); ++i) {
        BasicExtLink *ext_link = (*i);
        AbstractController *abs_cntrl = ext_link->params().ext_node;
        abs_cntrl->initNetworkPtr(this);
        const AddrRangeList &ranges = abs_cntrl->getAddrRanges();
        if (!ranges.empty()) {
            MachineID mid = abs_cntrl->getMachineID();
            AddrMapNode addr_map_node = {
                .id = mid.getNum(),
                .ranges = ranges
            };
            addrMap.emplace(mid.getType(), addr_map_node);
        }
    }

    // Register a callback function for combining the statistics
    statistics::registerDumpCallback([this]() { collateStats(); });

    for (auto &it : dynamic_cast<Network *>(this)->params().ext_links) {
        it->params().ext_node->initNetQueues();
    }
}

Network::~Network()
{
    for (int node = 0; node < m_nodes; node++) {

        // Delete the Message Buffers
        for (auto& it : m_toNetQueues[node]) {
            delete it;
        }

        for (auto& it : m_fromNetQueues[node]) {
            delete it;
        }
    }

    delete m_topology_ptr;
}

uint32_t
Network::MessageSizeType_to_int(MessageSizeType size_type)
{
    switch(size_type) {
    case MessageSizeType_Control:
    case MessageSizeType_Request_Control:
    case MessageSizeType_Reissue_Control:
    case MessageSizeType_Response_Control:
    case MessageSizeType_Writeback_Control:
    case MessageSizeType_Broadcast_Control:
    case MessageSizeType_Multicast_Control:
    case MessageSizeType_Forwarded_Control:
    case MessageSizeType_Invalidate_Control:
    case MessageSizeType_Unblock_Control:
    case MessageSizeType_Persistent_Control:
    case MessageSizeType_Completion_Control:
        return m_control_msg_size;
    case MessageSizeType_Data:
    case MessageSizeType_Response_Data:
    case MessageSizeType_ResponseLocal_Data:
    case MessageSizeType_ResponseL2hit_Data:
    case MessageSizeType_Writeback_Data:
        return m_data_msg_size;
    default:
        panic("Invalid range for type MessageSizeType");
        break;
    }
}

void
Network::checkNetworkAllocation(NodeID local_id, bool ordered,
                                        int network_num,
                                        std::string vnet_type)
{
    fatal_if(local_id >= m_nodes, "Node ID is out of range");
    fatal_if(network_num >= m_virtual_networks, "Network id is out of range");

    if (ordered) {
        m_ordered[network_num] = true;
    }

    m_vnet_type_names[network_num] = vnet_type;
}


void
Network::setToNetQueue(NodeID global_id, bool ordered, int network_num,
                                std::string vnet_type, MessageBuffer *b)
{
    NodeID local_id = getLocalNodeID(global_id);
    DPRINTF(RubyNetwork,"%s, global id = %d, local id = %d, vnet_type = %s, network_num = %d\n",__func__,global_id, local_id,vnet_type,network_num);
    checkNetworkAllocation(local_id, ordered, network_num, vnet_type);

    while (m_toNetQueues[local_id].size() <= network_num) {
        m_toNetQueues[local_id].push_back(nullptr);
    }
    m_toNetQueues[local_id][network_num] = b;
}

void
Network::setFromNetQueue(NodeID global_id, bool ordered, int network_num,
                                std::string vnet_type, MessageBuffer *b)
{
    NodeID local_id = getLocalNodeID(global_id);
    DPRINTF(RubyNetwork,"%s, global id = %d, local id = %d, vnet_type = %s, network_num = %d\n",__func__,global_id, local_id,vnet_type,network_num);
    checkNetworkAllocation(local_id, ordered, network_num, vnet_type);

    while (m_fromNetQueues[local_id].size() <= network_num) {
        m_fromNetQueues[local_id].push_back(nullptr);
    }
    m_fromNetQueues[local_id][network_num] = b;
}

NodeID
Network::addressToNodeID(Addr addr, MachineType mtype)
{
    // Look through the address maps for entries with matching machine
    // type to get the responsible node for this address.
    const auto &matching_ranges = addrMap.equal_range(mtype);
    for (auto it = matching_ranges.first; it != matching_ranges.second; it++) {
        AddrMapNode &node = it->second;
        auto &ranges = node.ranges;
        for (AddrRange &range: ranges) {
            if (range.contains(addr)) {
                // warn("%s: found mapping for address %x mtype=%s, id = %d\n",
                //     name(), addr, mtype,node.id);
                return node.id;
            }
        }
    }

    std::string known_ranges;
    for (auto it = matching_ranges.first; it != matching_ranges.second; it++) {
        for (const AddrRange &range : it->second.ranges) {
            if (!known_ranges.empty()) {
                known_ranges += ", ";
            }
            known_ranges += range.to_string();
        }
    }
    fatal(
        "%s: couldn't map address %#x to machine type %s (known ranges: %s)",
        name(),
        addr,
        MachineType_to_string(mtype),
        known_ranges.c_str());
    return 0;
}

NodeID
Network::getLocalNodeID(NodeID global_id) const
{
    DPRINTF(RubyNetwork, "Mapping global_id=%d\n", global_id);

    MachineID decoded_mid;
    const bool decoded = decodeGlobalNodeID(global_id, decoded_mid);
    if (decoded) {
        auto mit = machineToLocalMap.find(decoded_mid);
        if (mit != machineToLocalMap.end()) {
            return mit->second;
        }
    }

    auto it = globalToLocalMap.find(global_id);
    if (it != globalToLocalMap.end()) {
        return it->second;
    }

    // Legacy fallback for multi-network init ordering:
    // some queue bindings may still pass compact local indices (0..m_nodes-1)
    // before global type bases settle. Only allow this fallback when the
    // decoded machine type is not even present in this local network.
    if (global_id < m_nodes) {
        if (!decoded) {
            return global_id;
        }
        bool local_type_exists = false;
        for (const auto &entry : machineToLocalMap) {
            if (entry.first.type == decoded_mid.type) {
                local_type_exists = true;
                break;
            }
        }
        if (!local_type_exists) {
            return global_id;
        }
    }

    DPRINTF(RubyNetwork, "Printing globalToLocalMap contents:\n");
    for (const auto &entry : globalToLocalMap) {
        DPRINTF(RubyNetwork, "Global ID: %d -> Local ID: %d\n", entry.first, entry.second);
    }

    std::string map_keys;
    for (const auto &entry : globalToLocalMap) {
        if (!map_keys.empty()) {
            map_keys += ",";
        }
        map_keys += std::to_string(entry.first);
    }
    std::string local_mids;
    for (const auto &entry : machineToLocalMap) {
        if (!local_mids.empty()) {
            local_mids += ",";
        }
        local_mids += MachineIDToString(entry.first);
        local_mids += "->";
        local_mids += std::to_string(entry.second);
    }
    std::string decoded_str = "unresolved";
    if (decoded) {
        decoded_str = MachineIDToString(decoded_mid);
    }
    fatal_if(
        true,
        "%s: no local mapping for global node id %d (decoded=%s, known global ids: %s, known local machine ids: %s)",
        name(),
        global_id,
        decoded_str.c_str(),
        map_keys.c_str(),
        local_mids.c_str());

}

} // namespace ruby
} // namespace gem5
