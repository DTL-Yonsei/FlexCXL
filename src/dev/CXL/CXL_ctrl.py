from m5.params import *
from m5.SimObject import *
from m5.objects.ClockedObject import ClockedObject
from m5.objects.CXL_Encoder import CXL_Encoder
from m5.objects.CXL_Decoder import CXL_Decoder
from m5.objects.CXL_Framer import CXL_Framer
from m5.objects.CXL_Deframer import CXL_Deframer
from m5.objects.CXL_CRC import CXL_CRC
from m5.objects.CXL_Packing import CXL_Packing
from m5.objects.CXL_Unpacking import CXL_Unpacking
from m5.objects.CXL_FlexBus import CXL_FlexBus
from m5.objects.CXL_CRC_check import CXL_CRC_check

from m5.objects.System import System


# class Transaction_Queue(ClockedObject):
#     type = 'Transaction_Queue'
#     cxx_class = 'gem5::Transaction_Queue'
#     cxx_header = 'Transaction_Queue.hh'

#     PHY2TL_in_port= ResponsePort("PHY2TL_in_port")
#     PHY2TL_out_port = ResponsePort("PHY2TL_out_port")

#     TL2PHY_in_port= ResponsePort("TL2PHY_in_port")
#     TL2PHY_out_port = ResponsePort("TL2PHY_out_port")


class CXL_ctrl(ClockedObject):
    type = "CXL_ctrl"
    cxx_class = "gem5::CXL_ctrl"
    cxx_header = "dev/CXL/CXL_ctrl.hh"

    speed = Param.NetworkBandwidth(
        "16Gbps", "link speed"
    )  # Gen 3 link speed , Gen 1 2.5 Gbps , Gen 2 5 Gbps , Gen 3 8 Gbps. Gen 3 uses 128B/130B encoding , so effective speed = 985 MBPS
    delay = Param.Latency("0ns", "delay")
    delay_var = Param.Latency("0ns", "delay_var")
    lanes = Param.Int("lanes")
    max_queue_size = Param.Int(64, "max_queue_siz")
    validation_replay_policy_enable = Param.Bool(
        False,
        "Enable validation-only replay admission thresholds",
    )
    validation_replay_policy = Param.String(
        "hard_full",
        "Validation replay policy: hard_full, high_watermark, or hysteretic",
    )
    validation_rb_stall_threshold = Param.Int(
        -1,
        "Validation replay stall occupancy (-1 derives from capacity)",
    )
    validation_rb_resume_threshold = Param.Int(
        -1,
        "Validation replay resume occupancy (-1 derives from stall threshold)",
    )
    validation_strict_cft_deadline = Param.Bool(
        False,
        "Validation-only rescheduling when a newly armed CFT deadline is earlier",
    )
    validation_outstanding_window_capacity = Param.Int(
        -1,
        "Validation-only H2D outstanding-window capacity "
        "(-1 uses max_queue_size)",
    )
    mps = Param.Int("mps")

    upstreamResponse = ResponsePort("upstreamResponse")
    downstreamResponse = ResponsePort("downstreamResponse")
    upstreamRequest = RequestPort("upstreamRequest")
    downstreamRequest = RequestPort("downstreamRequest")

    # esj 2025-06-22
    downstreamResponse2 = ResponsePort("downstreamResponse2")
    upstreamRequest2 = RequestPort("upstreamRequest2")
    upstreamResponse2 = ResponsePort("upstreamResponse2")
    downstreamRequest2 = RequestPort("downstreamRequest2")
    ###

    internal_Resquest = RequestPort("internal_Resquest")
    internal_Response = ResponsePort("internal_Response")

    # esj 2025-01-03
    pcie_Resquest = RequestPort("pcie_Resquest")
    pcie_Response = ResponsePort("pcie_Response")

    # esj 2025-01-31
    # downstreamResponse2 = ResponsePort("downstreamResponse2")
    # downstreamRequest2 = RequestPort("downstreamRequest2")
    ###

    Host = Param.Bool(False, "is host")

    encoder = Param.CXL_Encoder(CXL_Encoder(), "CXL_Encoder")
    decoder = Param.CXL_Decoder(CXL_Decoder(), "CXL_Decoder")

    framer = Param.CXL_Framer(CXL_Framer(), "CXL_Framer")
    deframer = Param.CXL_Deframer(CXL_Deframer(), "CXL_Deframer")

    crc = Param.CXL_CRC(CXL_CRC(), "CXL_CRC")
    crc_checker = Param.CXL_CRC_check(CXL_CRC_check(), "CXL_CRC_check")

    packing = Param.CXL_Packing(CXL_Packing(), "CXL_Packing")
    unpacking = Param.CXL_Unpacking(CXL_Unpacking(), "CXL_Unpacking")

    flexbus = Param.CXL_FlexBus(CXL_FlexBus(), "FlexBus")

    # esj 2025-01-02
    control_flit_cycle = Param.Int(100, "control_flit-response cycle ")

    # esj 2025-01-17
    crc_error_control_flit_cycle = Param.Int(
        50, "crc_error_control_flit-response cycle "
    )

    # esj 2025-01-29
    read_queue_size = Param.Int(32, "read_queue_size")

    # esj 2025-02-18
    queuing_delay = Param.Latency("1ns", "queuing_delay")

    # tl_queue = Param.Transaction_Queue(Transaction_Queue(),"Transaction_Queue")

    def attachIO_device(self):

        self.packing.in_port = self.crc.out_port
        # self.crc.in_port = self.framer.out_port

        # esj 2025-01-03
        # add flex bus
        self.crc.in_port = self.flexbus.Device_resp_port
        self.flexbus.Host_resp_port = self.framer.out_port
        self.flexbus.CXL_in_port = self.pcie_Resquest
        #####

        self.framer.in_port = self.encoder.out_port
        self.encoder.in_port = self.internal_Resquest

        self.decoder.out_port = self.deframer.in_port

        # self.deframer.out_port = self.crc_checker.in_port

        # esj 2025-01-03
        # add flex bus
        self.deframer.out_port = self.flexbus.Host_req_port
        self.flexbus.Device_req_port = self.crc_checker.in_port
        self.flexbus.CXL_out_port = self.pcie_Response
        ##

        self.crc_checker.out_port = self.unpacking.in_port

        self.unpacking.out_port = self.internal_Response

    def attachIO_host(self):

        # add crc
        self.packing.out_port = self.crc.in_port
        # self.crc.out_port = self.framer.in_port

        # esj 2025-01-03
        # add flex bus
        self.crc.out_port = self.flexbus.Host_req_port
        self.flexbus.Device_req_port = self.framer.in_port
        self.flexbus.CXL_out_port = self.pcie_Response
        ###

        self.framer.out_port = self.encoder.in_port
        self.encoder.out_port = self.internal_Response

        self.decoder.in_port = self.deframer.out_port

        # add crc
        # self.deframer.in_port = self.crc_checker.out_port

        # esj 2025-01-03
        # add flex bus
        self.deframer.in_port = self.flexbus.Device_resp_port
        self.flexbus.Host_resp_port = self.crc_checker.out_port
        self.flexbus.CXL_in_port = self.pcie_Resquest
        ###

        self.crc_checker.in_port = self.unpacking.out_port

        self.unpacking.in_port = self.internal_Resquest
