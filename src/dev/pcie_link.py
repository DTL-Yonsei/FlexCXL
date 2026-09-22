from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject
# class PCIELink(SimObject):
class PCIELink(ClockedObject):
    type = 'PCIELink'
    cxx_class = 'gem5::PCIELink'
    cxx_header = 'dev/pcie_link.hh'

    # speed = Param.Float("speed")
    speed = Param.NetworkBandwidth('16Gbps', "link speed") #Gen 3 link speed , Gen 1 2.5 Gbps , Gen 2 5 Gbps , Gen 3 8 Gbps. Gen 3 uses 128B/130B encoding , so effective speed = 985 MBPS
    delay = Param.Latency('0ns',"delay")
    delay_var = Param.Latency('0ns',"delay_var")
    # delay = Param.Tick(0,"delay")
    # delay_var = Param.Tick(0,"delay_var")
    lanes = Param.Int("lanes")
    max_queue_size = Param.Int("max_queue_siz")
    mps = Param.Int("mps")
    upstreamResponse= ResponsePort("upstreamResponse")
    downstreamResponse = ResponsePort("downstreamResponse")
    upstreamRequest = RequestPort("upstreamRequest")
    downstreamRequest = RequestPort("downstreamRequest")
