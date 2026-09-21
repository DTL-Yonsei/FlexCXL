from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject

class CXL_Framer(ClockedObject):
    type = 'CXL_Framer'
    cxx_class = 'gem5::CXL_Framer'
    cxx_header = 'dev/CXL/CXL_Framer.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")

    logical_PHY_delay = Param.Latency('12ns',"logical_PHY_delay")

