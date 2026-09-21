from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject



class CXL_CRC_check(ClockedObject):
    type = 'CXL_CRC_check'
    cxx_class = 'gem5::CXL_CRC_check'
    cxx_header = 'dev/CXL/CXL_CRC_check.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")

    success_rate = Param.Float(0.9,"success_rate")
