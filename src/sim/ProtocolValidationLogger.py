from m5.params import *
from m5.SimObject import SimObject


class ProtocolValidationLogger(SimObject):
    type = "ProtocolValidationLogger"
    cxx_header = "sim/protocol_validation_logger.hh"
    cxx_class = "gem5::ProtocolValidationLogger"

    enabled = Param.Bool(False, "Enable validation JSONL events")
    corruption_enabled = Param.Bool(
        False,
        "Enable deterministic one-shot corruption independently of logging",
    )
    output_file = Param.String("", "JSONL output path")
    run_id = Param.String("", "Stable validation run identifier")

    corrupt_direction = Param.String(
        "", "Optional deterministic corruption direction: H2D or D2H"
    )
    corrupt_seq = Param.Int(
        -1, "Legacy single deterministic corruption sequence"
    )
    corrupt_seqs = VectorParam.Int([], "Deterministic corruption sequence set")
    corrupt_attempt = Param.Int(0, "Transmission attempt to corrupt")
