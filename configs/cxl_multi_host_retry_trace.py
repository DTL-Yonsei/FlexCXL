#!/usr/bin/env python3

"""Run four synthetic traces through the production multi-host CXL path.

This OS-less harness replaces only the CPUs and guests with PyTrafficGen.
Requests still traverse each host CXL controller, the shared PCIe switch,
the CXL Ruby/Garnet link, the device CXL controller, and CXLDRAMsim3.
"""

import argparse
import json
from pathlib import Path
import sys

import m5
from m5.objects import (
    AddrRange,
    CXLHomeAgent,
    Pc,
    PortTerminator,
    PyTrafficGen,
    Root,
    RubySystem,
    SimpleMemory,
    SrcClockDomain,
    System,
    VoltageDomain,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "configs"))

from common import ObjectList, Options  # noqa: E402
from network import Network  # noqa: E402
from ruby import MESI_Two_Level, Ruby  # noqa: E402
from x86_FS_config import (
    connectX86CXLMemory2,
    connectX86RubySystem,
)  # noqa: E402


HOST_CONTROLLERS = ("CXL_ctrl", "CXL_ctrl3", "CXL_ctrl4", "CXL_ctrl5")
HOST_COUNT = 4
HPA_BASE = 0x1_0000_0000
BAR_BASE = 0xC000_0000


def parse_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    Options.addNoISAOptions(parser)
    Ruby.define_options(parser)
    parser.add_argument("--error-rate", type=float, default=0.10)
    parser.add_argument("--per-host-cxl-size", default="128MiB")
    parser.add_argument("--traffic-duration", default="8us")
    parser.add_argument("--traffic-period", default="200ns")
    parser.add_argument("--drain-duration", default="200us")
    parser.add_argument(
        "--progress-check",
        default="1ms",
        help="maximum TrafficGen interval without a completed request",
    )
    parser.add_argument("--working-set", default="4KiB")
    parser.add_argument(
        "--working-set-offset",
        default="0B",
        help="per-host offset of the traced address range",
    )
    parser.add_argument("--controller-clock", default="250MHz")
    parser.add_argument("--link-clock", default="250MHz")
    parser.add_argument(
        "--device-controller-queue-size",
        type=int,
        default=0,
        help=(
            "override only the device CXL controller queue/replay capacity; "
            "zero keeps the platform default"
        ),
    )
    parser.add_argument("--max-outstanding-reqs", type=int, default=32)
    parser.add_argument(
        "--read-percent",
        type=int,
        default=100,
        help="percentage of generated memory operations that are reads",
    )
    parser.add_argument("--case-id", default="rrsm-lrsm-host4-error")
    parser.add_argument(
        "--cpu-ruby-host0",
        action="store_true",
        help=(
            "route host0 traffic through the production CPU Ruby hierarchy "
            "before the CXL home agent"
        ),
    )
    parser.add_argument(
        "--cpu-ruby-deadlock-threshold",
        type=int,
        default=0,
        help=(
            "host0 CPU Ruby Sequencer deadlock threshold in cycles; zero "
            "keeps the production default"
        ),
    )
    args = parser.parse_args()
    if not 0.0 <= args.error_rate <= 1.0:
        parser.error("--error-rate must be within [0, 1]")
    if args.device_controller_queue_size < 0:
        parser.error("--device-controller-queue-size must be non-negative")
    if args.max_outstanding_reqs <= 0:
        parser.error("--max-outstanding-reqs must be positive")
    if not 0 <= args.read_percent <= 100:
        parser.error("--read-percent must be within [0, 100]")
    if args.cpu_ruby_deadlock_threshold < 0:
        parser.error("--cpu-ruby-deadlock-threshold must be non-negative")
    return args


def latency_ticks(value):
    from m5.util import convert

    return m5.ticks.fromSeconds(convert.toLatency(value))


def memory_size(value):
    from m5.util import convert

    return int(convert.toMemorySize(value))


def configure_network_args(args):
    args.network = "garnet"
    args.topology = "Pt2Pt"
    args.mesh_rows = 0
    args.vcs_per_vnet = 4
    args.routing_algorithm = 0
    args.garnet_deadlock_threshold = 500000
    args.network_fault_model = False
    args.simple_physical_channels = False
    args.link_latency = 1
    args.router_latency = 1
    args.link_width_bits = 64 * 8
    args.cacheline_size = 64
    args.CXL_clock = args.controller_clock


def create_cxl_ruby(system, args):
    system.cxl = RubySystem()
    (
        network,
        int_link_class,
        ext_link_class,
        router_class,
        interface_class,
    ) = Network.create_network(args, system.cxl)
    system.cxl.network = network
    network.control_msg_size = 16
    network.data_msg_size = 64

    topology = MESI_Two_Level.create_system_cxl(
        args, False, system, [], None, system.cxl, []
    )
    topology.makeTopology(
        args, network, int_link_class, ext_link_class, router_class
    )
    topology.registerTopology(args)
    Network.init_network(args, network, interface_class, is_cxl=True)
    system.cxl.number_of_virtual_networks = network.number_of_virtual_networks
    system.cxl.num_of_sequencers = 2


def apply_link_clock(system, clock):
    system.cxl_link_clk_domain = SrcClockDomain(
        clock=clock, voltage_domain=system.voltage_domain
    )
    domain = system.cxl_link_clk_domain
    network = system.cxl.network
    network.clk_domain = domain
    for router in network.routers:
        router.clk_domain = domain
    for netif in network.netifs:
        netif.clk_domain = domain
    for int_link in network.int_links:
        int_link.network_link.clk_domain = domain
        int_link.credit_link.clk_domain = domain
        for name in (
            "src_net_bridge",
            "src_cred_bridge",
            "dst_net_bridge",
            "dst_cred_bridge",
        ):
            getattr(int_link, name).clk_domain = domain
    for ext_link in network.ext_links:
        for link in ext_link.network_links:
            link.clk_domain = domain
        for link in ext_link.credit_links:
            link.clk_domain = domain
        for name in (
            "ext_net_bridge",
            "ext_cred_bridge",
            "int_net_bridge",
            "int_cred_bridge",
        ):
            for bridge in getattr(ext_link, name):
                bridge.clk_domain = domain


def traffic_states(
    generator, start, size, duration, period, drain, read_percent
):
    yield generator.createLinear(
        duration,
        start,
        start + size - 64,
        64,
        period,
        period,
        read_percent,
        0,
    )
    # Keep a live state beyond the explicit simulation limit. Exhausting the
    # state list makes BaseTrafficGen dereference an empty generator.
    yield generator.createIdle(drain * 2)


def main():
    args = parse_args()
    configure_network_args(args)
    per_host_size = memory_size(args.per_host_cxl_size)
    working_set = memory_size(args.working_set)
    working_set_offset = memory_size(args.working_set_offset)
    if working_set < 64 or working_set_offset + working_set > per_host_size:
        raise ValueError("working set must fit within each host capacity")
    if working_set_offset % 64:
        raise ValueError("working set offset must be cache-line aligned")

    total_cxl_size = per_host_size * HOST_COUNT
    host_offsets = [host * per_host_size for host in range(HOST_COUNT)]
    host_ranges = [
        AddrRange(HPA_BASE + offset, size=per_host_size)
        for offset in host_offsets
    ]

    ObjectList.cxl_mode = True
    ObjectList.switch_mode = True
    ObjectList.routing_mode_type = "round-robin"
    ObjectList.priority_limit = 1
    ObjectList.priority_host_ratios = "1,1,1,1"
    ObjectList.cxl_error_rate = args.error_rate
    ObjectList.cxl_mem_start = HPA_BASE
    ObjectList.cxl_mem_size = per_host_size
    ObjectList.cxl_logical_device_count = HOST_COUNT
    ObjectList.cxl_host_visible_sizes = [per_host_size] * HOST_COUNT
    ObjectList.cxl_host_device_offsets = host_offsets
    ObjectList.cxl_mem_as_system_ram = False
    ObjectList.cxl_home_agent_direct_validation = args.cpu_ruby_host0

    generators = [
        PyTrafficGen(
            elastic_req=True,
            max_outstanding_reqs=args.max_outstanding_reqs,
            progress_check=args.progress_check,
            cpu_id=host_id,
        )
        for host_id in range(HOST_COUNT)
    ]

    if args.cpu_ruby_host0:
        args.num_cpus = 1
        args.num_dirs = 2
        args.num_l2caches = 1
        args.CXL_clock = args.controller_clock
        system_mem_ranges = [AddrRange(0, size="3GiB")]
    else:
        system_mem_ranges = host_ranges

    system = System(
        cpu=[generators[0]] if args.cpu_ruby_host0 else [],
        mem_mode="timing",
        mem_ranges=system_mem_ranges,
        cache_line_size=64,
    )
    system.voltage_domain = VoltageDomain()
    system.clk_domain = SrcClockDomain(
        clock="1GHz", voltage_domain=system.voltage_domain
    )
    # BaseTrafficGen consults PhysicalMemory, not only System.mem_ranges,
    # before issuing a request. This unconnected null memory registers the
    # trace HPA aperture; traffic itself uses the explicit home-agent ports.
    system.trace_address_map = SimpleMemory(
        range=AddrRange(HPA_BASE, size=total_cxl_size),
        null=True,
        conf_table_reported=False,
        kvm_map=False,
    )
    system.pc = Pc()
    connectX86RubySystem(system)
    system.pc.south_bridge.ide.dma = system.iobus.cpu_side_ports
    if not args.cpu_ruby_host0:
        system.system_port = system.iobus.cpu_side_ports

    connectX86CXLMemory2(
        system,
        [],
        cxl_size=total_cxl_size,
        cxl_device_size=total_cxl_size,
        Ruby=True,
        connect_primary_host_upstream=False,
    )
    if args.device_controller_queue_size:
        system.CXL_ctrl2.max_queue_size = args.device_controller_queue_size
        system.CXL_ctrl2.read_queue_size = (
            args.device_controller_queue_size // 2
        )
        for component_name in (
            "crc_checker",
            "crc",
            "packing",
            "unpacking",
            "flexbus",
            "framer",
            "deframer",
            "decoder",
            "encoder",
        ):
            getattr(
                system.CXL_ctrl2, component_name
            ).max_queue_size = args.device_controller_queue_size

    system.cxl_controller_clk_domain = SrcClockDomain(
        clock=args.controller_clock, voltage_domain=system.voltage_domain
    )
    for name in (*HOST_CONTROLLERS, "CXL_ctrl2"):
        getattr(system, name).clk_domain = system.cxl_controller_clk_domain

    if args.cpu_ruby_host0:
        Ruby.create_system(
            args,
            False,
            system,
            piobus=system.iobus,
            dma_ports=[],
            bootmem=None,
            cpus=[generators[0]],
            is_second=False,
        )
        system.ruby.clk_domain = SrcClockDomain(
            clock=args.ruby_clock, voltage_domain=system.voltage_domain
        )
        if args.cpu_ruby_deadlock_threshold:
            system.ruby._cpu_ports[
                0
            ].deadlock_threshold = args.cpu_ruby_deadlock_threshold
        generators[0].port = system.ruby._cpu_ports[0].in_ports
    else:
        create_cxl_ruby(system, args)
    apply_link_clock(system, args.link_clock)

    controllers = [getattr(system, name) for name in HOST_CONTROLLERS]
    system.unused_home_agent_ports = PortTerminator()
    first_direct_host = 1 if args.cpu_ruby_host0 else 0
    for host_id in range(first_direct_host, HOST_COUNT):
        controller = controllers[host_id]
        generator = generators[host_id]
        home_agent = CXLHomeAgent(
            clk_domain=system.cxl_controller_clk_domain,
            cxl_mem_start=HPA_BASE + host_offsets[host_id],
            cxl_mem_size=per_host_size,
            cxl_device_offset=host_offsets[host_id],
            cxl_bar_start=BAR_BASE,
            cxl_bar_size=system.pc.cxlmemdevice1.BAR0.size,
            response_latency="1ns",
            validation_force_cxl_ready=True,
            validation_host_id=host_id,
        )
        generator.port = home_agent.ruby_side_1
        system.unused_home_agent_ports.req_ports = home_agent.ruby_side
        home_agent.cxl_side = controller.upstreamResponse
        home_agent.cxl_response_side = controller.upstreamResponse2
        setattr(system, f"traffic_generator{host_id}", generator)
        setattr(system, f"cxl_trace_home_agent{host_id}", home_agent)

    root = Root(full_system=False, system=system)
    m5.ticks.setGlobalFrequency("1THz")
    m5.instantiate()

    traffic_duration = latency_ticks(args.traffic_duration)
    traffic_period = latency_ticks(args.traffic_period)
    drain_duration = latency_ticks(args.drain_duration)
    for host_id, generator in enumerate(generators):
        generator.start(
            traffic_states(
                generator,
                HPA_BASE + host_offsets[host_id] + working_set_offset,
                working_set,
                traffic_duration,
                traffic_period,
                drain_duration,
                args.read_percent,
            )
        )

    exit_event = m5.simulate(traffic_duration + drain_duration)
    m5.stats.dump()
    manifest = {
        "case_id": args.case_id,
        "num_hosts": HOST_COUNT,
        "error_rate": args.error_rate,
        "traffic_duration_ticks": traffic_duration,
        "traffic_period_ticks": traffic_period,
        "drain_duration_ticks": drain_duration,
        "progress_check_ticks": latency_ticks(args.progress_check),
        "working_set_bytes": working_set,
        "working_set_offset_bytes": working_set_offset,
        "device_controller_queue_size": (
            args.device_controller_queue_size or "platform-default"
        ),
        "max_outstanding_reqs": args.max_outstanding_reqs,
        "read_percent": args.read_percent,
        "cpu_ruby_host0": args.cpu_ruby_host0,
        "cpu_ruby_deadlock_threshold_cycles": (
            args.cpu_ruby_deadlock_threshold or "production-default"
        ),
        "host_hpa_starts": [HPA_BASE + offset for offset in host_offsets],
        "host_device_offsets": host_offsets,
        "exit_tick": m5.curTick(),
        "exit_cause": exit_event.getCause(),
        "path": [
            "PyTrafficGen",
            "CXLHomeAgent",
            "host CXL_ctrl",
            "PCIESwitch",
            "CXL Ruby/Garnet",
            "device CXL_ctrl",
            "CXLDRAMsim3",
        ],
    }
    output = Path(m5.options.outdir) / "multi_host_retry_trace.json"
    output.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


main()
