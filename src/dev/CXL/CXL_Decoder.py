from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject

class CXL_Decoder(ClockedObject):
    type = 'CXL_Decoder'
    cxx_class = 'gem5::CXL_Decoder'
    cxx_header = 'dev/CXL/CXL_Decoder.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")

