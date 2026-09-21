from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject


class CXL_Unpacking(ClockedObject):
    type = 'CXL_Unpacking'
    cxx_class = 'gem5::CXL_Unpacking'
    cxx_header = 'dev/CXL/CXL_Unpacking.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")