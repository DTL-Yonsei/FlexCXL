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

uint32_t Network::m_virtual_networks;
uint32_t Network::m_control_msg_size;
uint32_t Network::m_data_msg_size;

static uint32_t
messageSizeTypeToBytesImpl(MessageSizeType size_type,
                           uint32_t control_msg_size,
                           uint32_t data_msg_size)
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
        return control_msg_size;
      case MessageSizeType_Data:
      case MessageSizeType_Response_Data:
      case MessageSizeType_ResponseLocal_Data:
      case MessageSizeType_ResponseL2hit_Data:
      case MessageSizeType_Writeback_Data:
        return data_msg_size;
      default:
        panic("Invalid range for type MessageSizeType");
    }
}

Network::Network(const Params &p)
     : ClockedObject(p),
       m_control_msg_size_bytes(p.control_msg_size),
       m_data_msg_size_bytes(
           p.data_msg_size +
           (p.data_msg_size_includes_control ? p.control_msg_size : 0))
{
     m_virtual_networks = p.number_of_virtual_networks;
     m_control_msg_size = m_control_msg_size_bytes;

     fatal_if(p.data_msg_size > p.ruby_system->getBlockSizeBytes(),
              "%s: data message size > cache line size", name());
     m_data_msg_size = m_data_msg_size_bytes;

     params().ruby_system->registerNetwork(this);

     // Populate localNodeVersions with the version of each MachineType in
     // this network. This will be used to compute a global to local ID.
     // Do this by looking at the ext_node for each ext_link. There is one
     // ext_node per ext_link and it points to an AbstractController.
     // For RubySystems with one network global and local ID are the same.
     std::unordered_map<MachineType, std::vector<NodeID>> localNodeVersions;
     for (auto &it : params().ext_links) {
         AbstractController *cntrl = it->params().ext_node;
         localNodeVersions[cntrl->getType()].push_back(cntrl->getVersion());
         params().ruby_system->registerMachineID(cntrl->getMachineID(), this);
         DPRINTF(RubyNetwork,"%s, ctrl name = %s\n",__func__,cntrl->name());
     }

     // Compute a local ID for each MachineType using the same order as SLICC
     NodeID local_node_id = 0;

     //esj 2025-03-27
     NodeID ruby_last_global_id = 0;
     int var = 0;
    //  if(name().find("cxl") == std::string::npos && trace::cxl_mode && !trace::cxl_fixed_mode) var = 2; //esj cxl fixed mode option 2025-11-24
     if(name().find("cxl") == std::string::npos && trace::cxl_mode) var = 2; //esj cxl fixed mode option 2025-11-24
     //

     //esj 2025-03-28
     // for (int i = 0; i < MachineType_base_level(MachineType_NUM); ++i) {
     for (int i = 0; i < MachineType_base_level(MachineType_NUM) - var; ++i) {
         MachineType mach = static_cast<MachineType>(i);
         if (localNodeVersions.count(mach)) {
             for (auto &ver : localNodeVersions.at(mach)) {
                 // Get the global ID Ruby will pass around

                //esj 2025-03-27
                 NodeID global_node_id = MachineType_base_number(mach) + ver;
                //esj 2025-04-06
                //  NodeID global_node_id = MachineType_base_number(mach);

                 // NodeID global_node_id = trace::last_global_id + MachineType_base_number(mach) + ver;
                //  DPRINTF(RubyNetwork, "Global ID: %d -> Local ID: %d, ruby_last_global_id = %d, last_global_id=%d \n", global_node_id, local_node_id,ruby_last_global_id,trace::last_global_id);
                //  DPRINTF(RubyNetwork,"network name = %s, machintype = %s\n",name(),MachineType_to_string(mach));
                 globalToLocalMap.emplace(global_node_id, local_node_id);
                 ++local_node_id;
                 //esj 2025-03-27
                //  ++ruby_last_global_id;
             }
         }
     }
     //esj 2025-03-27
    //  trace::last_global_id = ruby_last_global_id;

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
    return messageSizeTypeToBytesImpl(
        size_type, m_control_msg_size, m_data_msg_size);
}

uint32_t
Network::messageSizeTypeToBytes(MessageSizeType size_type) const
{
    return messageSizeTypeToBytesImpl(
        size_type, m_control_msg_size_bytes, m_data_msg_size_bytes);
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
     DPRINTF(RubyNetwork,"%s, global id = %d, local id = %d, vnet_type = %s, network_num = %d\n",global_id, local_id,vnet_type,network_num);
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
                 return node.id;
             }
         }
     }
     return MachineType_base_count(mtype);
 }

 NodeID
 Network::getLocalNodeID(NodeID global_id) const
 {
     DPRINTF(RubyNetwork, "Mapping global_id=%d\n", global_id);

     DPRINTF(RubyNetwork, "Printing globalToLocalMap contents:\n");
     for (const auto &entry : globalToLocalMap) {
         DPRINTF(RubyNetwork, "Global ID: %d -> Local ID: %d\n", entry.first, entry.second);
     }

     assert(globalToLocalMap.count(global_id));


     return globalToLocalMap.at(global_id);



 }

 } // namespace ruby
 } // namespace gem5
