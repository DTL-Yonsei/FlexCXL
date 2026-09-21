from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject

class CXL_Encoder(ClockedObject):
    type = 'CXL_Encoder'
    cxx_class = 'gem5::CXL_Encoder'
    cxx_header = 'dev/CXL/CXL_Encoder.hh'

    in_port= ResponsePort("in_port")
    out_port = RequestPort("out_port")

    cycleperflit = Param.Int(1,"packing latency cycle per flit")

    lanes = Param.Int('8' , "Number of lanes on link") # 1,2,4 ,8 or 16 
    speed = Param.NetworkBandwidth('16Gbps', "link speed") #Gen 3 link speed , Gen 1 2.5 Gbps , Gen 2 5 Gbps , Gen 3 8 Gbps. Gen 3 uses 128B/130B encoding , so effective speed = 985 MBPS  

    max_queue_size = Param.Int(10,"max_queue_size")