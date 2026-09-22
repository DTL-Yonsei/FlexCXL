from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject


class CXL_CRC(ClockedObject):
    type = "CXL_CRC"
    cxx_class = "gem5::CXL_CRC"
    cxx_header = "dev/CXL/CXL_CRC.hh"

    in_port = ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1, "packing latency cycle per flit")

    max_queue_size = Param.Int(10, "max_queue_size")

    error_rate = Param.Float(0.01, "error_rate")

    validation_seeded_iid_enable = Param.Bool(
        False,
        "Enable the validation-only reproducible packet-level IID error model",
    )
    validation_iid_seed = Param.UInt64(
        1,
        "Per-CRC-object seed for validation-only packet-level IID errors",
    )
    validation_iid_retry_eligible = Param.Bool(
        True,
        "Allow retransmitted data packets to receive an independent IID error",
    )
