from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject

class CXL_FlexBus(ClockedObject):
    type = 'CXL_FlexBus'
    cxx_class = 'gem5::CXL_FlexBus'
    cxx_header = 'dev/CXL/CXL_FlexBus.hh'


    #CXL port
    Host_resp_port = ResponsePort("Host_resp_port")
    Host_req_port = ResponsePort("Host_req_port")

    #PHY_port
    Device_req_port = RequestPort("Device_req_port")
    Device_resp_port = RequestPort("Device_resp_port")

    #dma_port
    CXL_out_port = RequestPort("CXL_out_port")
    CXL_in_port = ResponsePort("CXL_in_port")



    #pcie_port
    PCIe_req_port = RequestPort("PCIe_req_port")
    PCIe_resp_port = ResponsePort("PCIe_resp_port")

    is_host = Param.Bool(False,"is_host")



    cycleperflit = Param.Int(2,"packing latency cycle per flit")

    max_queue_size = Param.Int(10,"max_queue_size")

