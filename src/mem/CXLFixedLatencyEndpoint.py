from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class CXLFixedLatencyEndpoint(AbstractMemory):
    type = "CXLFixedLatencyEndpoint"
    cxx_header = "mem/CXLFixedLatencyEndpoint.hh"
    cxx_class = "gem5::memory::CXLFixedLatencyEndpoint"

    request_port = ResponsePort(
        "Receives CXL.mem timing requests and issues request retries"
    )
    response_port = ResponsePort(
        "Sends CXL.mem timing responses and receives response retries"
    )

    response_latency = Param.Latency(
        "20ns", "Latency from request acceptance to first response attempt"
    )
    capacity = Param.Unsigned(
        64, "Maximum number of accepted requests awaiting response delivery"
    )

    # This endpoint is a validation oracle behind the CXL BAR, not system RAM.
    in_addr_map = False
    kvm_map = False
    conf_table_reported = False
