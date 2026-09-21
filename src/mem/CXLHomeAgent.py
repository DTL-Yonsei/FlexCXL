from m5.params import *
from m5.objects.ClockedObject import ClockedObject


class CXLHomeAgent(ClockedObject):
    type = "CXLHomeAgent"
    cxx_header = "mem/cxl_home_agent.hh"
    cxx_class = "gem5::memory::CXLHomeAgent"

    ruby_side = ResponsePort("Ruby directory CXL memory request port")
    ruby_side_1 = ResponsePort("Ruby directory CXL memory response port")
    cxl_side = RequestPort("CXL host controller request port")
    cxl_response_side = RequestPort(
        "Optional split CXL host controller response port"
    )

    cxl_mem_start = Param.Addr(0, "CXL memory HPA window start")
    cxl_mem_size = Param.UInt64(0, "CXL memory HPA window size")
    cxl_device_offset = Param.UInt64(
        0, "Offset of this host window inside the shared CXL device"
    )
    cxl_bar_start = Param.Addr(0xC0000000, "CXL endpoint BAR window start")
    cxl_bar_size = Param.UInt64(0x20000, "CXL endpoint BAR window size")
    response_latency = Param.Latency("1ns", "Response delay back to Ruby")

    validation_force_cxl_ready = Param.Bool(
        False,
        "Test-only equivalent of completed CXL enumeration; default is off",
    )
    validation_host_id = Param.Unsigned(
        0,
        "Source host identity stamped only when validation_force_cxl_ready is set",
    )
