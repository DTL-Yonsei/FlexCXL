/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
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

#include "mem/ruby/network/Topology.hh"

#include <algorithm>
#include <cassert>
#include <map>

#include "base/trace.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/network/BasicLink.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/slicc_interface/AbstractController.hh"

namespace gem5
{

namespace ruby
{

const int INFINITE_LATENCY = 10000; // Yes, this is a big hack

// Note: In this file, we use the first 2*m_nodes SwitchIDs to
// represent the input and output endpoint links.  These really are
// not 'switches', as they will not have a Switch object allocated for
// them. The first m_nodes SwitchIDs are the links into the network,
// the second m_nodes set of SwitchIDs represent the the output queues
// of the network.

Topology::Topology(uint32_t num_nodes, uint32_t num_routers,
                uint32_t num_vnets,
                const std::vector<BasicExtLink *> &ext_links,
                const std::vector<BasicIntLink *> &int_links)
    : m_nodes(num_nodes),
    m_number_of_switches(num_routers), m_vnets(num_vnets),
    m_ext_link_vector(ext_links), m_int_link_vector(int_links),
    m_endpointMachines(num_nodes)
{

    //esj 2025-04-17
    // if(m_nodes == 0 || m_nodes == 2) m_nodes = MachineType_base_number(MachineType_MiscNode);
    // DPRINTF(RubyNetwork,"%s, topology name = %s, m_nodes = %d\n",__func__,name(),m_nodes);
    ///

    // Total nodes/controllers in network
    assert(m_nodes > 1);

    // analyze both the internal and external links, create data structures.
    // The python created external links are bi-directional,
    // and the python created internal links are uni-directional.
    // The networks and topology utilize uni-directional links.
    // Thus each external link is converted to two calls to addLink,
    // one for each direction.
    //
    // Build a per-network machine-type base map so endpoint indexing stays
    // local to this network, regardless of global controller counts.
    std::map<MachineType, int> local_type_counts;
    for (auto ext_link : ext_links) {
        auto *abs_cntrl = ext_link->params().ext_node;
        local_type_counts[abs_cntrl->getType()]++;
    }

    std::vector<MachineType> ordered_types;
    ordered_types.reserve(local_type_counts.size());
    for (const auto &entry : local_type_counts) {
        ordered_types.push_back(entry.first);
    }

    std::sort(
        ordered_types.begin(),
        ordered_types.end(),
        [](MachineType a, MachineType b) {
            return MachineType_base_level(a) < MachineType_base_level(b);
        });

    std::map<MachineType, int> local_type_base;
    int next_base = 0;
    for (auto type : ordered_types) {
        local_type_base[type] = next_base;
        next_base += local_type_counts[type];
    }

    std::map<MachineType, int> local_type_seen;

    // External Links
    for (std::vector<BasicExtLink*>::const_iterator i = ext_links.begin();
        i != ext_links.end(); ++i) {
        BasicExtLink *ext_link = (*i);
        AbstractController *abs_cntrl = ext_link->params().ext_node;
        BasicRouter *router = ext_link->params().int_node;
        
        DPRINTF(RubyNetwork,"%s, ctrl name = %s, m_nodes = %d\n",__func__,abs_cntrl->name(),m_nodes);

        MachineType machine_type = abs_cntrl->getType();
        int machine_base_idx = local_type_base[machine_type];
        int local_ver = local_type_seen[machine_type]++;

        int ext_idx1 = machine_base_idx + local_ver;
        int ext_idx2 = ext_idx1 + m_nodes;
        int int_idx = router->params().router_id + 2*m_nodes;
        DPRINTF(RubyNetwork, "%s, machin id = %d, m_nodes = %d, machine_base_idx = %d,router->params().router_id = %d, ext_idx1 = %d, ext_idx2 = %d, int_idx = %d\n",
            __func__,abs_cntrl->getType(),m_nodes,machine_base_idx,router->params().router_id,ext_idx1,ext_idx2,int_idx);
        assert(ext_idx1 >= 0 && ext_idx1 < m_nodes);
        m_endpointMachines[ext_idx1] = abs_cntrl->getMachineID();
        // create the internal uni-directional links in both directions
        // ext to int
        DPRINTF(RubyNetwork, "%s, add link ext to int\n",__func__);
        addLink(ext_idx1, int_idx, ext_link);
        // int to ext
        DPRINTF(RubyNetwork, "%s, add link int to ext\n",__func__);
        addLink(int_idx, ext_idx2, ext_link);
    }

    // Internal Links
    for (std::vector<BasicIntLink*>::const_iterator i = int_links.begin();
        i != int_links.end(); ++i) {
        BasicIntLink *int_link = (*i);
        BasicRouter *router_src = int_link->params().src_node;
        BasicRouter *router_dst = int_link->params().dst_node;

        PortDirection src_outport = int_link->params().src_outport;
        PortDirection dst_inport = int_link->params().dst_inport;

        // Store the IntLink pointers for later
        m_int_link_vector.push_back(int_link);

        int src = router_src->params().router_id + 2*m_nodes;
        int dst = router_dst->params().router_id + 2*m_nodes;

        // create the internal uni-directional link from src to dst
        addLink(src, dst, int_link, src_outport, dst_inport);
    }
}

void
Topology::createLinks(Network *net)
{
    // Find maximum switchID
    SwitchID max_switch_id = 0;
    for (LinkMap::const_iterator i = m_link_map.begin();
        i != m_link_map.end(); ++i) {
        std::pair<SwitchID, SwitchID> src_dest = (*i).first;
        max_switch_id = std::max(max_switch_id, src_dest.first);
        max_switch_id = std::max(max_switch_id, src_dest.second);
        DPRINTF(RubyNetwork,"%s, max_switch_id = %d, src_dest.first = %d, src_dest.second =%d\n",__func__,max_switch_id,src_dest.first,src_dest.second);
    }
    
    //esj 2025-03-27
    bool is_cxl = false;
    bool is_drive_sys = false;
    if(net->name().find("drivesys") != std::string::npos) is_drive_sys = true;
    DPRINTF(RubyNetwork,"%s, net->name() = %s\n",__func__,net->name());
    
    if(net->name().find("cxl") != std::string::npos) is_cxl = true;
    //

    // Initialize weight, latency, and inter switched vectors
    int num_switches = max_switch_id+1;
    Matrix topology_weights(m_vnets,
            std::vector<std::vector<int>>(num_switches,
            std::vector<int>(num_switches, INFINITE_LATENCY)));
    Matrix component_latencies(num_switches,
            std::vector<std::vector<int>>(num_switches,
            std::vector<int>(m_vnets, -1)));
    Matrix component_inter_switches(num_switches,
            std::vector<std::vector<int>>(num_switches,
            std::vector<int>(m_vnets, 0)));

    // Set identity weights to zero
    for (int i = 0; i < topology_weights[0].size(); i++) {
        for (int v = 0; v < m_vnets; v++) {
            topology_weights[v][i][i] = 0;
        }
    }

    // Fill in the topology weights and bandwidth multipliers
    for (auto link_group : m_link_map) {
        std::pair<int, int> src_dest = link_group.first;
        std::vector<bool> vnet_done(m_vnets, 0);
        int src = src_dest.first;
        int dst = src_dest.second;

        // Iterate over all links for this source and destination
        std::vector<LinkEntry> link_entries = link_group.second;
        for (int l = 0; l < link_entries.size(); l++) {
            BasicLink* link = link_entries[l].link;
            if (link->mVnets.size() == 0) {
                for (int v = 0; v < m_vnets; v++) {
                    // Two links connecting same src and destination
                    // cannot carry same vnets.
                    fatal_if(vnet_done[v], "Two links connecting same src"
                    " and destination cannot support same vnets");

                    component_latencies[src][dst][v] = link->m_latency;
                    topology_weights[v][src][dst] = link->m_weight;
                    vnet_done[v] = true;
                    DPRINTF(RubyNetwork,"%s, topology_weights[v=%d][src=%d][dst=%d] = %d\n",__func__,v,src,dst,link->m_weight);
                }
            } else {
                for (int v = 0; v < link->mVnets.size(); v++) {
                    int vnet = link->mVnets[v];
                    fatal_if(vnet >= m_vnets, "Not enough virtual networks "
                            "(setting latency and weight for vnet %d)", vnet);
                    // Two links connecting same src and destination
                    // cannot carry same vnets.
                    fatal_if(vnet_done[vnet], "Two links connecting same src"
                    " and destination cannot support same vnets");

                    component_latencies[src][dst][vnet] = link->m_latency;
                    topology_weights[vnet][src][dst] = link->m_weight;
                    DPRINTF(RubyNetwork,"%s, topology_weights[v=%d][src=%d][dst=%d] = %d\n",__func__,vnet,src,dst,link->m_weight);
                    vnet_done[vnet] = true;
                }
            }
        }
    }

    // Walk topology and hookup the links
    Matrix dist = shortest_path(topology_weights, component_latencies,
                                component_inter_switches);

    for (int i = 0; i < topology_weights[0].size(); i++) {
        for (int j = 0; j < topology_weights[0][i].size(); j++) {
            std::vector<NetDest> routingMap;
            routingMap.resize(m_vnets);

            // Not all sources and destinations are connected
            // by direct links. We only construct the links
            // which have been configured in topology.
            bool realLink = false;

            for (int v = 0; v < m_vnets; v++) {
                int weight = topology_weights[v][i][j];
                if (weight > 0 && weight != INFINITE_LATENCY) {
                    realLink = true;
                    routingMap[v] =
                        shortest_path_to_node(i, j, topology_weights, dist, v, is_cxl, is_drive_sys); //esj 2025-03-27
                }
            }
            // Make one link for each set of vnets between
            // a given source and destination. We do not
            // want to create one link for each vnet.
            if (realLink) {
                makeLink(net, i, j, routingMap);
            }
            else{
                DPRINTF(RubyNetwork,"%s, not real link , src = %d, dst = %d\n",__func__,i,j);
            }
        }
    }
}

void
Topology::addLink(SwitchID src, SwitchID dest, BasicLink* link,
                PortDirection src_outport_dirn,
                PortDirection dst_inport_dirn)
{
    //esj 2025-03-26
    DPRINTF(RubyNetwork,"%s,sj src = %d, dest = %d, m_number_of_switches = %d,m_nodes = %d \n",__func__,src,dest,m_number_of_switches,m_nodes);
    assert(src <= m_number_of_switches+m_nodes+m_nodes);
    assert(dest <= m_number_of_switches+m_nodes+m_nodes);

    std::pair<int, int> src_dest_pair;
    src_dest_pair.first = src;
    src_dest_pair.second = dest;
    LinkEntry link_entry;

    link_entry.link = link;
    link_entry.src_outport_dirn = src_outport_dirn;
    link_entry.dst_inport_dirn  = dst_inport_dirn;

    auto lit = m_link_map.find(src_dest_pair);
    if (lit != m_link_map.end()) {
        // HeteroGarnet allows multiple links between
        // same source-destination pair supporting
        // different vnets. If there is a link already
        // between a given pair of source and destination
        // add this new link to it.
        lit->second.push_back(link_entry);
    } else {
        std::vector<LinkEntry> links;
        links.push_back(link_entry);
        m_link_map[src_dest_pair] = links;
    }
}

void
Topology::makeLink(Network *net, SwitchID src, SwitchID dest,
                std::vector<NetDest>& routing_table_entry)
{
    // Make sure we're not trying to connect two end-point nodes
    // directly together
    assert(src >= 2 * m_nodes || dest >= 2 * m_nodes);

    std::pair<int, int> src_dest;
    LinkEntry link_entry;

    DPRINTF(RubyNetwork,"%s, src = %d, dst = %s, m_nodes = %d\n",__func__,src,dest,m_nodes);

    if (src < m_nodes) {
        src_dest.first = src;
        src_dest.second = dest;
        std::vector<LinkEntry> links = m_link_map[src_dest];
        for (int l = 0; l < links.size(); l++) {
            link_entry = links[l];
            std::vector<NetDest> linkRoute;
            linkRoute.resize(m_vnets);
            BasicLink *link = link_entry.link;
            if (link->mVnets.size() == 0) {
                net->makeExtInLink(src, dest - (2 * m_nodes), link,
                                routing_table_entry);
            } else {
                for (int v = 0; v< link->mVnets.size(); v++) {
                    int vnet = link->mVnets[v];
                    linkRoute[vnet] = routing_table_entry[vnet];
                }
                net->makeExtInLink(src, dest - (2 * m_nodes), link,
                                linkRoute);
            }
        }
    } else if (dest < 2*m_nodes) {
        assert(dest >= m_nodes);
        NodeID node = dest - m_nodes;
        src_dest.first = src;
        src_dest.second = dest;
        std::vector<LinkEntry> links = m_link_map[src_dest];
        for (int l = 0; l < links.size(); l++) {
            link_entry = links[l];
            std::vector<NetDest> linkRoute;
            linkRoute.resize(m_vnets);
            BasicLink *link = link_entry.link;
            if (link->mVnets.size() == 0) {
                net->makeExtOutLink(src - (2 * m_nodes), node, link,
                                routing_table_entry);
            } else {
                for (int v = 0; v< link->mVnets.size(); v++) {
                    int vnet = link->mVnets[v];
                    linkRoute[vnet] = routing_table_entry[vnet];
                }
                net->makeExtOutLink(src - (2 * m_nodes), node, link,
                                linkRoute);
            }
        }
    } else {
        assert((src >= 2 * m_nodes) && (dest >= 2 * m_nodes));
        src_dest.first = src;
        src_dest.second = dest;
        std::vector<LinkEntry> links = m_link_map[src_dest];
        for (int l = 0; l < links.size(); l++) {
            link_entry = links[l];
            std::vector<NetDest> linkRoute;
            linkRoute.resize(m_vnets);
            BasicLink *link = link_entry.link;
            if (link->mVnets.size() == 0) {
                net->makeInternalLink(src - (2 * m_nodes),
                            dest - (2 * m_nodes), link, routing_table_entry,
                            link_entry.src_outport_dirn,
                            link_entry.dst_inport_dirn);
            } else {
                for (int v = 0; v< link->mVnets.size(); v++) {
                    int vnet = link->mVnets[v];
                    linkRoute[vnet] = routing_table_entry[vnet];
                }
                net->makeInternalLink(src - (2 * m_nodes),
                            dest - (2 * m_nodes), link, linkRoute,
                            link_entry.src_outport_dirn,
                            link_entry.dst_inport_dirn);
            }
        }
    }
}

// The following all-pairs shortest path algorithm is based on the
// discussion from Cormen et al., Chapter 26.1.
void
Topology::extend_shortest_path(Matrix &current_dist, Matrix &latencies,
    Matrix &inter_switches)
{
    int nodes = current_dist[0].size();

    // We find the shortest path for each vnet for a given pair of
    // source and destinations. This is done simply by traversing via
    // all other nodes and finding the minimum distance.
    for (int v = 0; v < m_vnets; v++) {
        // There is a different topology for each vnet. Here we try to
        // build a topology by finding the minimum number of intermediate
        // switches needed to reach the destination
        bool change = true;
        while (change) {
            change = false;
            for (int i = 0; i < nodes; i++) {
                for (int j = 0; j < nodes; j++) {
                    // We follow an iterative process to build the shortest
                    // path tree:
                    // 1. Start from the direct connection (if there is one,
                    // otherwise assume a hypothetical infinite weight link).
                    // 2. Then we iterate through all other nodes considering
                    // new potential intermediate switches. If we find any
                    // lesser weight combination, we set(update) that as the
                    // new weight between the source and destination.
                    // 3. Repeat for all pairs of nodes.
                    // 4. Go to step 1 if there was any new update done in
                    // Step 2.
                    int minimum = current_dist[v][i][j];
                    int previous_minimum = minimum;
                    int intermediate_switch = -1;
                    for (int k = 0; k < nodes; k++) {
                        minimum = std::min(minimum,
                            current_dist[v][i][k] + current_dist[v][k][j]);
                        if (previous_minimum != minimum) {
                            intermediate_switch = k;
                            inter_switches[i][j][v] =
                                inter_switches[i][k][v] +
                                inter_switches[k][j][v] + 1;
                        }
                        previous_minimum = minimum;
                    }
                    if (current_dist[v][i][j] != minimum) {
                        change = true;
                        current_dist[v][i][j] = minimum;
                        assert(intermediate_switch >= 0);
                        assert(intermediate_switch < latencies[i].size());
                        latencies[i][j][v] =
                            latencies[i][intermediate_switch][v] +
                            latencies[intermediate_switch][j][v];
                    }
                }
            }
        }
    }
}

Matrix
Topology::shortest_path(const Matrix &weights, Matrix &latencies,
                        Matrix &inter_switches)
{
    Matrix dist = weights;
    extend_shortest_path(dist, latencies, inter_switches);
    return dist;
}

bool
Topology::link_is_shortest_path_to_node(SwitchID src, SwitchID next,
                                        SwitchID final, const Matrix &weights,
                                        const Matrix &dist, int vnet)
{
    return weights[vnet][src][next] + dist[vnet][next][final] ==
        dist[vnet][src][final];
}

NetDest
Topology::shortest_path_to_node(SwitchID src, SwitchID next,
                                const Matrix &weights, const Matrix &dist,
                                int vnet, bool is_cxl, bool is_drive_sys) //esj 2025-03-27
{
    (void)is_cxl;
    (void)is_drive_sys;

    NetDest result;
    const int max_machines = m_nodes;

    DPRINTF(
        RubyNetwork,
        "%s, name = %s, local max_machines = %d, next = %d\n",
        __func__,
        name(),
        max_machines,
        next);

    for (NodeID d = 0; d < m_nodes; ++d) {
        if (!m_endpointMachines[d].isValid()) {
            continue;
        }
        // Destination switches for machine endpoints are indexed as
        // [m_nodes ... 2*m_nodes-1] in this local topology.
        if (link_is_shortest_path_to_node(
                src, next, d + max_machines, weights, dist, vnet)) {
            DPRINTF(
                RubyNetwork,
                "%s, adding local endpoint %d -> %s\n",
                __func__,
                d,
                MachineIDToString(m_endpointMachines[d]).c_str());
            result.add(m_endpointMachines[d]);
        }
    }

    DPRINTF(RubyNetwork, "Returning shortest path\n"
            "(src-(2*max_machines)): %d, (next-(2*max_machines)): %d, "
            "src: %d, next: %d, vnet:%d result: %s\n",
            (src-(2*max_machines)), (next-(2*max_machines)),
            src, next, vnet, result);

    return result;
}

} // namespace ruby
} // namespace gem5
