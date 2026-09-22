from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject


class CXL_Packing(ClockedObject):
    type = 'CXL_Packing'
    cxx_class = 'gem5::CXL_Packing'
    cxx_header = 'dev/CXL/CXL_Packing.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")
