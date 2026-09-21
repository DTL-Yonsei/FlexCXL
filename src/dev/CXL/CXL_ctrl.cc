#include "dev/CXL/CXL_ctrl.hh"

#include <algorithm>
#include <cmath>

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/random.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "debug/CXL_ctrl.hh"
#include "dev/CXL/CXL_CRC.hh"
#include "dev/CXL/CXL_Decoder.hh"
#include "dev/CXL/CXL_Deframer.hh"
#include "dev/CXL/CXL_Encoder.hh"
#include "dev/CXL/CXL_FlexBus.hh"
#include "dev/CXL/CXL_Framer.hh"
#include "dev/CXL/CXL_Packing.hh"
#include "dev/CXL/CXL_Unpacking.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{

    cxl_ReplayBuffer :: cxl_ReplayBuffer (int max_size)
    {
        // Initialize the buffer with null ptrs, indicating an empty buffer
        maximumSize = max_size ;

        //esj 2025-01-10
        // for (int i = 0 ; i < max_size ; i++)
        //     queue.push_back(NULL) ;
        // //esj 2025-01-10
        // for (int i = 0 ; i < max_size ; i++)
        //     seqnum.push_back(NULL) ;

        //esj 2025-01-02
        buffer_size = 0;
    }

    int
    cxl_ReplayBuffer::size()
    {
        // int count = 0;
        // for (std::deque<PacketPtr>::iterator it = queue.begin();
        //     it != queue.end(); it++) {
        //     if (*it == NULL) {
        //         continue;
        //     }
        //     else if (!((*it)->getflags() & 0x00000100)) {
        //         continue;
        //     }
        //     count++;
        // }
        // DPRINTF(CXL_ctrl, "[%s] Buffer status: current=%d, max=%d\n",
        //         __func__, count, maximumSize);
        // return count;

        // //esj 2025-01-02
        // DPRINTF(CXL_ctrl, "[%s] Buffer status: current=%d, max=%d\n", __func__, buffer_size, maximumSize);
        // return buffer_size;

        //esj 2025-01-11
        DPRINTF(CXL_ctrl, "[%s] Buffer status: current=%d, max=%d\n", __func__, queue.size(), maximumSize);
        return queue.size();
    }


    void
    cxl_ReplayBuffer::pushBack(PacketPtr ptr)
    {
        DPRINTF(CXL_ctrl, "[%s] START: pushing packet: addr=0x%x\n",
                __func__, ptr->getAddr());
        //esj 2024-12-07
        // if (size() >= maximumSize) {
        //     DPRINTF(CXL_ctrl, "[%s] ERROR: buffer full: size=%d, max=%d\n",
        //             __func__, size(), maximumSize);
        //     return;
        // }

        //esj 2025-01-10
        // if (queue.empty()) {
        //     queue.clear();  // clear the queue
        //     for (int i = 0; i < maximumSize; i++) {
        //         queue.push_back(NULL);
        //         //esj 2025-01-10
        //         seqnum.push_back(NULL);
        //     }
        // }

        // bool inserted = false;
        // for (std::deque<PacketPtr>::iterator it = queue.begin();
        //      it != queue.end(); it++) {
        //         DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, std::distance(queue.begin(), it), maximumSize);
        //     if (*it == NULL) {
        //         *it = ptr;
        //         inserted = true;
        //         DPRINTF(CXL_ctrl, "[%s] SUCCESS: packet inserted: position=%d\n",
        //                 __func__, std::distance(queue.begin(), it));
        //         break;
        //     }
        // }

        // if (!inserted) {
        //     DPRINTF(CXL_ctrl, "[%s] ERROR: no empty slots available\n", __func__);
        // }

        if (size()>= maximumSize) {
            DPRINTF(CXL_ctrl, "[%s] ERROR: buffer full: size=%d, max=%d\n",
                    __func__,size(), maximumSize);
            return;
        }
        //esj 2024-12-30
        // copyPacketInfo(ptr);

        queue.push_back(ptr);

        //esj 2025-01-10
        // seqnum.push_back(ptr->cxl_pkt.seqNum);

        //esj 2025-01-02
        ++buffer_size;

        DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, size(), maximumSize);
    }

    PacketPtr
    cxl_ReplayBuffer :: popFront()
    {
        // Remove and return the PacketPtr ptr stored at the front of the buff.
        if (queue.front() == NULL)
            return NULL ;

        PacketPtr temp = queue.front() ;
        queue.pop_front() ;

        //esj 2025-01-02
        --buffer_size;

        //esj 2025-01-01
        // queue.push_back(NULL) ; //esj 2024-12-07
        return temp ;
    }

    PacketPtr
    cxl_ReplayBuffer :: popByAddress(Addr address) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if ((*it)->getAddr() == address) {
                PacketPtr packet = *it;
                queue.erase(it);
                //esj 2025-01-02
                --buffer_size;
                return packet;
            }
        }
        return NULL;  // Address not found
    }

    bool
    cxl_ReplayBuffer :: popByseqnum(uint64_t seq_num) {
        // if(queue.size() == 0){
        //     return false;
        // }
        // for (auto it = queue.begin(); it != queue.end(); ++it) {
        //     if ((*it)->cxl_pkt.seqNum == seq_num) {
        //         PacketPtr packet = *it;
        //         queue.erase(it);
        //         return true;
        //     }
        // }
        // return false;  // Address not found

        if (queue.empty()) {
            return false;
        }

        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (*it == NULL) {
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                continue;
            }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    PacketPtr packet = *it;
                    queue.erase(it);

                    //esj 2025-01-02
                    --buffer_size;


                    // //esj 2025-02-17
                    // if(packet->cxl_pkt.is_controlflit){
                    //     delete packet;
                    // }
                    // //

                    //esj 2025-01-01
                    // queue.push_back(NULL) ;
                    return true;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
        }
        return false;
    }

    // esj 2025-05-16
    bool
    cxl_ReplayBuffer :: popByseqnum_double(uint64_t seq_num, uint64_t seq_num2) {

        int count = 0;
        if (queue.empty()) {
            return false;
        }

        DPRINTF(CXL_ctrl, "[%s] seq_num = %d, seq_num2 = %d\n", __func__, seq_num, seq_num2);
        for (auto it = queue.begin(); it != queue.end();) {
            if (*it == NULL) {
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                continue;
            }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    PacketPtr packet = *it;
                    it = queue.erase(it);
                    DPRINTF(CXL_ctrl, "[%s] erase seq_num = %d\n", __func__, seq_num);

                    --buffer_size;
                    count++;
                    continue;

                }
                else if ((*it)->cxl_pkt.seqNum == seq_num2) {
                    PacketPtr packet = *it;
                    it = queue.erase(it);
                    DPRINTF(CXL_ctrl, "[%s] erase seq_num2 = %d\n", __func__, seq_num2);
                    --buffer_size;
                    count++;
                    continue;
                }

                if(count == 2){
                    return true;
                }

            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
            ++it;
        }
        return false;
    }


    PacketPtr
    cxl_ReplayBuffer :: popByseqnum2(uint64_t seq_num) {
        if (queue.empty()) {
            return NULL;
        }

        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (*it == NULL) {
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                continue;
            }
            //esj 2024-12-19
            if((*it)->cxl_pkt.is_controlflit){
                // DPRINTF(CXL_ctrl, "[%s] Control flit, not returning packet\n",__func__);
                continue;
            }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    PacketPtr packet = *it;
                    return packet;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
        }
        return NULL;
    }

    bool
    cxl_ReplayBuffer :: findByseqnum(uint64_t seq_num) {
        //esj 2025-01-10
        if (queue.empty()) {
            return false;
        }

        int count = 0;
        for (auto it = queue.begin(); it != queue.end(); ++it) {


            if (*it == NULL) {
                //esj 2025-07-14
                DPRINTF(CXL_ctrl, "[%s] %dth packet is null\n", __func__, count++);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                //esj 2025-07-14
                DPRINTF(CXL_ctrl, "[%s] %dth packet has invalid addr flag, ptr=%p\n",
                        __func__, count++, static_cast<void*>(*it));
                continue;
            }

            //esj 2025-07-14
            DPRINTF(CXL_ctrl, "[%s] %dth packet is valid, is control flit = %s, seqnum = %d\n", __func__, count++, (*it)->cxl_pkt.is_controlflit? "true":"false", (*it)->cxl_pkt.seqNum);

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    return true;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
        }
        return false;
    }

    //esj 2025-07-14
    void
    cxl_ReplayBuffer :: findByseqnum2(uint64_t seq_num) {


        DPRINTF(CXL_ctrl, "[%s] queue sizw = %d, seq_num = %d\n", __func__, queue.size(), seq_num);
        if (queue.empty()) {
            return ;
        }

        for (auto it = queue.begin(); it != queue.end();) {
            if (queue.empty()) {
                return ;
            }
            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                it = queue.erase(it);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr flag, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                // Defensive cleanup: this may already be a stale pointer.
                it = queue.erase(it);
                if (buffer_size > 0)
                    --buffer_size;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size flag, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                // Defensive cleanup: avoid touching packet payload/fields here.
                it = queue.erase(it);
                if (buffer_size > 0)
                    --buffer_size;
                continue;
            }
            //esj 2025-01-18
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }

            try {
                //esj 2025-01-11
                 if ((*it)->cxl_pkt.seqNum == seq_num) {
                    DPRINTF(CXL_ctrl, "[%s], erase seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);

                    it = queue.erase(it);
                    if(buffer_size > 0)
                        --buffer_size;
                    continue;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }
            ++it;
        }
        DPRINTF(CXL_ctrl,"%s, packet is not yet in buffer, seqnum = %d\n",__func__,seq_num);
        return ;
    }

    //esj 2025-01-10
    bool
    cxl_ReplayBuffer :: popByseqnum3(uint64_t seq_num) {
        // if (seqnum.empty()) {
        //     return false;
        // }
        // DPRINTF(CXL_ctrl, "[%s] seq_num = %d\n", __func__, seq_num);
        // int count = 0;
        // for (auto it = seqnum.begin(); it != seqnum.end();) {
        //     // if (*it == seq_num) {
        //     if (*it <= seq_num) {
        //         DPRINTF(CXL_ctrl, "[%s] %dth seqnum packet: seq=%d\n", __func__, count, *it);
        //         if(*(queue.begin() + count) != NULL)
        //             DPRINTF(CXL_ctrl, "[%s] %dth queue packet: seq=%d\n", __func__, count, (*(queue.begin() + count))->cxl_pkt.seqNum);

        //         if (*it == seq_num)
        //         {
        //             queue.erase(queue.begin() + count);
        //             seqnum.erase(it);
        //             --buffer_size;
        //             return true;
        //         }
        //         queue.erase(queue.begin() + count);
        //         seqnum.erase(it);
        //         --buffer_size;
        //         // return true;

        //     }
        //     ++it;
        //     ++count;
        // }
        // return false;

        DPRINTF(CXL_ctrl, "[%s] queue sizw = %d, seq_num = %d\n", __func__, queue.size(), seq_num);
        if (queue.empty()) {
            return false;
        }

        for (auto it = queue.begin(); it != queue.end();) {
            if (queue.empty()) {
                return false;
            }
            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                it = queue.erase(it);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr flag, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                // Defensive cleanup: this may already be a stale pointer.
                it = queue.erase(it);
                if (buffer_size > 0)
                    --buffer_size;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size flag, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                // Defensive cleanup: avoid touching packet payload/fields here.
                it = queue.erase(it);
                if (buffer_size > 0)
                    --buffer_size;
                continue;
            }
            //esj 2025-01-18
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, drop ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }

            try {
                //esj 2025-01-11
                 if ((*it)->cxl_pkt.seqNum == seq_num) {
                // if ((*it)->cxl_pkt.seqNum <= seq_num) {
                    // if((*it)->cxl_pkt.seqNum == seq_num){
                    //     DPRINTF(CXL_ctrl, "[%s], erase seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                    //     it = queue.erase(it);
                    //     if(buffer_size > 0)
                    //         --buffer_size;
                    //     return true;
                    // }

                    // //esj 2025-01-11
                    // if(!(*it)->cxl_pkt.crc_check || (*it)->cxl_pkt.retry_req || (*it)->cxl_pkt.retry_resp){
                    //     ++it;
                    //     continue;
                    // }
                    // //

                    DPRINTF(CXL_ctrl, "[%s], erase seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                    // //esj 2025-02-17
                    // PacketPtr packet = *it;
                    // if(packet->cxl_pkt.is_controlflit){
                    //     delete packet;
                    // }
                    // //
                    it = queue.erase(it);
                    if(buffer_size > 0)
                        --buffer_size;
                    // continue;
                    return true;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }
            ++it;
        }
        return false;
    }

    PacketPtr
    cxl_ReplayBuffer :: popByseqnum6(uint64_t seq_num) {
        // if (seqnum.empty()) {
        //     return NULL;
        // }
        // int count = 0;
        // for (auto it = seqnum.begin(); it != seqnum.end();) {
        //     if (*it == seq_num) {
        //         PacketPtr packet = *(queue.begin() + count);
        //         return packet;
        //     }
        //     ++it;
        //     ++count;
        // }
        // return NULL;
        DPRINTF(CXL_ctrl, "[%s] queue size = %d, seq_num = %d\n", __func__, queue.size(), seq_num);

        if (queue.empty()) {
            DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
            return NULL;
        }
        //esj 2025-01-18
        DPRINTF(CXL_ctrl, "[%s] find start\n", __func__);
        int count = 0;


        for (auto it = queue.begin(); it != queue.end();) {

            //esj 2025-01-11
            if (queue.empty()) {
                DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
                return NULL;
            }
            //

            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr flag, ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size flag, ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                ++it;
                continue;
            }

            //esj 2025-01-18
            DPRINTF(CXL_ctrl, "[%s] %dth packet, seqnum = %d\n", __func__, count, (*it)->cxl_pkt.seqNum);
            if((*it)->cxl_pkt.is_controlflit){
                DPRINTF(CXL_ctrl, "[%s], control flit, eras\n",__func__);
                // //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // //
                it = queue.erase(it);
                continue;
            }
            //esj 2025-01-18
            if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                if(queue.empty()){
                    DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
                    break;
                }
                continue;
            }
            //esj 2025-01-24
            if((*it)->cxl_pkt.complete){
                DPRINTF(CXL_ctrl, "[%s], completed flit, eras\n",__func__);
                it = queue.erase(it);
                continue;
            }
            // //esj 2025-01-23
            // else if((*it)->cxl_pkt.retry_ack){
            //     it = queue.erase(it);
            //     continue;
            // }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    PacketPtr packet = *it;
                    //esj 2025-01-11
                    // it = queue.erase(it);
                    // if(buffer_size > 0)
                    //     --buffer_size;

                    DPRINTF(CXL_ctrl, "[%s] after pop: queue size = %d\n", __func__, queue.size());
                    //
                    return packet;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = queue.erase(it);
                continue;
            }
            ++it;
            ++count;
        }
        //esj 2025-07-14
        DPRINTF(CXL_ctrl, "[%s] return NULL\n", __func__);
        return NULL;
    }

    PacketPtr
    cxl_ReplayBuffer :: popByseqnum7(uint64_t seq_num) {

        DPRINTF(CXL_ctrl, "[%s] queue size = %d, seq_num = %d\n", __func__, queue.size(), seq_num);

        if (queue.empty()) {
            DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
            return NULL;
        }
        //esj 2025-01-18
        DPRINTF(CXL_ctrl, "[%s] find start\n", __func__);
        int count = 0;


        for (auto it = queue.begin(); it != queue.end();) {

            //esj 2025-01-11
            if (queue.empty()) {
                DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
                return NULL;
            }
            //

            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr flag, ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size flag, ptr=%p\n",
                        __func__, static_cast<void*>(*it));
                ++it;
                continue;
            }

            //esj 2025-01-18
            DPRINTF(CXL_ctrl, "[%s] %dth packet, seqnum = %d\n", __func__, count, (*it)->cxl_pkt.seqNum);
            if((*it)->cxl_pkt.is_controlflit){
                DPRINTF(CXL_ctrl, "[%s], control flit, eras\n",__func__);
                // //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // //
                it = queue.erase(it);
                continue;
            }
            //esj 2025-01-18
            if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                if(queue.empty()){
                    DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
                    break;
                }
                continue;
            }
            //esj 2025-01-24
            if((*it)->cxl_pkt.complete){
                DPRINTF(CXL_ctrl, "[%s], completed flit, eras\n",__func__);
                it = queue.erase(it);
                continue;
            }
            // //esj 2025-01-23
            // else if((*it)->cxl_pkt.retry_ack){
            //     it = queue.erase(it);
            //     continue;
            // }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {

                    if((*it)->isResponse()){
                        PacketPtr packet = *it;
                        DPRINTF(CXL_ctrl, "[%s] found packet, is response = %s, is read = %s\n", __func__, (*it)->isResponse()? "true":"false", (*it)->isRead()? "true":"false");

                        return packet;
                    }
                    DPRINTF(CXL_ctrl, "[%s] passing packet, is response = %s, is read = %s\n", __func__, (*it)->isResponse()? "true":"false", (*it)->isRead()? "true":"false");

                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = queue.erase(it);
                continue;
            }
            ++it;
            ++count;
        }
        //esj 2025-07-14
        DPRINTF(CXL_ctrl, "[%s] return NULL\n", __func__);
        return NULL;
    }

    void
    cxl_ReplayBuffer :: eraseinvalid() {
        DPRINTF(CXL_ctrl, "[%s] queue size = %d\n", __func__, queue.size());
        // Guard window for obviously corrupt/uninitialized seqNum values.
        const uint64_t seqGuardMax =
            std::max<uint64_t>(trace::send_packet_num, 1ULL) + (1ULL << 20);

        if (queue.empty()) {
            DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
            return ;
        }
        //esj 2025-01-18
        DPRINTF(CXL_ctrl, "[%s] find start\n", __func__);
        int count = 0;


        for (auto it = queue.begin(); it != queue.end();) {
            //esj 2025-01-18
            DPRINTF(CXL_ctrl, "[%s] %dth packet\n", __func__, count);

            //esj 2025-01-11
            if (queue.empty()) {
                DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
                return ;
            }
            //

            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                it = queue.erase(it);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                //
                it = queue.erase(it);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                //
                it = queue.erase(it);
                continue;
            }
            else if((*it)->cxl_pkt.is_controlflit){
                DPRINTF(CXL_ctrl, "[%s], control flit, eras\n",__func__);
                //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                //     packet = nullptr;
                // }
                //
                it = queue.erase(it);
                continue;
            }
            //esj 2025-01-18
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                //
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }
            else if((*it)->cxl_pkt.complete){
                DPRINTF(CXL_ctrl, "[%s], completed flit, eras\n",__func__);
                //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                //
                it = queue.erase(it);
                continue;
            }
            else if((*it)->cxl_pkt.seqNum > seqGuardMax &&
                    !(*it)->cxl_pkt.retry_req &&
                    !(*it)->cxl_pkt.retry_resp){
                DPRINTF(CXL_ctrl, "[%s], abnormal large seqnum, drop seqnum = %d (guard=%d)\n",
                        __func__, (*it)->cxl_pkt.seqNum, seqGuardMax);
                it = queue.erase(it);
                if (buffer_size > 0)
                    --buffer_size;
                continue;
            }
            DPRINTF(CXL_ctrl, "[%s] %dth packet is valid, is control flit = %s, seqnum = %d\n", __func__, count, (*it)->cxl_pkt.is_controlflit? "true":"false", (*it)->cxl_pkt.seqNum);
            ++it;
            ++count;
        }
    }

    // bool
    // cxl_ReplayBuffer :: popByseqnum3(uint64_t seq_num) {
    //     if (queue.empty()) {
    //         return false;
    //     }

    //     for (auto it = queue.begin(); it != queue.end(); ++it) {
    //         if (*it == nullptr) {
    //             continue;
    //         }
    //         else if (!((*it)->getflags() & 0x00000100) || !(*it)->isInvalidate()) {
    //             continue;
    //         }

    //         try {
    //             if ((*it)->cxl_pkt.seqNum == seq_num) {
    //                 PacketPtr packet = *it;
    //                 if(packet->cxl_pkt.retry_resp){
    //                     return true;
    //                 }
    //             }
    //         } catch (...) {
    //             DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
    //             continue;
    //         }
    //     }
    //     return false;
    // }

    // bool
    // cxl_ReplayBuffer :: popByseqnum4(uint64_t seq_num) {
    //     if (queue.empty()) {
    //         return false;
    //     }

    //     for (auto it = queue.begin(); it != queue.end(); ++it) {
    //         if (*it == nullptr) {
    //             continue;
    //         }
    //         else if (!((*it)->getflags() & 0x00000100) || !(*it)->isInvalidate()) {
    //             continue;
    //         }

    //         try {
    //             if ((*it)->cxl_pkt.seqNum == seq_num) {
    //                 PacketPtr packet = *it;
    //                 if(packet->cxl_pkt.retry_req){
    //                     return true;
    //                 }
    //             }
    //         } catch (...) {
    //             DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
    //             continue;
    //         }
    //     }
    //     return false;
    // }

    void
    cxl_ReplayBuffer :: print_buffer() {
        DPRINTF(CXL_ctrl, "[%s] size = %d\n", __func__, size());
        if (size() == 0) {
            return ;
        }
        DPRINTF(CXL_ctrl, "[%s] start print buffer \n",__func__);
        int count = 0;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            DPRINTF(CXL_ctrl, "[%s] %dth packet: seq=%d, is_controlflit=%d, retry_resp=%d, retry_req=%d\n"
            ,__func__, count, (*it)->cxl_pkt.seqNum, (*it)->cxl_pkt.is_controlflit, (*it)->cxl_pkt.retry_resp, (*it)->cxl_pkt.retry_req);
            count++;

        }
        DPRINTF(CXL_ctrl, "[%s] end print buffer \n",__func__);
    }

    void
    cxl_ReplayBuffer :: print_buffer2() {
        DPRINTF(CXL_ctrl, "[%s] size = %d\n", __func__, size());
        if (queue.empty()) {
            return ;
        }
        DPRINTF(CXL_ctrl, "[%s] start print buffer \n",__func__);
        int count = 0;
        // for (auto it = queue.begin(); it != queue.end(); ++it) {
        for (auto it = queue.begin(); it != queue.end();) {
            if (*it == NULL) {
                ++it; //esj
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                ++it; //esj
                continue;
            }

            //esj 2024-12-26
            if((*it)->cxl_pkt.is_controlflit){
                //esj 2025-01-11
                // queue.erase(it);
                ++it;
                //

                // queue.push_back(NULL) ;
                continue; //esj
            }

            DPRINTF(CXL_ctrl, "[%s] %dth packet: addr=0x%x, seq=%d, is_controlflit=%d, retry_resp=%d, retry_req=%d, crc_check = %s, validaddr  = %s, crc_ack = %s, complete = %s\n"
            ,__func__, count, (*it)->getAddr(), (*it)->cxl_pkt.seqNum, (*it)->cxl_pkt.is_controlflit, (*it)->cxl_pkt.retry_resp, (*it)->cxl_pkt.retry_req, (*it)->cxl_pkt.crc_check ? "true" : "false", (*it)->getflags() & 0x00000100 ? "true" : "false", (*it)->cxl_pkt.retry_ack ? "true" : "false", (*it)->cxl_pkt.complete ? "true" : "false");
            count++;
            ++it; //esj
        }
        DPRINTF(CXL_ctrl, "[%s] end print buffer \n",__func__);
    }

    //esj 2024-12-30
    PacketPtr
    cxl_ReplayBuffer :: popByseqnum5(uint64_t seq_num) {
        if (queue.empty()) {
            return NULL;
        }

        for (auto it = queue.begin(); it != queue.end();) {
            if (*it == NULL) {
                it = queue.erase(it);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                // //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // //
                it = queue.erase(it);
                continue;
            }
            //esj 2024-12-19
            if((*it)->cxl_pkt.is_controlflit){
                // //esj 2025-02-17
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // //
                it = queue.erase(it);
                continue; //esj
            }

            try {
                if ((*it)->cxl_pkt.seqNum == seq_num) {
                    PacketPtr packet = *it;
                    return packet;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = queue.erase(it);
                continue;
            }
            ++it; //esj
        }
        return NULL;
    }


    PacketPtr
    cxl_ReplayBuffer :: get (int idx)
    {
        // Get the PacketPtr ptr stored at the idx position in the buffer.
        if (idx > maximumSize)
            return NULL ;
        return queue[idx] ;
    }

    bool
    cxl_ReplayBuffer::empty()
    {
        // // Is the buffer empty ? Check for non-null values stored in it.
        // for (std::deque<PacketPtr>::iterator it = queue.begin() ;
        //     it != queue.end() ; it++) {

        //     if (*it == NULL) {
        //         continue;
        //     }
        //     else if (!((*it)->getflags() & 0x00000100)) {
        //         continue;
        //     }
        //     return false ;
        // }
        // return true ;

        //esj 2025-01-02
        if(buffer_size == 0){
            return true;
        }
        else{
            return false;
        }


    }

    //esj 2025-05-12
    // void copyPacketInfo(const PacketPtr src_pkt) {

    //     src_pkt->cxl_pkt.origin_packet_data.cmd = src_pkt->cmd;
    //     src_pkt->cxl_pkt.origin_packet_data.id = src_pkt->id;
    //     src_pkt->cxl_pkt.origin_packet_data.req = src_pkt->req;
    //     src_pkt->cxl_pkt.origin_packet_data.addr = src_pkt->getAddr();
    //     src_pkt->cxl_pkt.origin_packet_data.size = src_pkt->getSize();
    //     src_pkt->cxl_pkt.origin_packet_data._isSecure = src_pkt->isSecure();

    //     src_pkt->cxl_pkt.origin_packet_data.headerDelay = src_pkt->headerDelay;
    //     src_pkt->cxl_pkt.origin_packet_data.payloadDelay = src_pkt->payloadDelay;
    //     src_pkt->cxl_pkt.origin_packet_data.snoopDelay = 0;

    //     src_pkt->cxl_pkt.origin_packet_data.flags = src_pkt->getflags();
    //     src_pkt->cxl_pkt.origin_packet_data._qosValue = src_pkt->qosValue();

    //     src_pkt->cxl_pkt.origin_packet_data.bytesValid = src_pkt->get_bytes_valid();


    //     src_pkt->cxl_pkt.origin_packet_data.is_posted = false;
    //     src_pkt->cxl_pkt.origin_packet_data.req_bus[0] = src_pkt->req_bus[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_bus[1] = src_pkt->req_bus[1];
    //     src_pkt->cxl_pkt.origin_packet_data.req_dev[0] = src_pkt->req_dev[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_dev[1] = src_pkt->req_dev[1];
    //     src_pkt->cxl_pkt.origin_packet_data.req_func[0] = src_pkt->req_func[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_func[1] = src_pkt->req_func[1];

    //     src_pkt->cxl_pkt.origin_packet_data.isHtmTransactional = src_pkt->isHtmTransactional();
    //     if(src_pkt->isHtmTransactional()){
    //         src_pkt->cxl_pkt.origin_packet_data.htmTransactionUid = src_pkt->getHtmTransactionUid();
    //     }
    //     src_pkt->cxl_pkt.origin_packet_data.htmTransactionFailedInCache = src_pkt->htmTransactionFailedInCache();
    //     if (src_pkt->htmTransactionFailedInCache()){
    //         src_pkt->cxl_pkt.origin_packet_data.htmReturnReason = src_pkt->getHtmTransactionFailedInCacheRC();
    //     }

    // }

    //esj 2025-05-12
    // void returnPacketInfo(const PacketPtr src_pkt) {
    //     DPRINTF(CXL_ctrl, "[%s] START: returning packet: addr=0x%x\n",
    //             __func__, src_pkt->getAddr());

    //     src_pkt->cmd = src_pkt->cxl_pkt.origin_packet_data.cmd;
    //     src_pkt->req = src_pkt->cxl_pkt.origin_packet_data.req;
    //     src_pkt->setAddr(src_pkt->cxl_pkt.origin_packet_data.addr);
    //     src_pkt->setSize(src_pkt->cxl_pkt.origin_packet_data.size);
    //     src_pkt->set_is_secure(src_pkt->cxl_pkt.origin_packet_data._isSecure);

    //     src_pkt->headerDelay = src_pkt->cxl_pkt.origin_packet_data.headerDelay;
    //     src_pkt->payloadDelay = src_pkt->cxl_pkt.origin_packet_data.payloadDelay;
    //     src_pkt->snoopDelay = 0;

    //     src_pkt->setflags(src_pkt->cxl_pkt.origin_packet_data.flags);

    //     src_pkt->set_qos_value(src_pkt->cxl_pkt.origin_packet_data._qosValue);
    //     src_pkt->set_bytes_valid(src_pkt->cxl_pkt.origin_packet_data.bytesValid);


    //     src_pkt->cxl_pkt.origin_packet_data.is_posted = false;
    //     src_pkt->cxl_pkt.origin_packet_data.req_bus[0] = src_pkt->req_bus[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_bus[1] = src_pkt->req_bus[1];
    //     src_pkt->cxl_pkt.origin_packet_data.req_dev[0] = src_pkt->req_dev[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_dev[1] = src_pkt->req_dev[1];
    //     src_pkt->cxl_pkt.origin_packet_data.req_func[0] = src_pkt->req_func[0];
    //     src_pkt->cxl_pkt.origin_packet_data.req_func[1] = src_pkt->req_func[1];

    //     src_pkt->cxl_pkt.origin_packet_data.isHtmTransactional = src_pkt->isHtmTransactional();
    //     if(src_pkt->isHtmTransactional()){
    //         src_pkt->cxl_pkt.origin_packet_data.htmTransactionUid = src_pkt->getHtmTransactionUid();
    //     }
    //     src_pkt->cxl_pkt.origin_packet_data.htmTransactionFailedInCache = src_pkt->htmTransactionFailedInCache();
    //     if (src_pkt->htmTransactionFailedInCache()){
    //         src_pkt->cxl_pkt.origin_packet_data.htmReturnReason = src_pkt->getHtmTransactionFailedInCacheRC();
    //     }

    //     DPRINTF(CXL_ctrl, "[%s], cmd = %s, addr = 0x%x, size = %d, isSecure = %d, qosValue = %d, flags = %d\n",
    //             __func__, src_pkt->cmdString(), src_pkt->getAddr(), src_pkt->getSize(), src_pkt->isSecure(), src_pkt->qosValue(), src_pkt->getflags());
    //     DPRINTF(CXL_ctrl, "[%s] END: returning packet: addr=0x%x\n",
    //             __func__, src_pkt->getAddr());

    // }

    // //esj 2024-12-23
    // bool
    // CXL_ctrl :: check_seqnum_req(uint64_t seq_num) {
    //     if (retry_transmitList_req.empty()) {
    //         return false;
    //     }

    //     for (auto it = retry_transmitList_req.begin(); it != retry_transmitList_req.end();++it) {
    //         if ((*it).pkt == NULL) {
    //             // //esj 2025-01-03
    //             // it = retry_transmitList_req.erase(it);
    //             continue;
    //         }
    //         else if (!((*it).pkt->getflags() & 0x00000100)) {
    //             // //esj 2025-01-03
    //             // it = retry_transmitList_req.erase(it);
    //             continue;
    //         }

    //         try {
    //             if ((*it).pkt->cxl_pkt.seqNum == seq_num) {
    //                 DPRINTF(CXL_ctrl, "erase seqnum = %d from retry_transmitList_req\n",seq_num);
    //                 retry_transmitList_req.erase(it);
    //                 return true;
    //             }
    //         } catch (...) {
    //             DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
    //             continue;
    //         }
    //         // ++it; //esj 2024-01-03
    //     }
    //     return false;
    // }

    //esj 2025-01-17
    int
    CXL_ctrl :: check_seqnum_req(Tick when) {
        if (retry_transmitList_req.empty()) {
            return 0;
        }
        int position = 0;
        for (auto it = retry_transmitList_req.begin(); it != retry_transmitList_req.end();) {
            if ((*it).pkt == NULL) {
                // //esj 2025-01-03
                // it = retry_transmitList_req.erase(it);
                continue;
            }
            else if (!((*it).pkt->getflags() & 0x00000100)) {
                // //esj 2025-01-03
                // it = retry_transmitList_req.erase(it);
                continue;
            }

            try {
                if ((*it).tick > when) {
                    DPRINTF(CXL_ctrl, "when = %d\n",(*it).tick);

                    return position;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
            ++position;
            ++it;
        }
        return 0;
    }

    int
    CXL_ctrl :: check_seqnum_resp(Tick when) {
        if (retry_transmitList_resp.empty()) {
            return 0;
        }
        int position = 0;
        for (auto it = retry_transmitList_resp.begin(); it != retry_transmitList_resp.end();) {
            if ((*it).pkt == NULL) {
                //esj 2025-01-01
                it = retry_transmitList_resp.erase(it);
                continue;
            }
            else if (!((*it).pkt->getflags() & 0x00000100)) {
                //esj 2025-01-01
                it = retry_transmitList_resp.erase(it);
                continue;
            }

            try {
                if ((*it).tick > when) {
                    DPRINTF(CXL_ctrl, "when = %d\n",(*it).tick);

                    return position;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                continue;
            }
            ++it; //esj 2024-01-01
            ++position;
        }
        return 0;
    }

    // bool
    // CXL_ctrl :: check_seqnum_resp(uint64_t seq_num) {
    //     if (retry_transmitList_resp.empty()) {
    //         return false;
    //     }
    //     for (auto it = retry_transmitList_resp.begin(); it != retry_transmitList_resp.end();) {
    //         if ((*it).pkt == NULL) {
    //             //esj 2025-01-01
    //             it = retry_transmitList_resp.erase(it);
    //             continue;
    //         }
    //         else if (!((*it).pkt->getflags() & 0x00000100)) {
    //             //esj 2025-01-01
    //             it = retry_transmitList_resp.erase(it);
    //             continue;
    //         }

    //         try {
    //             if ((*it).pkt->cxl_pkt.seqNum == seq_num) {
    //                 DPRINTF(CXL_ctrl, "erase seqnum = %d from retry_transmitList_resp\n",seq_num);
    //                 retry_transmitList_resp.erase(it);
    //                 return true;
    //             }
    //         } catch (...) {
    //             DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
    //             continue;
    //         }
    //         ++it; //esj 2024-01-01
    //     }
    //     return false;
    // }

    // bool
    // CXL_ctrl::popfromtransmitlist_resp(uint64_t seq_num){
    //     auto it = retry_transmitList_resp.begin();
    //     while (it != retry_transmitList_resp.end()) {
    //         if (it->pkt->cxl_pkt.seqNum == seq_num) {
    //             retry_transmitList_resp.erase(it);
    //             return true;
    //         }
    //         ++it;
    //     }
    //     return false;
    // }

    // bool
    // CXL_ctrl::popfromtransmitlist_req(uint64_t seq_num){
    //     auto it = retry_transmitList_req.begin();
    //     while (it != retry_transmitList_req.end()) {
    //         if (it->pkt->cxl_pkt.seqNum == seq_num) {
    //             retry_transmitList_req.erase(it);
    //             return true;
    //         }
    //         ++it;
    //     }
    //     return false;
    // }
    bool
    CXL_ctrl::popfromtransmitlist_resp(uint64_t seq_num) {
        try {
            // Check if list is empty
            if (retry_transmitList_resp.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return false;
            }

            auto it = retry_transmitList_resp.begin();
            while (it != retry_transmitList_resp.end()) {
                // Check for null pointer
                if (!it->pkt) {
                    DPRINTF(CXL_ctrl, "[%s] Null packet found\n", __func__);
                    //esj 2025-01-01
                    ++it;
                    // it = retry_transmitList_resp.erase(it);
                    continue;
                }

                try {
                    if (it->pkt->cxl_pkt.seqNum == seq_num) {
                        //esj 2025-01-05
                        it->valid =false;

                        //esj 2025-02-17
                        // if(it->pkt->cxl_pkt.is_controlflit){
                        //     delete it->pkt;
                        // }
                        //

                        retry_transmitList_resp.erase(it);
                        // DeferredPacket temp = DeferredPacket(NULL,0);
                        // retry_transmitList_resp.push_back(temp);
                        DPRINTF(CXL_ctrl, "[%s] Successfully removed packet with seqNum=%d\n",
                                __func__, seq_num);
                        return true;
                    }
                } catch (const std::exception& e) {
                    DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet: %s\n",
                            __func__, e.what());
                    //esj 2025-01-01
                    ++it;
                    // it = retry_transmitList_resp.erase(it);
                    continue;
                }
                ++it;
            }
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception in popfromtransmitlist_resp: %s\n",
                    __func__, e.what());
            return false;
        }
        return false;
    }

    bool
    CXL_ctrl::popfromtransmitlist_req(uint64_t seq_num) {
        try {
            // Check if list is empty
            if (retry_transmitList_req.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return false;
            }

            auto it = retry_transmitList_req.begin();
            while (it != retry_transmitList_req.end()) {
                // Check for null pointer
                if (!it->pkt) {
                    DPRINTF(CXL_ctrl, "[%s] Null packet found\n", __func__);
                    //esj 2025-01-01
                    ++it;
                    // it = retry_transmitList_req.erase(it);
                    continue;
                }

                try {
                    if (it->pkt->cxl_pkt.seqNum == seq_num) {
                        //esj 2025-01-05
                        it->valid =false;

                        //esj 2025-01-10
                        delete it->pkt;
                        //

                        retry_transmitList_req.erase(it);
                        // DeferredPacket temp = DeferredPacket(NULL,0);
                        // retry_transmitList_req.push_back(temp);
                        DPRINTF(CXL_ctrl, "[%s] Successfully removed packet with seqNum=%d\n",
                                __func__, seq_num);
                        return true;
                    }
                } catch (const std::exception& e) {
                    DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet: %s\n",
                            __func__, e.what());
                    //esj 2025-01-01
                    ++it;
                    // it = retry_transmitList_req.erase(it);
                    continue;
                }
                ++it;
            }
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception in popfromtransmitlist_req: %s\n",
                    __func__, e.what());
            return false;
        }
        return false;
    }

    void
    CXL_ctrl::print_retry_transmitList_resp() {
        DPRINTF(CXL_ctrl, "[%s] ===== Start printing retry_transmitList_resp status =====\n", __func__);

        try {
            if (retry_transmitList_resp.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return;
            }

            int count = 0;
            for (const auto& item : retry_transmitList_resp) {
                if (item.pkt) {
                    try {
                        DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
                                __func__,
                                count,
                                item.pkt->getAddr(),
                                item.pkt->cxl_pkt.seqNum,
                                item.tick);
                    } catch (const std::exception& e) {
                        DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
                                __func__, count, e.what());
                    }
                    count++;
                } else {
                    DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
                }

            }

            DPRINTF(CXL_ctrl, "[%s] Total number of packets: %d\n", __func__, count);
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        }

        DPRINTF(CXL_ctrl, "[%s] ===== End printing retry_transmitList_resp status =====\n", __func__);
    }

    void
    CXL_ctrl::print_retry_transmitList_req() {
        DPRINTF(CXL_ctrl, "[%s] ===== Start printing retry_transmitList_req status =====\n", __func__);

        try {
            if (retry_transmitList_req.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return;
            }

            int count = 0;
            for (const auto& item : retry_transmitList_req) {
                if (item.pkt) {
                    try {
                        DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
                                __func__,
                                count,
                                item.pkt->getAddr(),
                                item.pkt->cxl_pkt.seqNum,
                                item.tick);
                    } catch (const std::exception& e) {
                        DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
                                __func__, count, e.what());
                    }
                    count++;
                } else {
                    DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
                }

            }

            DPRINTF(CXL_ctrl, "[%s] Total number of packets: %d\n", __func__, count);
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        }

        DPRINTF(CXL_ctrl, "[%s] ===== End printing retry_transmitList_req status =====\n", __func__);
    }


    int
    CXL_ctrl::getsize_retry_transmitList_resp() {
        // try {
        //     if (retry_transmitList_resp.empty()) {
        //         DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
        //         return 0;
        //     }

        //     int count = 0;
        //     for (const auto& item : retry_transmitList_resp) {
        //         if (item.pkt) {
        //             try {
        //                 DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
        //                         __func__,
        //                         count,
        //                         item.pkt->getAddr(),
        //                         item.pkt->cxl_pkt.seqNum,
        //                         item.tick);
        //             } catch (const std::exception& e) {
        //                 DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
        //                         __func__, count, e.what());
        //             }
        //             count++;
        //         } else {
        //             DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
        //         }

        //     }
        //     return count;

        //     DPRINTF(CXL_ctrl, "[%s] Total number of packets: %d\n", __func__, count);
        // } catch (const std::exception& e) {
        //     DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        // }


        //esj 2025-01-01
        if (retry_transmitList_resp.empty()) {
            return 0;
        }

        int count = 0;

        for (auto it = retry_transmitList_resp.begin(); it != retry_transmitList_resp.end();) {
            if ((*it).pkt == NULL) {
                it = retry_transmitList_resp.erase(it);
                continue;
            }
            else if (!((*it).pkt->getflags() & 0x00000100)) {

                it = retry_transmitList_resp.erase(it);
                continue;
            }
            //esj 2025-01-03
            // else if((*it).pkt->isResponse()){
            //     it = retry_transmitList_resp.erase(it);
            //     continue;
            // }

            try {
                if ((*it).pkt != NULL) {
                    count++;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = retry_transmitList_resp.erase(it);
                continue;
            }
            ++it; //esj 2024-01-01
        }
        return count;
    }

    int
    CXL_ctrl::getsize_retry_transmitList_req() {
        // try {
        //     if (retry_transmitList_req.empty()) {
        //         DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
        //         return 0;
        //     }

        //     int count = 0;
        //     for (const auto& item : retry_transmitList_req) {
        //         if (item.pkt) {
        //             try {
        //                 DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
        //                         __func__,
        //                         count,
        //                         item.pkt->getAddr(),
        //                         item.pkt->cxl_pkt.seqNum,
        //                         item.tick);
        //             } catch (const std::exception& e) {
        //                 DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
        //                         __func__, count, e.what());
        //             }
        //             count++;
        //         } else {
        //             DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
        //         }
        //     }
        //     return count;

        //     DPRINTF(CXL_ctrl, "[%s] Total number of packets: %d\n", __func__, count);
        // } catch (const std::exception& e) {
        //     DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        // }

        //esj 2024-01-01
        if (retry_transmitList_req.empty()) {
            return 0;
        }

        int count = 0;

        for (auto it = retry_transmitList_req.begin(); it != retry_transmitList_req.end();) {
            if ((*it).pkt == NULL) {
                it = retry_transmitList_req.erase(it);
                continue;
            }
            else if (!((*it).pkt->getflags() & 0x00000100)) {

                it = retry_transmitList_req.erase(it);
                continue;
            }

            try {
                if ((*it).pkt != NULL) {
                    count++;
                }
            } catch (...) {
                DPRINTF(CXL_ctrl, "Warning: Invalid packet pointer encountered in popByseqnum\n");
                it = retry_transmitList_req.erase(it);
                continue;
            }
            ++it; //esj 2024-01-01
        }
        return count;

    }

    int
    CXL_ctrl::get_retry_num_resp() {
        try {
            if (retry_transmitList_resp.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return 0;
            }

            int count = 0;
            for (const auto& item : retry_transmitList_resp) {
                if (item.pkt) {
                    try {
                        DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
                                __func__,
                                count,
                                item.pkt->getAddr(),
                                item.pkt->cxl_pkt.seqNum,
                                item.tick);
                    } catch (const std::exception& e) {
                        DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
                                __func__, count, e.what());
                    }
                } else {
                    DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
                }
                if(item.pkt->cxl_pkt.retry_resp)
                count++;
            }
            return count;

            DPRINTF(CXL_ctrl, "[%s] Total number of retry_resp: %d\n", __func__, count);
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        }
    }

    int
    CXL_ctrl::get_retry_num_req() {
        try {
            if (retry_transmitList_req.empty()) {
                DPRINTF(CXL_ctrl, "[%s] List is empty\n", __func__);
                return 0;
            }

            int count = 0;
            for (const auto& item : retry_transmitList_req) {
                if (item.pkt) {
                    try {
                        DPRINTF(CXL_ctrl, "[%s] Packet %d: addr=0x%x, seq=%d, tick=%d\n",
                                __func__,
                                count,
                                item.pkt->getAddr(),
                                item.pkt->cxl_pkt.seqNum,
                                item.tick);
                    } catch (const std::exception& e) {
                        DPRINTF(CXL_ctrl, "[%s] Exception while accessing packet %d info: %s\n",
                                __func__, count, e.what());
                    }
                } else {
                    DPRINTF(CXL_ctrl, "[%s] Packet %d: NULL\n", __func__, count);
                }
                if(item.pkt->cxl_pkt.retry_req)
                count++;
            }
            return count;

            DPRINTF(CXL_ctrl, "[%s] Total number of retry_req: %d\n", __func__, count);
        } catch (const std::exception& e) {
            DPRINTF(CXL_ctrl, "[%s] Exception while printing list: %s\n", __func__, e.what());
        }
    }


   //esj 2025-02-24
   CXL_ctrl::CXLStats::CXLStats(CXL_ctrl &ctrl)
   : statistics::Group(&ctrl), ctrl(ctrl),
   ADD_STAT(control_flit_req_read, "Number of control flit of req_read from host"),
   ADD_STAT(control_flit_req_write, "Number of control flit of req_write from host"),
   ADD_STAT(control_flit_resp_read, "Number of control flit of resp_read from device"),
   ADD_STAT(control_flit_resp_write, "Number of control flit of resp_write from device")
   ,ADD_STAT(data_flit_req_read,
             "Data flits for read requests")
   ,ADD_STAT(data_flit_req_write,
             "Data flits for write requests")
   ,ADD_STAT(data_flit_resp_read,
             "Data flits for read responses")
   ,ADD_STAT(data_flit_resp_write,
             "Data flits for write responses")
   ,ADD_STAT(controller_req_queue_occupancy_total,
             "Sum of controller request queue occupancy samples")
   ,ADD_STAT(controller_req_queue_occupancy_samples,
             "Controller request queue occupancy samples")
   ,ADD_STAT(controller_req_queue_occupancy_max,
             "Maximum controller request queue occupancy")
   ,ADD_STAT(controller_req_queue_occupancy_avg,
             "Average controller request queue occupancy")
   ,ADD_STAT(controller_resp_queue_occupancy_total,
             "Sum of controller response queue occupancy samples")
   ,ADD_STAT(controller_resp_queue_occupancy_samples,
             "Controller response queue occupancy samples")
   ,ADD_STAT(controller_resp_queue_occupancy_max,
             "Maximum controller response queue occupancy")
   ,ADD_STAT(controller_resp_queue_occupancy_avg,
             "Average controller response queue occupancy")
   ,ADD_STAT(replay_buffer_host_occupancy_total,
             "Sum of host-side replay buffer occupancy samples")
   ,ADD_STAT(replay_buffer_host_occupancy_samples,
             "Host-side replay buffer occupancy samples")
   ,ADD_STAT(replay_buffer_host_occupancy_max,
             "Maximum host-side replay buffer occupancy")
   ,ADD_STAT(replay_buffer_host_occupancy_avg,
             "Average host-side replay buffer occupancy")
   ,ADD_STAT(replay_buffer_device_occupancy_total,
             "Sum of device-side replay buffer occupancy samples")
   ,ADD_STAT(replay_buffer_device_occupancy_samples,
             "Device-side replay buffer occupancy samples")
   ,ADD_STAT(replay_buffer_device_occupancy_max,
             "Maximum device-side replay buffer occupancy")
   ,ADD_STAT(replay_buffer_device_occupancy_avg,
             "Average device-side replay buffer occupancy")
   ,ADD_STAT(retry_buffer_req_occupancy_total,
             "Sum of retry request buffer occupancy samples")
   ,ADD_STAT(retry_buffer_req_occupancy_samples,
             "Retry request buffer occupancy samples")
   ,ADD_STAT(retry_buffer_req_occupancy_max,
             "Maximum retry request buffer occupancy")
   ,ADD_STAT(retry_buffer_req_occupancy_avg,
             "Average retry request buffer occupancy")
   ,ADD_STAT(retry_buffer_resp_occupancy_total,
             "Sum of retry response buffer occupancy samples")
   ,ADD_STAT(retry_buffer_resp_occupancy_samples,
             "Retry response buffer occupancy samples")
   ,ADD_STAT(retry_buffer_resp_occupancy_max,
             "Maximum retry response buffer occupancy")
   ,ADD_STAT(retry_buffer_resp_occupancy_avg,
             "Average retry response buffer occupancy")
   // replay buffer histograms were replaced by explicit occupancy stats.
   ,ADD_STAT(stall_req_count, "Number of stall request") //esj 2025-11-21
   ,ADD_STAT(stall_resp_count, "Number of stall response")
   ,ADD_STAT(stall_req_retry_count, "Number of stall request retry")
   ,ADD_STAT(stall_resp_retry_count, "Number of stall response retry")
   ,ADD_STAT(stall_req_cycles,
             "Controller cycles spent on request stall events")
   ,ADD_STAT(stall_resp_cycles,
             "Controller cycles spent on response stall events")
   ,ADD_STAT(stall_req_retry_cycles,
             "Controller cycles spent on request retry stall events")
   ,ADD_STAT(stall_resp_retry_cycles,
             "Controller cycles spent on response retry stall events")
   ,ADD_STAT(crc_error_count, "Number of crc error") //esj 2025-11-29
{
   control_flit_req_read.reset();
   control_flit_req_write.reset();
   control_flit_resp_read.reset();
   control_flit_resp_write.reset();
   data_flit_req_read.reset();
   data_flit_req_write.reset();
   data_flit_resp_read.reset();
   data_flit_resp_write.reset();
   controller_req_queue_occupancy_total.reset();
   controller_req_queue_occupancy_samples.reset();
   controller_req_queue_occupancy_max.reset();
   controller_resp_queue_occupancy_total.reset();
   controller_resp_queue_occupancy_samples.reset();
   controller_resp_queue_occupancy_max.reset();
   replay_buffer_host_occupancy_total.reset();
   replay_buffer_host_occupancy_samples.reset();
   replay_buffer_host_occupancy_max.reset();
   replay_buffer_device_occupancy_total.reset();
   replay_buffer_device_occupancy_samples.reset();
   replay_buffer_device_occupancy_max.reset();
   retry_buffer_req_occupancy_total.reset();
   retry_buffer_req_occupancy_samples.reset();
   retry_buffer_req_occupancy_max.reset();
   retry_buffer_resp_occupancy_total.reset();
   retry_buffer_resp_occupancy_samples.reset();
   retry_buffer_resp_occupancy_max.reset();
   // replay_buffer.reset(); //esj 2025-09-09

   //esj 2025-11-21
   stall_req_count.reset();
   stall_resp_count.reset();
   stall_req_retry_count.reset();
   stall_resp_retry_count.reset();
   stall_req_cycles.reset();
   stall_resp_cycles.reset();
   stall_req_retry_cycles.reset();
   stall_resp_retry_cycles.reset();
   //esj 2025-11-29
   crc_error_count.reset();
}
    // void
    // CXL_ctrl::CXLStats::regStats()
    // {
    //     using namespace statistics;

    //     statistics::Group::regStats();

    //     control_flit_req_read.reset();
    //     control_flit_req_write.reset();
    //     control_flit_resp_read.reset();
    //     control_flit_resp_write.reset();



    //esj 2025-07-29
    void CXL_ctrl::CXLStats::regStats() {
        statistics::Group::regStats();

        control_flit_req_read
        .name(ctrl.name() + ".control_flit_req_read")
        .flags(statistics::nozero)
        ;

        control_flit_req_write
        .name(ctrl.name() + ".control_flit_req_write")
        .flags(statistics::nozero)
        ;

        control_flit_resp_read
        .name(ctrl.name() + ".control_flit_resp_read")
        .flags(statistics::nozero)
        ;

        control_flit_resp_write
        .name(ctrl.name() + ".control_flit_resp_write")
        .flags(statistics::nozero)
        ;

        data_flit_req_read
        .name(ctrl.name() + ".data_flit_req_read")
        .flags(statistics::nozero)
        ;

        data_flit_req_write
        .name(ctrl.name() + ".data_flit_req_write")
        .flags(statistics::nozero)
        ;

        data_flit_resp_read
        .name(ctrl.name() + ".data_flit_resp_read")
        .flags(statistics::nozero)
        ;

        data_flit_resp_write
        .name(ctrl.name() + ".data_flit_resp_write")
        .flags(statistics::nozero)
        ;

        controller_req_queue_occupancy_total
        .name(ctrl.name() + ".controller_req_queue_occupancy_total")
        .flags(statistics::nozero)
        ;
        controller_req_queue_occupancy_samples
        .name(ctrl.name() + ".controller_req_queue_occupancy_samples")
        .flags(statistics::nozero)
        ;
        controller_req_queue_occupancy_max
        .name(ctrl.name() + ".controller_req_queue_occupancy_max")
        .flags(statistics::nozero)
        ;
        controller_req_queue_occupancy_avg
        .name(ctrl.name() + ".controller_req_queue_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        controller_resp_queue_occupancy_total
        .name(ctrl.name() + ".controller_resp_queue_occupancy_total")
        .flags(statistics::nozero)
        ;
        controller_resp_queue_occupancy_samples
        .name(ctrl.name() + ".controller_resp_queue_occupancy_samples")
        .flags(statistics::nozero)
        ;
        controller_resp_queue_occupancy_max
        .name(ctrl.name() + ".controller_resp_queue_occupancy_max")
        .flags(statistics::nozero)
        ;
        controller_resp_queue_occupancy_avg
        .name(ctrl.name() + ".controller_resp_queue_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        replay_buffer_host_occupancy_total
        .name(ctrl.name() + ".replay_buffer_host_occupancy_total")
        .flags(statistics::nozero)
        ;
        replay_buffer_host_occupancy_samples
        .name(ctrl.name() + ".replay_buffer_host_occupancy_samples")
        .flags(statistics::nozero)
        ;
        replay_buffer_host_occupancy_max
        .name(ctrl.name() + ".replay_buffer_host_occupancy_max")
        .flags(statistics::nozero)
        ;
        replay_buffer_host_occupancy_avg
        .name(ctrl.name() + ".replay_buffer_host_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        replay_buffer_device_occupancy_total
        .name(ctrl.name() + ".replay_buffer_device_occupancy_total")
        .flags(statistics::nozero)
        ;
        replay_buffer_device_occupancy_samples
        .name(ctrl.name() + ".replay_buffer_device_occupancy_samples")
        .flags(statistics::nozero)
        ;
        replay_buffer_device_occupancy_max
        .name(ctrl.name() + ".replay_buffer_device_occupancy_max")
        .flags(statistics::nozero)
        ;
        replay_buffer_device_occupancy_avg
        .name(ctrl.name() + ".replay_buffer_device_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        retry_buffer_req_occupancy_total
        .name(ctrl.name() + ".retry_buffer_req_occupancy_total")
        .flags(statistics::nozero)
        ;
        retry_buffer_req_occupancy_samples
        .name(ctrl.name() + ".retry_buffer_req_occupancy_samples")
        .flags(statistics::nozero)
        ;
        retry_buffer_req_occupancy_max
        .name(ctrl.name() + ".retry_buffer_req_occupancy_max")
        .flags(statistics::nozero)
        ;
        retry_buffer_req_occupancy_avg
        .name(ctrl.name() + ".retry_buffer_req_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        retry_buffer_resp_occupancy_total
        .name(ctrl.name() + ".retry_buffer_resp_occupancy_total")
        .flags(statistics::nozero)
        ;
        retry_buffer_resp_occupancy_samples
        .name(ctrl.name() + ".retry_buffer_resp_occupancy_samples")
        .flags(statistics::nozero)
        ;
        retry_buffer_resp_occupancy_max
        .name(ctrl.name() + ".retry_buffer_resp_occupancy_max")
        .flags(statistics::nozero)
        ;
        retry_buffer_resp_occupancy_avg
        .name(ctrl.name() + ".retry_buffer_resp_occupancy_avg")
        .flags(statistics::nozero | statistics::nonan)
        ;

        //esj 2025-09-09
        // 히스토그램은 모든 인스턴스에서 동일한 버킷 수를 사용해야 함
        // (다른 버킷 수를 가진 히스토그램을 합칠 때 assertion 발생)
        // const int histogram_buckets = 128;  // 충분히 큰 고정값 사용
        // replay_buffer
        // .init(histogram_buckets)
        // .flags(statistics::nozero | statistics::pdf | statistics::oneline);

        // replay_buffer_size
        // .init(ctrl.maxQueueSize);

        // replay_buffer_without_zero
        // .init(histogram_buckets)
        // .flags(statistics::nozero | statistics::pdf | statistics::oneline);

        //esj 2025-11-21
        stall_req_count
        .name(ctrl.name() + ".stall_req_count")
        .flags(statistics::nozero)
        ;

        stall_resp_count
        .name(ctrl.name() + ".stall_resp_count")
        .flags(statistics::nozero)
        ;

        stall_req_retry_count
        .name(ctrl.name() + ".stall_req_retry_count")
        .flags(statistics::nozero)
        ;

        stall_resp_retry_count
        .name(ctrl.name() + ".stall_resp_retry_count")
        .flags(statistics::nozero)
        ;
        stall_req_cycles
        .name(ctrl.name() + ".stall_req_cycles")
        .flags(statistics::nozero)
        ;

        stall_resp_cycles
        .name(ctrl.name() + ".stall_resp_cycles")
        .flags(statistics::nozero)
        ;

        stall_req_retry_cycles
        .name(ctrl.name() + ".stall_req_retry_cycles")
        .flags(statistics::nozero)
        ;

        stall_resp_retry_cycles
        .name(ctrl.name() + ".stall_resp_retry_cycles")
        .flags(statistics::nozero)
        ;
        crc_error_count
        .name(ctrl.name() + ".crc_error_count")
        .flags(statistics::nozero)
        ;

        controller_req_queue_occupancy_avg =
            controller_req_queue_occupancy_total /
            controller_req_queue_occupancy_samples;
        controller_resp_queue_occupancy_avg =
            controller_resp_queue_occupancy_total /
            controller_resp_queue_occupancy_samples;
        replay_buffer_host_occupancy_avg =
            replay_buffer_host_occupancy_total /
            replay_buffer_host_occupancy_samples;
        replay_buffer_device_occupancy_avg =
            replay_buffer_device_occupancy_total /
            replay_buffer_device_occupancy_samples;
        retry_buffer_req_occupancy_avg =
            retry_buffer_req_occupancy_total /
            retry_buffer_req_occupancy_samples;
        retry_buffer_resp_occupancy_avg =
            retry_buffer_resp_occupancy_total /
            retry_buffer_resp_occupancy_samples;
    }

    void CXL_ctrl::CXLStats::resetStats() {
        statistics::Group::resetStats();

        control_flit_req_read.reset();
        control_flit_req_write.reset();
        control_flit_resp_read.reset();
        control_flit_resp_write.reset();
        data_flit_req_read.reset();
        data_flit_req_write.reset();
        data_flit_resp_read.reset();
        data_flit_resp_write.reset();
        controller_req_queue_occupancy_total.reset();
        controller_req_queue_occupancy_samples.reset();
        controller_req_queue_occupancy_max.reset();
        controller_resp_queue_occupancy_total.reset();
        controller_resp_queue_occupancy_samples.reset();
        controller_resp_queue_occupancy_max.reset();
        replay_buffer_host_occupancy_total.reset();
        replay_buffer_host_occupancy_samples.reset();
        replay_buffer_host_occupancy_max.reset();
        replay_buffer_device_occupancy_total.reset();
        replay_buffer_device_occupancy_samples.reset();
        replay_buffer_device_occupancy_max.reset();
        retry_buffer_req_occupancy_total.reset();
        retry_buffer_req_occupancy_samples.reset();
        retry_buffer_req_occupancy_max.reset();
        retry_buffer_resp_occupancy_total.reset();
        retry_buffer_resp_occupancy_samples.reset();
        retry_buffer_resp_occupancy_max.reset();

        //esj 2025-11-21
        stall_req_count.reset();
        stall_resp_count.reset();
        stall_req_retry_count.reset();
        stall_resp_retry_count.reset();
        stall_req_cycles.reset();
        stall_resp_cycles.reset();
        stall_req_retry_cycles.reset();
        stall_resp_retry_cycles.reset();
        //esj 2025-11-29
        crc_error_count.reset();
        //esj 2025-09-09
        // replay_buffer.reset();
    }

    void CXL_ctrl::CXLStats::preDumpStats() {
        ctrl.sampleEndpointStats();
        statistics::Group::preDumpStats();
    }
    //////

    uint64_t
    CXL_ctrl::packetFlitCount(const PacketPtr pkt) const
    {
        if (!pkt) {
            return 0;
        }

        return pkt->cxl_pkt.flit_num == 0 ? 1 : pkt->cxl_pkt.flit_num;
    }

    void
    CXL_ctrl::sampleEndpointStats()
    {
        auto sample = [](statistics::Scalar &total,
                         statistics::Scalar &samples,
                         statistics::Scalar &max_value,
                         size_t value) {
            total += value;
            samples++;
            if (max_value.value() < value) {
                max_value = value;
            }
        };

        stats.controller_req_queue_occupancy_total += transmitList.size();
        stats.controller_req_queue_occupancy_samples++;
        if (stats.controller_req_queue_occupancy_max.value() <
            transmitList.size()) {
            stats.controller_req_queue_occupancy_max = transmitList.size();
        }

        sample(stats.controller_resp_queue_occupancy_total,
               stats.controller_resp_queue_occupancy_samples,
               stats.controller_resp_queue_occupancy_max,
               transmitList_resp.size());
        stats.replay_buffer_host_occupancy_total += cxl_buffer_host.size();
        stats.replay_buffer_host_occupancy_samples++;
        if (stats.replay_buffer_host_occupancy_max.value() <
            cxl_buffer_host.size()) {
            stats.replay_buffer_host_occupancy_max = cxl_buffer_host.size();
        }
        sample(stats.replay_buffer_device_occupancy_total,
               stats.replay_buffer_device_occupancy_samples,
               stats.replay_buffer_device_occupancy_max,
               cxl_buffer_device.size());
        sample(stats.retry_buffer_req_occupancy_total,
               stats.retry_buffer_req_occupancy_samples,
               stats.retry_buffer_req_occupancy_max,
               retry_buffer_req.size());
        sample(stats.retry_buffer_resp_occupancy_total,
               stats.retry_buffer_resp_occupancy_samples,
               stats.retry_buffer_resp_occupancy_max,
               retry_buffer_resp.size());
    }

    void
    CXL_ctrl::recordReqStall()
    {
        stats.stall_req_count++;
        stats.stall_req_cycles += static_cast<uint64_t>(
            ticksToCycles(clockPeriod()));
        sampleEndpointStats();
    }

    void
    CXL_ctrl::recordRespStall()
    {
        stats.stall_resp_count++;
        stats.stall_resp_cycles += static_cast<uint64_t>(
            ticksToCycles(clockPeriod()));
        sampleEndpointStats();
    }

    void
    CXL_ctrl::recordReqRetryStall()
    {
        stats.stall_req_retry_count++;
        stats.stall_req_retry_cycles += static_cast<uint64_t>(
            ticksToCycles(clockPeriod()));
        sampleEndpointStats();
    }

    void
    CXL_ctrl::recordRespRetryStall()
    {
        stats.stall_resp_retry_count++;
        stats.stall_resp_retry_cycles += static_cast<uint64_t>(
            ticksToCycles(clockPeriod()));
        sampleEndpointStats();
    }
    //////

    CXL_ctrl::CXL_ctrlResponsePort::CXL_ctrlResponsePort(const std::string& _name,CXL_ctrl* ctrl)
        : ResponsePort(_name, ctrl), ctrl(ctrl)
    {}


    CXL_ctrl::CXL_ctrlRequestPort::CXL_ctrlRequestPort(const std::string& _name,CXL_ctrl* ctrl)
        : RequestPort(_name, ctrl), ctrl(ctrl)
    {}



    CXL_ctrl :: CXL_ctrl(const Param &p)
    : ClockedObject(p),lanes(p.lanes),
    upstreamResponse(p.name +".upstreamResponse", this),
    downstreamResponse(p.name +".downstreamResponse", this),
    upstreamRequest(p.name + ".upstreamRequest", this ),
    downstreamRequest(p.name +".downstreamRequest", this)
    ,cxl_crc(p.crc),cxl_decoder(p.decoder),cxl_deframer(p.deframer),cxl_encoder(p.encoder),
    cxl_flexbus(p.flexbus),cxl_framer(p.framer),cxl_packing(p.packing),cxl_unpacking(p.unpacking),
    internal_Resquest(p.name +".internal_Resquest", this), internal_Response(p.name +".internal_Response", this),cxl_crc_checker(p.crc_checker),
    maxQueueSize(p.max_queue_size),LRSM(),RRSM(),recv_state(),trans_state(),
    TL2Packingevent([this]{TL2Packing();} , p.name),PHY2Decodingevent([this]{PHY2Decoding();}, p.name),cxl_buffer_host(p.max_queue_size),cxl_packet(NULL),cxl_packet_host(NULL),Host2Device_busy(false)
    ,is_host(p.Host),timeoutEvent([this]{timeoutfunc();} , p.name),retransmit(false),retransmitIdx(0),txQueueEvent([this]{processTxQueue();} , p.name),retryReq(false),mps(p.mps),
    delay(p.delay),delay_var(p.delay_var),sendSeqNum(0),recvSeqNum(0),temp_pkt(NULL),sendEvent([this]{trySendTiming();}, p.name),popseq(false),control_flit_respEvent([this]{control_flit_respfunc();}, p.name)
    ,response_busy(false),retryReq_host(false),retryResp_host(false),sendEventResp([this]{trySendTimingResp();}, p.name),
    cxl_buffer_device(p.max_queue_size),recv_state_device(),trans_state_device(),retryResp(false),
    control_flit_reqEvent([this]{control_flit_reqfunc();}, p.name),request_busy(false),control_flit_resp(NULL),control_flit(NULL),
    retry_buffer_req(p.max_queue_size),retry_buffer_resp(p.max_queue_size),
    req_seqnum(0),resp_seqnum(0),req_seqnum_second(0),recv_packet_num(0),control_flit_cycle(p.control_flit_cycle),
    retry_transmitList_req_size(0),retry_transmitList_resp_size(0), //esj 2025-01-03
    pcie_Resquest(p.name +".pcie_Resquest", this), pcie_Response(p.name +".pcie_Response", this), //esj 2025-01-03
    //esj 2025-01-17
    crc_error_control_flit_cycle(p.crc_error_control_flit_cycle),crc_error_resp_seqnum(-1),crc_error_req_seqnum(-1),
    read_queue_size(p.read_queue_size),reserved_read_queue(0) //esj 2025-01-29
    ,queuing_delay(p.queuing_delay)
    ,stats(*this) //esj 2025-02-24
    ,downstreamResponse2(p.name +".downstreamResponse2", this) //esj 2025-06-22
    ,upstreamRequest2(p.name + ".upstreamRequest2", this ) //esj 2025-06-22
    ,upstreamResponse2(p.name +".upstreamResponse2", this) //esj 2025-06-22
    ,downstreamRequest2(p.name + ".downstreamRequest2", this ) //esj 2025-06-22
    {
        //CXL timeout(50us - 10ms) -> maximum 10ms
        CXL_mem_retryTime = 10000000000;

        //esj 2025-05-13
        is_host_retry = false;

        // esj 2025-05-16
        read_turn = true;

        for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds;
             ++host_idx) {
            req_seqnum_by_host[host_idx] = 0;
            resp_seqnum_by_host[host_idx] = 0;
        }
    }

    CXL_ctrl::~CXL_ctrl()
    {
        // Free dynamically allocated timeout events that may still be pending.
        for (auto &entry : seqEvents) {
            if (entry.second) {
                if (entry.second->scheduled()) {
                    deschedule(*(entry.second));
                }
                delete entry.second;
            }
        }
        seqEvents.clear();

        for (auto &entry : seqEvents_device) {
            if (entry.second) {
                if (entry.second->scheduled()) {
                    deschedule(*(entry.second));
                }
                delete entry.second;
            }
        }
        seqEvents_device.clear();
    }

    uint8_t
    CXL_ctrl::normalizeHostIdx(uint8_t source_host_idx) const
    {
        if (source_host_idx >= Packet::MaxPciRequesterIds) {
            DPRINTF(CXL_ctrl,
                    "%s, invalid source_host_idx=%d, clamp to %d\n",
                    __func__,
                    source_host_idx,
                    Packet::MaxPciRequesterIds - 1);
            return Packet::MaxPciRequesterIds - 1;
        }
        return source_host_idx;
    }

    uint8_t
    CXL_ctrl::packetHostIdx(const PacketPtr pkt) const
    {
        if (pkt == nullptr) {
            return 0;
        }
        return normalizeHostIdx(pkt->source_host_idx);
    }

    uint64_t&
    CXL_ctrl::reqSeqnumByHost(uint8_t source_host_idx)
    {
        return req_seqnum_by_host[normalizeHostIdx(source_host_idx)];
    }

    const uint64_t&
    CXL_ctrl::reqSeqnumByHost(uint8_t source_host_idx) const
    {
        return req_seqnum_by_host[normalizeHostIdx(source_host_idx)];
    }

    uint64_t&
    CXL_ctrl::respSeqnumByHost(uint8_t source_host_idx)
    {
        return resp_seqnum_by_host[normalizeHostIdx(source_host_idx)];
    }

    const uint64_t&
    CXL_ctrl::respSeqnumByHost(uint8_t source_host_idx) const
    {
        return resp_seqnum_by_host[normalizeHostIdx(source_host_idx)];
    }

    std::deque<CXL_ctrl::retrypacket>&
    CXL_ctrl::errorReqListByHost(uint8_t source_host_idx)
    {
        return error_req_transmitList_by_host[normalizeHostIdx(source_host_idx)];
    }

    const std::deque<CXL_ctrl::retrypacket>&
    CXL_ctrl::errorReqListByHost(uint8_t source_host_idx) const
    {
        return error_req_transmitList_by_host[normalizeHostIdx(source_host_idx)];
    }

    bool
    CXL_ctrl::needsDeviceReplaySlot(PacketPtr pkt) const
    {
        return pkt && pkt->isResponse() &&
            !pkt->cxl_pkt.retry_resp &&
            !pkt->cxl_pkt.is_controlflit &&
            !pkt->cxl_pkt.retry_req &&
            !pkt->is_cxl_write_resp;
    }

    size_t
    CXL_ctrl::pendingDeviceReplaySlots() const
    {
        size_t pending = 0;
        for (const DeferredPacket &entry : transmitList_resp) {
            if (needsDeviceReplaySlot(entry.pkt))
                ++pending;
        }
        return pending;
    }

    bool
    CXL_ctrl::deviceReplayBlocksAdmission(PacketPtr pkt)
    {
        if (!needsDeviceReplaySlot(pkt))
            return false;

        return static_cast<size_t>(cxl_buffer_device.size()) +
            pendingDeviceReplaySlots() >=
            static_cast<size_t>(maxQueueSize);
    }

    PacketPtr
    CXL_ctrl::cloneDeviceReplayPacket(PacketPtr pkt) const
    {
        if (!pkt)
            return nullptr;

        // Packet's copy constructor may alias STATIC_DATA and does not copy
        // FlexCXL transport metadata. A replay copy must own both.
        PacketPtr copy = new Packet(pkt, true, false);
        copy->senderState = nullptr;
        copy->headerDelay = 0;
        copy->payloadDelay = 0;
        copy->snoopDelay = 0;

        copy->cxl_pkt = pkt->cxl_pkt;
        copy->cxl_flag = pkt->cxl_flag;
        copy->is_cxl_mem = pkt->is_cxl_mem;
        copy->is_cxl_io = pkt->is_cxl_io;
        copy->is_retry = pkt->is_retry;
        copy->is_cxl_write_resp = pkt->is_cxl_write_resp;
        copy->MetaField = pkt->MetaField;
        copy->MetaValue = pkt->MetaValue;
        copy->origin_dest = pkt->origin_dest;
        copy->origin_addr = pkt->origin_addr;
        copy->numa_mode = pkt->numa_mode;
        copy->cxlTransportSize = pkt->cxlTransportSize;
        copy->cxlMergeOverhead = pkt->cxlMergeOverhead;
        copy->cxlMergeId = pkt->cxlMergeId;
        copy->cxlMergeValid = pkt->cxlMergeValid;

        if (pkt->hasData()) {
            copy->allocate();
            copy->setData(pkt->getConstPtr<uint8_t>());
        }

        return copy;
    }

    PacketPtr
    CXL_ctrl::cloneDeviceReplayPacket(uint64_t seq_num) const
    {
        const auto it = deviceReplaySnapshots.find(seq_num);
        if (it == deviceReplaySnapshots.end())
            return nullptr;
        return cloneDeviceReplayPacket(it->second.get());
    }

    bool
    CXL_ctrl::insertDeviceReplaySnapshot(PacketPtr pkt)
    {
        const uint64_t seq_num = pkt->cxl_pkt.seqNum;

        // One exact-sequence entry is indexed by the legacy replay buffer;
        // the map owns its lifetime.
        cxl_buffer_device.findByseqnum2(seq_num);
        deviceReplaySnapshots.erase(seq_num);

        PacketPtr snapshot = cloneDeviceReplayPacket(pkt);
        const int size_before = cxl_buffer_device.size();
        cxl_buffer_device.pushBack(snapshot);
        if (cxl_buffer_device.size() != size_before + 1) {
            delete snapshot;
            return false;
        }

        deviceReplaySnapshots.emplace(
            seq_num, std::unique_ptr<Packet>(snapshot));
        return true;
    }

    void
    CXL_ctrl::releaseDeviceReplaySnapshot(uint64_t seq_num)
    {
        deviceReplaySnapshots.erase(seq_num);
    }

    void
    CXL_ctrl::init()
    {
        // if (!upstreamRequest.isConnected() || !upstreamResponse.isConnected() ||
        // !downstreamRequest.isConnected() || !downstreamResponse.isConnected()) {

        //     fatal ("CXL_ctrlPorts must be connected !!\n") ;
        // }
        if(is_host){
            upstreamResponse.sendRangeChange();
            upstreamResponse2.sendRangeChange();
        }
        else
            downstreamResponse.sendRangeChange();
    }

    bool
    CXL_ctrl::TL2PHY_interface(PacketPtr pkt){
        DPRINTF(CXL_ctrl, "[%s] START: processing packet: addr=0x%x\n", __func__, pkt->getAddr());
        bool result = false;
        //esj 2024-12-12
        //send request packet to device
        if(pkt->isRequest()){
            DPRINTF(CXL_ctrl, "[%s] buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d, recv_packet_num = %d/%d\n",
                __func__, cxl_buffer_host.size(), maxQueueSize,
                transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size,recv_packet_num,maxQueueSize);
            // if (transmitList.size() > maxQueueSize -1 || cxl_buffer_host.size() > maxQueueSize-1
            //     || recv_packet_num > maxQueueSize -1 //esj 2024-12-25

            //esj 2025-05-08
            //esj 2025-05-10
            //esj 2025-07-09
            // if(retry_pkt && !pkt->cxl_pkt.is_retry_req){
            //     DPRINTF(CXL_ctrl,"%s, retry = %s\n",__func__,retry_pkt ? "true" : "false");
            //     warn("%s, retry = %s\n",__func__,retry_pkt ? "true" : "false");

            //     //esj 2025-06-29
            //     retryReq_host = true;

            //     return false;
            // }


            //esj 2025-05-15
            //esj 2025-06-30
            // if(pkt->isRead() && (reserved_read_queue > read_queue_size -1)){
            //     retryReq_host = true;
            //     warn("esj  %s cxl ctrl blocked, reserved_read_queue/read_queue_size = %d/%d\n",__func__,reserved_read_queue,read_queue_size);
            //     return false;
            // }

            // esj 2025-05-16
            if (transmitList.size() > maxQueueSize -1 || cxl_buffer_host.size() > maxQueueSize-1
            // if ((transmitList.size() + transmitList_write.size()) > (maxQueueSize -1) || cxl_buffer_host.size() > (maxQueueSize-1)
                || recv_packet_num > (maxQueueSize -1) //esj 2024-12-25
                // || reserved_read_queue > read_queue_size -1 //esj 2025-01-29 //esj 2025-05-15
                // LRSM.Local_state == LRSM.RETRY_LLRREQ) {
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d\n",
                        __func__, cxl_buffer_host.size(), maxQueueSize,
                        transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size);
                retryReq_host = true;

                //esj 2025-12-29
                recordReqStall();

                // warn("esj  %s cxl ctrl blocked\n",__func__);
                return false;
            }

            //esj 2024-12-25
            //esj 2025-02-26
            // recv_packet_num++;
            //esj 2025-06-29
            //esj 2025-07-09
            if(!pkt->cxl_pkt.is_controlflit ){
            // if(!pkt->cxl_pkt.is_controlflit && !pkt->cxl_pkt.is_retry_req){
                recv_packet_num++;
            }
            DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",recv_packet_num);

            //esj 2025-01-29
            //esj 2025-02-26
            // if(pkt->isRead()){
            //esj 2025-06-29
            // if(pkt->isRead() & !pkt->cxl_pkt.is_controlflit){
            //esj 2025-06-30
            // if(pkt->isRead() & !pkt->cxl_pkt.is_controlflit && !pkt->cxl_pkt.is_retry_req){
            //     reserved_read_queue++;
            //     DPRINTF(CXL_ctrl, "reserved_read_queue num = %d\n",reserved_read_queue);
            // }


            //esj 2024-12-17
            // if(!pkt->cxl_pkt.retry_resp){
            //     pkt->cxl_pkt.seqNum = trans_state.send_packet_num;
            //     pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            //     pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();


            //     DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
            //             __func__, pkt->cxl_pkt.seqNum,
            //             pkt->isRead() && pkt->isRequest(),
            //             pkt->isWrite() && pkt->isRequest(),
            //             pkt->isRead() && pkt->isResponse(),
            //             pkt->isWrite() && pkt->isResponse());

            //     trans_state.send_packet_num++;
            //     DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n",
            //             __func__, trans_state.send_packet_num);
            // }

            //esj 2025-06-29
            //esj 2025-07-09
            // if(pkt->cxl_pkt.is_retry_req){
            //     warn("esj  %s cxl ctrl retry req pkt\n",__func__);
            //     result = transmit_cxl(pkt);
            //     return result;
            // }


            //esj 2024-12-18
            pkt->cxl_pkt.LRSM_host = LRSM.cur_stat();

            //esj 2025-07-21
            // pkt->cxl_pkt.seqNum = trans_state.send_packet_num;
            pkt->cxl_pkt.seqNum = trace::send_packet_num;

            //esj 2025-01-17
            if(trans_state.send_packet_num == 0){
                pkt->cxl_pkt.start = true;
            }
            else{
                pkt->cxl_pkt.start = false;
            }

            pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();

            const uint8_t host_idx = packetHostIdx(pkt);
            pkt->cxl_pkt.resp_seqnum = respSeqnumByHost(host_idx);

            //esj 2025-01-17
            if(error_resp_transmitList.size() > 0){
                uint64_t seq_num = error_resp_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_resp_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                error_resp_transmitList.pop_front();

                if(control_flit_reqEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_reqEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_reqEvent\n",__func__);
                    bool pop_success = popfromtransmitlist_req(seq_num);

                    if(pop_success){
                        DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                    }
                }
            }
            else{
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                //esj 2025-01-18
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
            }


            //

            //esj 2024-12-30
            // if(cxl_buffer_host.size() == 0 && trans_state.send_packet_num!= 0){
            //     int num = trans_state.send_packet_num -1;

            //     while(num){
            //         cancelSeqEvent(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }

            //esj 2024-12-23
            // if(cxl_buffer_host.size() == 0 && resp_seqnum!= 0){
            //     int num = resp_seqnum;
            //     while(num){
            //         cancelSeqEvent(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }



            DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
                    __func__, pkt->cxl_pkt.seqNum,
                    pkt->isRead() && pkt->isRequest(),
                    pkt->isWrite() && pkt->isRequest(),
                    pkt->isRead() && pkt->isResponse(),
                    pkt->isWrite() && pkt->isResponse());
            //esj 2025-03-02
            trans_state.send_packet_num++;
            //esj 2025-07-21
            ++trace::send_packet_num;


            // if(trans_state.send_packet_num > UINT64_MAX -1){
            //     DPRINTF(CXL_ctrl, "[%s] reset seqnum = %d\n", __func__, trans_state.send_packet_num);
            //     trans_state.send_packet_num = 1;
            // }
            DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n",
                    __func__, trans_state.send_packet_num);
            DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n",
                    __func__, trace::send_packet_num);

            result = transmit_cxl(pkt);
            sendSeqNum++;

            DPRINTF(CXL_ctrl, "[%s] END: transmission_status=%s\n", __func__, result ? "success" : "failed");
        }
        //send response packet to host
        else{
            DPRINTF(CXL_ctrl, "[%s] cxl_buffer_device size=%d/%d, transmitList_resp_size=%d/%d, state=%s\n",
                __func__, cxl_buffer_device.size(), maxQueueSize,
                transmitList_resp.size(), maxQueueSize, RRSM.cur_stat());
            const bool tx_queue_blocked =
                transmitList_resp.size() > maxQueueSize - 1;
            const size_t pending_replay = pendingDeviceReplaySlots();
            const bool replay_blocked = deviceReplayBlocksAdmission(pkt);
            if (tx_queue_blocked || replay_blocked
            // || recv_packet_num > maxQueueSize -1 //esj 2024-12-26
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl,
                        "[%s] BLOCKED: cxl_buffer_device size=%d/%d, "
                        "pending_replay=%d, "
                        "transmitList_resp_size=%d/%d, state=%s\n",
                        __func__, cxl_buffer_device.size(), maxQueueSize,
                        static_cast<int>(pending_replay),
                        transmitList_resp.size(), maxQueueSize,
                        RRSM.cur_stat());
                // warn("esj  %s cxl ctrl blocked\n",__func__);
                retryResp_host = true;
                return false;
            }

            //esj 2025-01-29
            // cxl_buffer_device.eraseinvalid();
            //

            // //esj 2024-12-26
            // recv_packet_num --;
            // DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",recv_packet_num);

            // //esj 2024-12-17
            // if(!pkt->cxl_pkt.retry_req){
            //     pkt->cxl_pkt.seqNum = trans_state_device.send_packet_num;
            //     pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            //     pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();


            //     DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
            //             __func__, pkt->cxl_pkt.seqNum,
            //             pkt->isRead() && pkt->isRequest(),
            //             pkt->isWrite() && pkt->isRequest(),
            //             pkt->isRead() && pkt->isResponse(),
            //             pkt->isWrite() && pkt->isResponse());

            //     trans_state_device.send_packet_num++;
            //     DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n", __func__, trans_state_device.send_packet_num);
            // }

            //esj 2024-12-17
            // pkt->cxl_pkt.seqNum = trans_state_device.send_packet_num;

            pkt->cxl_pkt.LRSM_device = LRSM.cur_stat();
            pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();

            //esj2024-12-22
            const uint8_t host_idx = packetHostIdx(pkt);
            pkt->cxl_pkt.req_seqnum = reqSeqnumByHost(host_idx);

            // Assign a deterministic seqNum for newly generated device->host response flits.
            // Without this, stale/uninitialized seqNum can leak into replay buffer.
            if (!pkt->cxl_pkt.is_controlflit &&
                !pkt->cxl_pkt.retry_req &&
                !pkt->cxl_pkt.retry_resp) {
                pkt->cxl_pkt.seqNum = trans_state_device.send_packet_num;
                pkt->cxl_pkt.start = (trans_state_device.send_packet_num == 0);
            }

            //esj 2025-01-17
            auto &error_req_list = errorReqListByHost(host_idx);
            if(error_req_list.size() > 0){
                uint64_t seq_num = error_req_list.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_req_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                error_req_list.pop_front();
                DPRINTF(CXL_ctrl, "======================[ error_req_transmitList host=%d ]======================\n", host_idx);
                DPRINTF(CXL_ctrl, "error_req_transmitList front seqnum = %llu, list size = %d\n",seq_num,error_req_list.size());

                if(control_flit_respEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_respEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_respEvent\n",__func__);
                    bool pop_success = popfromtransmitlist_resp(seq_num);

                    if(pop_success){
                        DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                    }

                }
            }
            else{
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                //esj 2025-01-18
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
            }

            //

            //esj 2024-12-30
            // if(cxl_buffer_device.size() == 0){
            //     // int num = pkt->cxl_pkt.seqNum;
            //     //esj 2024-12-23
            //     int num = pkt->cxl_pkt.req_seqnum;

            //     while(num){
            //         cancelSeqEvent_device(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }


            DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
                    __func__, pkt->cxl_pkt.seqNum,
                    pkt->isRead() && pkt->isRequest(),
                    pkt->isWrite() && pkt->isRequest(),
                    pkt->isRead() && pkt->isResponse(),
                    pkt->isWrite() && pkt->isResponse());

            trans_state_device.send_packet_num++;
            DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n", __func__, trans_state_device.send_packet_num);

            result = transmit_cxl_device(pkt);
            sendSeqNum++;


            DPRINTF(CXL_ctrl, "[%s] END: transmission_status=%s\n", __func__, result ? "success" : "failed");
        }

        return result;
    }

    //esj 2025-06-27
    bool
    CXL_ctrl::TL2PHY_interface2(PacketPtr pkt){
        DPRINTF(CXL_ctrl, "[%s] START: processing packet: addr=0x%x\n", __func__, pkt->getAddr());
        bool result = false;
        //esj 2024-12-12
        //send request packet to device
        if(pkt->isRequest()){
            DPRINTF(CXL_ctrl, "[%s] buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d, recv_packet_num = %d/%d\n",
                __func__, cxl_buffer_host.size(), maxQueueSize,
                transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size,recv_packet_num,maxQueueSize);
            // if (transmitList.size() > maxQueueSize -1 || cxl_buffer_host.size() > maxQueueSize-1
            //     || recv_packet_num > maxQueueSize -1 //esj 2024-12-25

            //esj 2025-05-08
            //esj 2025-05-10
            //esj 2025-07-09
            // if(retry_pkt && !pkt->cxl_pkt.is_retry_req){
            //     DPRINTF(CXL_ctrl,"%s, retry = %s\n",__func__,retry_pkt ? "true" : "false");
            //     warn("%s, retry = %s\n",__func__,retry_pkt ? "true" : "false");
            //     return false;
            // }


            //esj 2025-05-15
            if(pkt->isRead() && (reserved_read_queue > read_queue_size -1)){
                retryReq_host = true;
                return false;
            }

            // esj 2025-05-16
            if (transmitList.size() > maxQueueSize -1 || cxl_buffer_host.size() > maxQueueSize-1
            // if ((transmitList.size() + transmitList_write.size()) > (maxQueueSize -1) || cxl_buffer_host.size() > (maxQueueSize-1)
                || recv_packet_num > (maxQueueSize -1) //esj 2024-12-25
                // || reserved_read_queue > read_queue_size -1 //esj 2025-01-29 //esj 2025-05-15
                // LRSM.Local_state == LRSM.RETRY_LLRREQ) {
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d\n",
                        __func__, cxl_buffer_host.size(), maxQueueSize,
                        transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size);
                retryReq_host = true;
                return false;
            }

            //esj 2024-12-25
            //esj 2025-02-26
            // recv_packet_num++;
            if(!pkt->cxl_pkt.is_controlflit){
                recv_packet_num++;
            }
            DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",recv_packet_num);

            //esj 2025-01-29
            //esj 2025-02-26
            // if(pkt->isRead()){
            if(pkt->isRead() & !pkt->cxl_pkt.is_controlflit){
                reserved_read_queue++;
                DPRINTF(CXL_ctrl, "reserved_read_queue num = %d\n",reserved_read_queue);
            }


            //esj 2024-12-17
            // if(!pkt->cxl_pkt.retry_resp){
            //     pkt->cxl_pkt.seqNum = trans_state.send_packet_num;
            //     pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            //     pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();


            //     DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
            //             __func__, pkt->cxl_pkt.seqNum,
            //             pkt->isRead() && pkt->isRequest(),
            //             pkt->isWrite() && pkt->isRequest(),
            //             pkt->isRead() && pkt->isResponse(),
            //             pkt->isWrite() && pkt->isResponse());

            //     trans_state.send_packet_num++;
            //     DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n",
            //             __func__, trans_state.send_packet_num);
            // }
            //esj 2024-12-18
            pkt->cxl_pkt.LRSM_host = LRSM.cur_stat();

            pkt->cxl_pkt.seqNum = trans_state.send_packet_num;

            //esj 2025-01-17
            if(trans_state.send_packet_num == 0){
                pkt->cxl_pkt.start = true;
            }
            else{
                pkt->cxl_pkt.start = false;
            }

            pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();

            const uint8_t host_idx = packetHostIdx(pkt);
            pkt->cxl_pkt.resp_seqnum = respSeqnumByHost(host_idx);

            //esj 2025-01-17
            if(error_resp_transmitList.size() > 0){
                uint64_t seq_num = error_resp_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_resp_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                error_resp_transmitList.pop_front();

                if(control_flit_reqEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_reqEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_reqEvent\n",__func__);
                    bool pop_success = popfromtransmitlist_req(seq_num);

                    if(pop_success){
                        DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                    }
                }
            }
            else{
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                //esj 2025-01-18
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
            }


            //

            //esj 2024-12-30
            // if(cxl_buffer_host.size() == 0 && trans_state.send_packet_num!= 0){
            //     int num = trans_state.send_packet_num -1;

            //     while(num){
            //         cancelSeqEvent(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }

            //esj 2024-12-23
            // if(cxl_buffer_host.size() == 0 && resp_seqnum!= 0){
            //     int num = resp_seqnum;
            //     while(num){
            //         cancelSeqEvent(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }



            DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
                    __func__, pkt->cxl_pkt.seqNum,
                    pkt->isRead() && pkt->isRequest(),
                    pkt->isWrite() && pkt->isRequest(),
                    pkt->isRead() && pkt->isResponse(),
                    pkt->isWrite() && pkt->isResponse());
            //esj 2025-03-02
            trans_state.send_packet_num++;
            // if(trans_state.send_packet_num > UINT64_MAX -1){
            //     DPRINTF(CXL_ctrl, "[%s] reset seqnum = %d\n", __func__, trans_state.send_packet_num);
            //     trans_state.send_packet_num = 1;
            // }
            DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n",
                    __func__, trans_state.send_packet_num);

            result = transmit_cxl(pkt);
            sendSeqNum++;

            DPRINTF(CXL_ctrl, "[%s] END: transmission_status=%s\n", __func__, result ? "success" : "failed");
        }
        //send response packet to host
        else{
            DPRINTF(CXL_ctrl, "[%s] cxl_buffer_device size=%d/%d, transmitList_resp_size=%d/%d, state=%s\n",
                __func__, cxl_buffer_device.size(), maxQueueSize,
                transmitList_resp.size(), maxQueueSize, RRSM.cur_stat());

            cxl_buffer_device.eraseinvalid();

            const bool tx_queue_blocked =
                transmitList_resp.size() > maxQueueSize - 1;
            const size_t pending_replay = pendingDeviceReplaySlots();
            const bool replay_blocked = deviceReplayBlocksAdmission(pkt);
            if (tx_queue_blocked || replay_blocked
            // || recv_packet_num > maxQueueSize -1 //esj 2024-12-26
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl,
                        "[%s] BLOCKED: cxl_buffer_device size=%d/%d, "
                        "pending_replay=%d, "
                        "transmitList_resp_size=%d/%d, state=%s\n",
                        __func__, cxl_buffer_device.size(), maxQueueSize,
                        static_cast<int>(pending_replay),
                        transmitList_resp.size(), maxQueueSize,
                        RRSM.cur_stat());
                retryResp_host = true;

                //esj 2025-12-29
                recordRespStall();
                // warn("esj  %s cxl ctrl blocked\n",__func__);
                return false;
            }

            //esj 2025-01-29
            //esj 2025-06-30
            // 2430 -> 2410
            // cxl_buffer_device.eraseinvalid();
            //

            // //esj 2024-12-26
            // recv_packet_num --;
            // DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",recv_packet_num);

            // //esj 2024-12-17
            // if(!pkt->cxl_pkt.retry_req){
            //     pkt->cxl_pkt.seqNum = trans_state_device.send_packet_num;
            //     pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            //     pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            //     pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();


            //     DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
            //             __func__, pkt->cxl_pkt.seqNum,
            //             pkt->isRead() && pkt->isRequest(),
            //             pkt->isWrite() && pkt->isRequest(),
            //             pkt->isRead() && pkt->isResponse(),
            //             pkt->isWrite() && pkt->isResponse());

            //     trans_state_device.send_packet_num++;
            //     DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n", __func__, trans_state_device.send_packet_num);
            // }

            //esj 2024-12-17
            // pkt->cxl_pkt.seqNum = trans_state_device.send_packet_num;

            pkt->cxl_pkt.LRSM_device = LRSM.cur_stat();
            pkt->cxl_pkt.isReq = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isRwD = pkt->isWrite() && pkt->isRequest();
            pkt->cxl_pkt.isDRS = pkt->isRead() && pkt->isRequest();
            pkt->cxl_pkt.isNDR = pkt->isWrite() && pkt->isResponse();

            //esj2024-12-22
            const uint8_t host_idx = packetHostIdx(pkt);
            pkt->cxl_pkt.req_seqnum = reqSeqnumByHost(host_idx);

            //esj 2025-01-17
            //esj 2025-07-22
            // if(error_req_transmitList.size() > 0){
            //     uint64_t seq_num = error_req_transmitList.front().crc_error_seqnum;
            //     pkt->cxl_pkt.crc_error_req_seqnum = seq_num;
            //     pkt->cxl_pkt.crc_error_resp_seqnum = -1;
            //     error_req_transmitList.pop_front();
            //     DPRINTF(CXL_ctrl, "======================[ error_req_transmitList ]======================\n");
            //     DPRINTF(CXL_ctrl, "error_req_transmitList front seqnum = %llu, list size = %d\n",seq_num,error_req_transmitList.size());

            //     if(control_flit_respEvent.scheduled()){
            //         DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_respEvent ]======================\n");
            //         DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_respEvent\n",__func__);
            //         bool pop_success = popfromtransmitlist_resp(seq_num);

            //         if(pop_success){
            //             DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
            //         }

            //     }
            // }
            // else{
            //     pkt->cxl_pkt.crc_error_req_seqnum = -1;
            //     //esj 2025-01-18
            //     pkt->cxl_pkt.crc_error_resp_seqnum = -1;
            // }

            auto &error_req_list = errorReqListByHost(host_idx);
            if(error_req_list.size() > 0){
                uint64_t seq_num = error_req_list.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_req_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                error_req_list.pop_front();
                DPRINTF(CXL_ctrl, "======================[ error_req_transmitList host=%d ]======================\n", host_idx);
                DPRINTF(CXL_ctrl, "error_req_transmitList front seqnum = %llu, list size = %d\n",seq_num,error_req_list.size());
                if(control_flit_respEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_respEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_respEvent\n",__func__);
                    bool pop_success = popfromtransmitlist_resp(seq_num);

                    if(pop_success){
                        DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                    }
                }
            }
            else{
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
            }
            ///

            //

            //esj 2024-12-30
            // if(cxl_buffer_device.size() == 0){
            //     // int num = pkt->cxl_pkt.seqNum;
            //     //esj 2024-12-23
            //     int num = pkt->cxl_pkt.req_seqnum;

            //     while(num){
            //         cancelSeqEvent_device(num);
            //         num--;
            //     }
            //     DPRINTF(CXL_ctrl, "reset timeout\n");
            // }


            DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d\n",
                    __func__, pkt->cxl_pkt.seqNum,
                    pkt->isRead() && pkt->isRequest(),
                    pkt->isWrite() && pkt->isRequest(),
                    pkt->isRead() && pkt->isResponse(),
                    pkt->isWrite() && pkt->isResponse());

            trans_state_device.send_packet_num++;
            DPRINTF(CXL_ctrl, "[%s] Transmission stats: total_packets=%d\n", __func__, trans_state_device.send_packet_num);

            result = transmit_cxl_device(pkt);
            sendSeqNum++;


            DPRINTF(CXL_ctrl, "[%s] END: transmission_status=%s\n", __func__, result ? "success" : "failed");
        }

        return result;
    }

    bool
    CXL_ctrl::transmit_cxl(PacketPtr pkt){
        DPRINTF(CXL_ctrl,"[%s], transmit\n",__func__);
        // if (busyhost()) {
        //     DPRINTF(CXL_ctrl, "packet not sent, link busy\n");
        //     return false;
        // }

        //esj 2024-12-23
        if(pkt->isResponse()){
            assert(pkt->isRequest());
            return false;
        }


        cxl_packet = pkt;
        Host2Device_busy = true;

        // if(!TL2Packingevent.scheduled()){
        //     this->schedule(TL2Packingevent, curTick());
        //     return true;
        // }
        // else {
        //     return false;
        // }

        //esj 2024-12-05
        //esj 2025-03-02
        // if (!retryReq) {
        if (!retryReq_host) {
            Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;
            DPRINTF(CXL_ctrl, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);
            schedTimingReq(pkt, curTick() + receive_delay + delay);
        }
        else{
            //esj 2025-12-29
            recordReqRetryStall();
        }

        //esj 2025-03-02
        // return !retryReq;
        return !retryReq_host;

    }

    bool
    CXL_ctrl::transmit_cxl_device(PacketPtr pkt){
        DPRINTF(CXL_ctrl,"[%s], transmit\n",__func__);

        //esj 2024-12-23
        if(pkt->isRequest()){
            assert(pkt->isResponse());
            return false;
        }


        cxl_packet_device = pkt;
        Device2Host_busy = true;

        //esj 2025-03-02
        // if (!retryResp) {
        if (!retryResp_host) {
            Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;
            DPRINTF(CXL_ctrl, "[%s] receive_delay = %d, delay = %d\n",__func__, receive_delay, delay);
            schedTimingResp(pkt, curTick() + receive_delay + delay);
        }
        else{
            //esj 2025-12-29
            recordRespRetryStall();
        }

        //esj 2025-03-02
        // return !retryResp;
        return !retryResp_host;

    }



    void
    CXL_ctrl::TL2Packing(){
        DPRINTF(CXL_ctrl,"%s, TL2Packing\n",__func__);
        bool success = cxl_packing->in_port.recvTimingReq(cxl_packet);

        if(success){
            DPRINTF(CXL_ctrl,"%s, send success = %d\n",__func__,success);
            Host2Device_busy = false;
            trans_state.send_packet_num++;

            if (cxl_packet == replayPacket) {
                DPRINTF(CXL_ctrl,"%s, pushing packet to host buffer, addr=%0x\n",
                        __func__, cxl_packet->getAddr());
                cxl_buffer_host.pushBack(cxl_packet);

                DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",
                        __func__, cxl_buffer_host.size());

                replayPacket = NULL;
                if (!timeoutEvent.scheduled()) {
                    DPRINTF(CXL_ctrl,"%s, timeout start, end = %d \n",__func__,curTick() + CXL_mem_retryTime);
                    this->schedule(timeoutEvent , curTick() + CXL_mem_retryTime) ;
                }
                if (retryReq) {
                    DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                    retryReq = false ;
                    this->upstreamResponse.sendRetryReq();
                }
            }
            else{
                DPRINTF(CXL_ctrl,"%s, transmit pending TLP if any\n",__func__);
                transmit_cxl(replayPacket);
            }
        }
        else{
            txQueue.emplace_back(std::make_pair(curTick() + this->clockPeriod(), cxl_packet));
                if (!txQueueEvent.scheduled())
                    this->schedule(txQueueEvent, txQueue.front().first);
        }

        if(retransmit){
            if (!timeoutEvent.scheduled()) {
                    this->schedule(timeoutEvent , curTick() + CXL_mem_retryTime) ;
                }

                if (retransmitIdx >= cxl_buffer_host.size()) {
                    retransmitIdx = 0 ;
                    retransmit = false ;
                    if (retryReq) {
                        retryReq = false ;
                        this->upstreamResponse.sendRetryReq();
                    }
                } else {
                    transmit_cxl(cxl_buffer_host.get(retransmitIdx) ) ;
                    retransmitIdx ++ ;
                    return ;
                }
        }
    }


    bool
    CXL_ctrl::PHY2TL_interface(PacketPtr pkt){
        DPRINTF(CXL_ctrl,"%s, PHY2TL_interface\n",__func__);

        if (busyhost()) {
            DPRINTF(CXL_ctrl, "packet not sent, link busy\n");
            return false;
        }

        cxl_packet_host = pkt;
        Host2Device_busy = true;

        if(!PHY2Decodingevent.scheduled()){
            this->schedule(PHY2Decodingevent, curTick() + this->clockPeriod());
            return true;
        }
        else{
            return false;
        }
    }

    void
    CXL_ctrl::PHY2Decoding(){
        DPRINTF(CXL_ctrl,"%s, PHY2Decoding\n",__func__);
        bool success = cxl_decoder->in_port.recvTimingReq(cxl_packet_host);

        if(success){
            Host2Device_busy = false;
            retryReq_host = false;

        }
        else{
            retryReq_host = true;
            this->downstreamResponse.recvTimingReq(cxl_packet_host);
        }
    }

    void
    CXL_ctrl::timeoutfunc()
    {
        DPRINTF(CXL_ctrl, "[%s] START: checking timeout conditions\n", __func__);

        if (cxl_buffer_host.size() == 0 || cxl_buffer_host.front() == NULL || retransmit == true) {
            DPRINTF(CXL_ctrl, "[%s] SKIP: buffer_empty=%d, front_null=%d, retransmit=%d\n",
                    __func__, cxl_buffer_host.size() == 0,
                    cxl_buffer_host.front() == NULL, retransmit);
            return;
        }

        DPRINTF(CXL_ctrl, "[%s] Initiating retransmission\n", __func__);
        bool success = transmit_cxl(cxl_buffer_host.front());
        retransmit = true;
        retransmitIdx = (success) ? 1 : 0;

        DPRINTF(CXL_ctrl, "[%s] END: retransmission_status=%s, next_idx=%d\n",
                __func__, success ? "success" : "failed", retransmitIdx);
    }

    void
    CXL_ctrl::processTxQueue()
    {
        auto cur(txQueue.front());
        txQueue.pop_front();

        // Schedule a new event to process the next packet in the queue.
        if (!txQueue.empty()) {
            auto next(txQueue.front());
            assert(next.first > curTick());
            this->schedule(txQueueEvent, next.first);
        }

        assert(cur.first == curTick());
        txComplete(cur.second);
    }

    void
    CXL_ctrl::txComplete(PacketPtr packet)
    {
        DPRINTF(CXL_ctrl, "%s,packet received: len=%d\n",__func__, packet->cxl_pkt.packing_size);

        TL2PHY_interface(cxl_packet);
    }

    bool
    CXL_ctrl::recvresponse(PacketPtr packet){
        DPRINTF(CXL_ctrl, "[%s] START: addr=0x%x, size=%d\n",
                __func__, packet->getAddr(), packet->cxl_pkt.packing_size);

        if (timeoutEvent.scheduled()) {
            DPRINTF(CXL_ctrl, "[%s] Canceling scheduled timeout\n", __func__);
            this->deschedule(timeoutEvent);
        }

        bool success = false;
        if (!packet->cxl_pkt.is_controlflit) {
            DPRINTF(CXL_ctrl,"=============== response flit response ===============\n");
            DPRINTF(CXL_ctrl, "[%s] Processing response flit, send_packet num = %d\n", __func__, trans_state.send_packet_num);

            // Record the link-level receive sequence before handing ownership
            // of the response to the upstream receiver.  The synchronous
            // sendTimingResp() call may consume or mutate the packet, and a
            // re-entrant request generated by that response must already
            // carry this D2H acknowledgement.
            const uint8_t host_idx = packetHostIdx(packet);
            const uint64_t seq_num = packet->cxl_pkt.seqNum;
            respSeqnumByHost(host_idx) = seq_num;
            // Keep legacy scalar for compatibility/debug traces.
            resp_seqnum = respSeqnumByHost(host_idx);

            //esj 2025-05-25
            //esj 2025-07-12
            // Addr curent_pkt_addr = packet->getAddr();
            // packet->setAddr(packet->origin_addr);

            //esj 2025-06-22
            // success = this->upstreamResponse.sendTimingResp(packet);
            success = this->upstreamResponse2.sendTimingResp(packet);

            // if (success) {
            //     uint64_t seq_num = packet->cxl_pkt.ack_infomation.recv_Packet_num;
            //     DPRINTF(CXL_ctrl, "[%s] Response sent: ack_seq=%d\n", __func__, seq_num);
            //     bool flag = false ;
            //     while (!flag) {
            //         temp_pkt = cxl_buffer_host.front() ;
            //         if(cxl_buffer_host.size() != 0 ){
            //             DPRINTF(CXL_ctrl, "[%s] front buffer: addr=0x%x, seq=%d\n",__func__, temp_pkt->getAddr(), temp_pkt->cxl_pkt.seqNum);
            //         }
            //         if (cxl_buffer_host.size() == 0 ) {
            //             flag = true ;
            //         } else if (temp_pkt->cxl_pkt.seqNum <= seq_num) {
            //             cxl_buffer_host.popFront() ;
            //             DPRINTF(CXL_ctrl, "[%s] Removing from buffer: addr=0x%x, seq=%d, after pop, buffer_size=%d\n",
            //                     __func__, temp_pkt->getAddr(), temp_pkt->cxl_pkt.seqNum, cxl_buffer_host.size());
            //         } else {
            //             flag = true ;
            //             DPRINTF(CXL_ctrl, "[%s] Scheduling new timeout\n", __func__);
            //             this->schedule(timeoutEvent , curTick() + CXL_mem_retryTime) ;
            //         }
            //     }
            // }
            if(success){

                //esj 2025-05-07
                //esj 2025-05-25
                //esj 2025-07-12
                Addr curent_pkt_addr = packet->getAddr();
                packet->setAddr(packet->origin_addr);
                //
                // packet->origin_addr = curent_pkt_addr;
                ///


                //esj 2024-12-18
                // uint64_t seq_num = packet->cxl_pkt.ack_infomation.recv_Packet_num;
                //esj 2025-01-18
                for(auto it = error_resp_transmitList.begin(); it != error_resp_transmitList.end(); it++){
                    if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                        DPRINTF(CXL_ctrl,"%s, found error_resp_transmitList, erase it\n",__func__);
                        it = error_resp_transmitList.erase(it);
                        break;
                    }
                }

                recv_state.recv_packet_num++;
                DPRINTF(CXL_ctrl,"%s, Device send_packet_num = %d\n",__func__,recv_state.recv_packet_num);

                //esj 2024-12-25
                if(packet->cxl_pkt.retry_resp){
                    DPRINTF(CXL_ctrl,"%s, retry_buffer_req poped, seq_num = %d\n",__func__,seq_num);
                    //esj 2025-01-11
                    // retry_buffer_req.popByseqnum(seq_num);
                    retry_buffer_req.popByseqnum3(seq_num);
                    //esj 2024-12-31
                    // retry_buffer_req.print_buffer();
                }


                //esj 2024-12-17
                if(retry_buffer_req.size() == 0){
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;
                }
                else{
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                }
                DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,LRSM.cur_stat(),LRSM.NUM_RETRY);

                DPRINTF(CXL_ctrl, "[%s] retry_buffer_req size = %d\n", __func__, retry_buffer_req.size());


                if(cxl_buffer_host.empty()){
                    DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                }
                // else{
                //esj 2024-22
                // //esj 2024-12-18
                // else if(LRSM.cur_stat() == "RETRY_LOCAL_NORMAL" && packet->cxl_pkt.LRSM_device == "RETRY_LOCAL_NORMAL"){
                //     // popseq = cxl_buffer_host.popByseqnum(seq_num);
                //     // DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");
                //     // if(popseq){
                //     //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                //     //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                //     //     cancelSeqEvent(seq_num);
                //     // }
                //     // DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_host.size(), cxl_buffer_host.maximumSize);
                //     //esj 2024-12-09
                //     int num = seq_num;
                //     while (num) {
                //         //esj 2024-12-17
                //         if(cxl_buffer_host.popByseqnum2(num) != NULL){
                //             if(cxl_buffer_host.popByseqnum3(num)){
                //                 continue;
                //             }
                //         }
                //         popseq = cxl_buffer_host.popByseqnum(num);
                //         if(popseq){
                //             DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                //             DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                //             cancelSeqEvent(seq_num);
                //             DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                //             cxl_buffer_host.print_buffer();
                //         }
                //         num--;
                //     }
                //     DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_host.size(), cxl_buffer_host.maximumSize);
                //     ///////////////////////////////
                // }
                //esj 2024-12-17
                else{
                    //esj 2024-12-25
                    // esj 2025-05-16
                    // cxl_buffer_host.popByseqnum(seq_num);

                    //esj 2024-12-22
                    // if(popseq){
                    //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                    //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                    //     cancelSeqEvent(seq_num);
                    //     DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //     cxl_buffer_host.print_buffer();
                    // }
                    DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                    DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                    // esj 2025-05-16
                    // cancelSeqEvent(seq_num);

                    //esj 2024-12-22
                    DPRINTF(CXL_ctrl, "[%s] pop packet(ack) from buffer: req seq=%d\n",__func__,  packet->cxl_pkt.req_seqnum);

                    //esj 2025-05-16
                    // cxl_buffer_host.popByseqnum(packet->cxl_pkt.req_seqnum);
                    cxl_buffer_host.popByseqnum_double(seq_num, packet->cxl_pkt.req_seqnum);

                    // esj 2025-05-16
                    // cancelSeqEvent(packet->cxl_pkt.req_seqnum);
                    cancelSeqEvent_double(seq_num, packet->cxl_pkt.req_seqnum);

                    //esj 2024-12-30
                    // if(cxl_buffer_host.size() == 0){
                    //     // int num = packet->cxl_pkt.seqNum;
                    //     //esj 2024-12-23
                    //     int num = packet->cxl_pkt.req_seqnum;

                    //     while(num){
                    //         cancelSeqEvent(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();


                }
                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                //esj 2024-12-31
                // cxl_buffer_host.print_buffer2();

                //esj 2025-03-04
                if (retryReq_host) {
                    DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                    retryReq_host = false ;
                    upstreamResponse.sendRetryReq();
                }
            }
        }
        else{
            DPRINTF(CXL_ctrl,"=============== control flit response ===============\n");
            DPRINTF(CXL_ctrl, "[%s] Control flit response\n", __func__);
            DPRINTF(CXL_ctrl,"%s, retry_req = %s, retry_ack = %s\n",__func__,packet->cxl_pkt.retry_req? "true":"false", packet->cxl_pkt.retry_ack? "true":"false");

            //esj 2025-02-19
            // packet->cxl_pkt.complete = true;

            //esj 2024-12-18
            if(!packet->cxl_pkt.is_from_device){
                if(!packet->cxl_pkt.retry_req | (packet->cxl_pkt.retry_ack & packet->cxl_pkt.crc_check)){
                    uint64_t seq_num = packet->cxl_pkt.seqNum;

                    //esj 2024-12-10
                    if(packet->cxl_pkt.retry_ack){
                        DPRINTF(CXL_ctrl,"%s, retry_ack = true\n",__func__);
                        RRSM.Remote_state = CXL_Remote_state::RETRY_REMOTE_NORMAL;
                        DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                        //esj 2024-12-15
                        // if (retryReq_host) {
                        //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                        //     retryReq_host = false ;
                        //     upstreamResponse.sendRetryReq();
                        // }
                    }

                    if(cxl_buffer_host.empty()){
                        DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                    }
                    else{
                        popseq = cxl_buffer_host.popByseqnum(seq_num);

                        DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");

                        //esj 2025-03-06
                        if(popseq){
                            if (retryReq_host) {
                                DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                                retryReq_host = false ;
                                upstreamResponse.sendRetryReq();
                            }
                        }
                        ////

                        //esj 2024-12-22
                        // if(popseq){
                        //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                        //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        //     cancelSeqEvent(seq_num);
                        //     DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                        //     cxl_buffer_host.print_buffer();
                        // }
                        // //esj 2024-12-18
                        // else{
                        //     DPRINTF(CXL_ctrl,"%s, seq = %d not found in buffer\n",__func__,seq_num);
                        //     assert(popseq);
                        // }
                        DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                        DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        cancelSeqEvent(seq_num);
                        DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                        //esj 2024-12-31
                        // cxl_buffer_host.print_buffer2();

                        //esj 2024-12-25
                        // if(cxl_buffer_host.size() == 0){
                        // // int num = packet->cxl_pkt.seqNum;
                        // //esj 2024-12-23
                        //     int num = packet->cxl_pkt.req_seqnum;

                        //     while(num){
                        //         cancelSeqEvent(num);
                        //         num--;
                        //     }
                        //     DPRINTF(CXL_ctrl, "reset timeout\n");
                        // }

                        DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_host.size(), cxl_buffer_host.maximumSize);
                    }
                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    success = true;

                    //esj 2024-12-25
                    delete packet;
                }
                else{
                    //esj 2024-12-10
                    DPRINTF(CXL_ctrl,"%s, retry_req = %d\n",__func__,packet->cxl_pkt.retry_req);
                    DPRINTF(CXL_ctrl,"%s, crc_check = %d\n",__func__,packet->cxl_pkt.crc_check);
                    // LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                    DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                    DPRINTF(CXL_ctrl,"%s,cxl_buffer_host, packet->cxl_pkt.seqNum = %d\n",__func__,packet->cxl_pkt.seqNum);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    // PacketPtr pkt = cxl_buffer_host.popByseqnum2(packet->cxl_pkt.seqNum);
                    PacketPtr pkt = cxl_buffer_host.popByseqnum5(packet->cxl_pkt.seqNum);

                    //esj 2025-01-17
                    auto &error_req_list = errorReqListByHost(packetHostIdx(packet));
                    for(auto it = error_req_list.begin(); it != error_req_list.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                            it = error_req_list.erase(it);
                            break;
                        }
                    }
                    //

                    //esj 2025-01-06
                    if(pkt == NULL ){
                        delete packet;
                        return true;
                    }

                    // //esj 2024-12-22
                    // if(cxl_buffer_host.size() == 0){
                    //     int num = pkt->cxl_pkt.seqNum;
                    //     while(num){
                    //         cancelSeqEvent(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }

                    // Preserve the piggyback ACK carried by the first
                    // transmission. A replay must not acknowledge a newer
                    // response in place of the original one.

                    assert(pkt != NULL);

                    pkt->cxl_pkt.retry_req = true;

                    DPRINTF(CXL_ctrl,"================ received error packet, send request packet ===============\n");

                    //esj 2024-12-24
                    // if(pkt->cxl_pkt.retry_req & pkt->isResponse()){
                    //     returnPacketInfo(pkt);
                    // }

                    DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_req = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_req);
                    schedTimingReq(pkt, curTick());

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    success = true;

                    //esj 2025-01-10
                    delete packet;
                }
            }
            else{
                DPRINTF(CXL_ctrl,"%s, is_from_device = true\n",__func__);

                if(!packet->cxl_pkt.retry_resp | (packet->cxl_pkt.retry_ack & packet->cxl_pkt.crc_check)){
                    uint64_t seq_num = packet->cxl_pkt.seqNum;

                    //esj 2024-12-10
                    if(packet->cxl_pkt.retry_ack){
                        DPRINTF(CXL_ctrl,"%s, retry_ack = true\n",__func__);
                        RRSM.Remote_state = CXL_Remote_state::RETRY_REMOTE_NORMAL;
                        DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                        //esj 2024-12-15
                        // if (retryReq_host) {
                        //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                        //     retryReq_host = false ;
                        //     upstreamResponse.sendRetryReq();
                        // }
                    }

                    if(cxl_buffer_host.empty()){
                        DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                    }
                    else{
                        popseq = cxl_buffer_host.popByseqnum(seq_num);

                        DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");
                        //esj 2024-12-22
                        // if(popseq){
                        //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                        //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        //     cancelSeqEvent(seq_num);
                        //     DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                        //     cxl_buffer_host.print_buffer();
                        // }
                        // //esj 2024-12-18
                        // else{
                        //     DPRINTF(CXL_ctrl,"%s, seq = %d not found in buffer\n",__func__,seq_num);
                        //     assert(popseq);
                        // }
                        DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_host.size());
                        DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        cancelSeqEvent(seq_num);
                        DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                        //esj 2024-12-31
                        // cxl_buffer_host.print_buffer2();

                        //esj 2024-12-25
                        // if(cxl_buffer_host.size() == 0){
                        //     int num = packet->cxl_pkt.seqNum;
                        //     while(num){
                        //         cancelSeqEvent(num);
                        //         num--;
                        //     }
                        //     DPRINTF(CXL_ctrl, "reset timeout\n");
                        // }


                        //esj 2025-03-06
                        if(popseq){
                            if (retryReq_host) {
                                DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                                retryReq_host = false ;
                                upstreamResponse.sendRetryReq();
                            }
                        }
                        ////

                        DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_host.size(), cxl_buffer_host.maximumSize);
                    }
                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    success = true;

                    //esj 2024-12-25
                    delete packet;
                }
                else{
                    //esj 2024-12-10
                    DPRINTF(CXL_ctrl,"%s, retry_req = %d\n",__func__,packet->cxl_pkt.retry_req);
                    DPRINTF(CXL_ctrl,"%s, crc_check = %d\n",__func__,packet->cxl_pkt.crc_check);
                    // LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                    DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                    DPRINTF(CXL_ctrl,"%s,cxl_buffer_host, packet->cxl_pkt.seqNum = %d\n",__func__,packet->cxl_pkt.seqNum);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    // PacketPtr pkt = cxl_buffer_host.popByseqnum2(packet->cxl_pkt.seqNum);
                    PacketPtr pkt = cxl_buffer_host.popByseqnum5(packet->cxl_pkt.seqNum);

                    //esj 2025-01-17
                    auto &error_req_list = errorReqListByHost(packetHostIdx(packet));
                    for(auto it = error_req_list.begin(); it != error_req_list.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                            it = error_req_list.erase(it);
                            break;
                        }
                    }
                    //

                    //esj 2025-01-06
                    if(pkt == NULL ){
                        delete packet;
                        return true;
                    }

                    // //esj 2024-12-22
                    // if(cxl_buffer_host.size() == 0){
                    //     int num = pkt->cxl_pkt.seqNum;
                    //     while(num){
                    //         cancelSeqEvent(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }

                    // Preserve the piggyback ACK carried by the first
                    // transmission. A replay must not acknowledge a newer
                    // response in place of the original one.

                    //esj 2024-12-19
                    pkt->cxl_pkt.LRSM_host = LRSM.cur_stat();

                    assert(pkt != NULL);

                    pkt->cxl_pkt.retry_req = true;

                    DPRINTF(CXL_ctrl,"================ received error packet, send request packet ===============\n");

                    //esj 2024-12-24
                    // if(pkt->cxl_pkt.retry_req & pkt->isResponse()){
                    //     returnPacketInfo(pkt);
                    // }

                    DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_req = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_req);
                    schedTimingReq(pkt, curTick());

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                    //esj 2024-12-31
                    // cxl_buffer_host.print_buffer2();
                    success = true;

                    //esj 2025-01-10
                    delete packet;
                }
            }


        }

        DPRINTF(CXL_ctrl, "[%s] END: status=%s\n", __func__, success ? "success" : "failed");
        return success;
    }

    bool
    CXL_ctrl::recvrequest(PacketPtr packet){
        DPRINTF(CXL_ctrl, "[%s] START: addr=0x%x, size=%d\n", __func__, packet->getAddr(), packet->cxl_pkt.packing_size);

        bool success = false;
        if (!packet->cxl_pkt.is_controlflit) {
            DPRINTF(CXL_ctrl,"=============== response flit response(Host --> Device) ===============\n");
            DPRINTF(CXL_ctrl, "[%s] Processing response flit, send_packet num = %d\n", __func__, recv_state.recv_packet_num);

            //esj 2025-06-22
            success = this->upstreamResponse.sendTimingResp(packet);
            // success = this->upstreamResponse2.sendTimingResp(packet);
            //

            if(success){
                //esj 2024-12-18
                // uint64_t seq_num = packet->cxl_pkt.send_information.sen_packet_num;
                uint64_t seq_num = packet->cxl_pkt.seqNum;

                if(cxl_buffer_device.empty()){
                    DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                }
                //esj 2024-12-19
                // //esj 2024-12-18
                // else if(LRSM.cur_stat() == "RETRY_LOCAL_NORMAL" && packet->cxl_pkt.LRSM_host == "RETRY_LOCAL_NORMAL"){ //esj 2024-12-17

                //     //esj 2024-12-19
                //     // int num = seq_num;
                //     int num = packet->cxl_pkt.send_information.sen_packet_num;

                //     while (num) {
                //         //esj 2024-12-17
                //         if(cxl_buffer_device.popByseqnum2(num) != NULL){
                //             if(cxl_buffer_device.popByseqnum4(num)){
                //                 continue;
                //             }
                //         }

                //         popseq = cxl_buffer_device.popByseqnum(num);
                //         if(popseq){
                //             DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                //             DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                //             cancelSeqEvent(seq_num);
                //             DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                //             cxl_buffer_device.print_buffer();
                //         }
                //         num--;
                //     }
                //     DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_device.size(), cxl_buffer_device.maximumSize);
                //     ///////////////////////////////
                // }
                //esj 2024-12-17
                else{
                    popseq = cxl_buffer_device.popByseqnum(seq_num);
                    if (popseq)
                        releaseDeviceReplaySnapshot(seq_num);
                    //esj 2024-12-22
                    // if(popseq){
                    //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                    //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                    //     cancelSeqEvent(seq_num);
                    // }
                    DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                    DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                    cancelSeqEvent_device(seq_num);

                    //esj 2024-12-30
                    // if(cxl_buffer_device.size() == 0){
                    //     // int num = packet->cxl_pkt.seqNum;
                    //     //esj 2024-12-23
                    //     int num = packet->cxl_pkt.resp_seqnum;

                    //     while(num){
                    //         cancelSeqEvent_device(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }
                }
                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                //esj 2024-12-31
                //cxl_buffer_device.print_buffer2();();
            }
        }
        else{
            //esj 2025-02-19
            // packet->cxl_pkt.complete = true;

            DPRINTF(CXL_ctrl,"=============== control flit response(Host --> Device) ===============\n");
            DPRINTF(CXL_ctrl, "[%s] Control flit response\n", __func__);
            //esj 2024-12-18
            // DPRINTF(CXL_ctrl,"%s, retry_resp = %s, retry_ack = %s\n",__func__,packet->cxl_pkt.retry_resp? "true":"false", packet->cxl_pkt.retry_ack? "true":"false");
            DPRINTF(CXL_ctrl,"%s,retry_resp = %s, retry_req = %s, retry_ack = %s\n",__func__,packet->cxl_pkt.retry_resp? "true":"false", packet->cxl_pkt.retry_req? "true":"false", packet->cxl_pkt.retry_ack? "true":"false");

            //esj 2024-12-18
            // if(!packet->cxl_pkt.retry_resp | (packet->cxl_pkt.retry_ack & packet->cxl_pkt.crc_check)){
            if(!packet->cxl_pkt.is_from_device){
                if(!packet->cxl_pkt.retry_req | (packet->cxl_pkt.retry_ack & packet->cxl_pkt.crc_check)){
                    uint64_t seq_num = packet->cxl_pkt.seqNum;

                    if(packet->cxl_pkt.retry_ack){
                        DPRINTF(CXL_ctrl,"%s, retry_ack = true\n",__func__);
                        RRSM.Remote_state = CXL_Remote_state::RETRY_REMOTE_NORMAL;
                        DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                        //esj 2024-12-15
                        // if (retryResp_host) {
                        //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                        //     retryResp_host = false ;
                        //     upstreamResponse.sendRetryReq();
                        // }
                    }

                    if(cxl_buffer_device.empty()){
                        DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                    }
                    else{
                        //esj 2025-01-10
                        // popseq = cxl_buffer_device.popByseqnum(seq_num);
                        //esj 2025-01-11
                        //cxl_buffer_device.print_buffer2();();
                        popseq = cxl_buffer_device.popByseqnum3(seq_num);
                        if (popseq)
                            releaseDeviceReplaySnapshot(seq_num);
                        //

                        //esj 2025-03-02
                        if (retryResp_host) {
                            DPRINTF(CXL_ctrl,"%s, retryresp \n",__func__);
                            retryResp_host = false ;
                            //esj 2025-06-27
                            // upstreamRequest.sendRetryResp();
                            upstreamRequest2.sendRetryResp();
                        }

                        DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");
                        //esj 2024-12-22
                        // if(popseq){
                        //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                        //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        //     cancelSeqEvent(seq_num);
                        // }
                        DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                        DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        //esj 2025-01-10
                        // cancelSeqEvent_device(seq_num);
                        cancelSeqEvent_device2(seq_num);

                        //esj 2024-12-25
                        // if(cxl_buffer_device.size() == 0){
                        //     // int num = packet->cxl_pkt.seqNum;
                        //     //esj 2024-12-23
                        //     int num = packet->cxl_pkt.resp_seqnum;

                        //     while(num){
                        //         cancelSeqEvent_device(num);
                        //         num--;
                        //     }
                        //     DPRINTF(CXL_ctrl, "reset timeout\n");
                        // }


                        DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_device.size(), cxl_buffer_device.maximumSize);
                    }
                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    //esj 2024-12-31
                    //cxl_buffer_device.print_buffer2();();
                    success = true;

                    //esj 2024-12-25
                    delete packet;
                }
                else{
                    //esj 2024-12-10
                    // DPRINTF(CXL_ctrl,"%s, retry_resp = true\n",__func__);
                    DPRINTF(CXL_ctrl,"%s, retry_req = %s\n",__func__,packet->cxl_pkt.retry_req? "true":"false");
                    DPRINTF(CXL_ctrl,"%s, crc_check = %d\n",__func__,packet->cxl_pkt.crc_check);
                    // LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                    DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    //esj 2024-12-31
                    //cxl_buffer_device.print_buffer2();();
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum2(packet->cxl_pkt.seqNum);

                    //esj 2025-01-10
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum5(packet->cxl_pkt.seqNum);
                    //esj 2025-07-17
                    PacketPtr pkt = cloneDeviceReplayPacket(
                        packet->cxl_pkt.seqNum);
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum7(
                    //     packet->cxl_pkt.seqNum);


                    //esj 2025-01-25
                    //esj 2025-07-14
                    // pkt->cxl_pkt.retry_resp = true;

                    //esj 2025-01-17
                    for(auto it = error_resp_transmitList.begin(); it != error_resp_transmitList.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_resp_transmitList, erase it\n",__func__);
                            it = error_resp_transmitList.erase(it);
                            break;
                        }
                    }
                    //

                    //esj 2025-01-06
                    if(pkt == NULL){
                        DPRINTF(CXL_ctrl,"%s, pkt is NULL\n",__func__);
                        delete packet;
                        return true;
                    }
                    //esj 2025-07-17
                    else {
                        if (!pkt->isResponse()){
                            DPRINTF(CXL_ctrl,"%s, pkt is response = %s\n",__func__,pkt->isResponse()? "true":"false");
                            cxl_buffer_device.popByseqnum3(pkt->cxl_pkt.seqNum);
                            pkt = cloneDeviceReplayPacket(
                                packet->cxl_pkt.seqNum);
                            if (pkt == NULL){
                                DPRINTF(CXL_ctrl,"%s, pkt error\n",__func__);
                                delete packet;
                                return true;
                            }
                            else{
                                DPRINTF(CXL_ctrl,"%s, pkt addr= %0x, seqnum = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum);
                            }
                        }
                    }
                    //esj 2025-07-14
                    pkt->cxl_pkt.retry_resp = true;

                    //esj 2025-07-14
                    DPRINTF(CXL_ctrl,"%s, pkt is response = %s\n",__func__,pkt->isResponse()? "true":"false");

                    // //esj 2024-12-22
                    // if(cxl_buffer_device.size() == 0){
                    //     int num = pkt->cxl_pkt.seqNum;
                    //     while(num){
                    //         cancelSeqEvent_device(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }

                    //esj 2024-12-22
                    pkt->cxl_pkt.req_seqnum =
                        reqSeqnumByHost(packetHostIdx(pkt));

                    //esj 2024-12-18
                    // pkt->cxl_pkt.retry_resp = true;

                    // if(pkt->cxl_pkt.retry_resp & pkt->isResponse()){
                    //     returnPacketInfo(pkt);
                    // }

                    DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_resp,pkt->cxl_pkt.is_controlflit);
                    schedTimingResp(pkt, curTick());

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    //esj 2024-12-31
                    //cxl_buffer_device.print_buffer2();();
                    success = true;

                    //esj 2025-01-10
                    delete packet;
                }
            }
            else{
                DPRINTF(CXL_ctrl,"%s, is_from_device = true\n",__func__);
                if(!packet->cxl_pkt.retry_resp | (packet->cxl_pkt.retry_ack & packet->cxl_pkt.crc_check)){
                    uint64_t seq_num = packet->cxl_pkt.seqNum;

                    if(packet->cxl_pkt.retry_ack){
                        DPRINTF(CXL_ctrl,"%s, retry_ack = true\n",__func__);
                        RRSM.Remote_state = CXL_Remote_state::RETRY_REMOTE_NORMAL;
                        DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                        //esj 2024-12-15
                        // if (retryResp_host) {
                        //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                        //     retryResp_host = false ;
                        //     upstreamResponse.sendRetryReq();
                        // }
                    }

                    if(cxl_buffer_device.empty()){
                        DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                    }
                    else{
                        //esj 2025-01-10
                        // popseq = cxl_buffer_device.popByseqnum(seq_num);
                        //esj 2025-01-11
                        //cxl_buffer_device.print_buffer2();();
                        popseq = cxl_buffer_device.popByseqnum3(seq_num);
                        if (popseq)
                            releaseDeviceReplaySnapshot(seq_num);

                        //esj 2025-03-02
                        if (retryResp_host) {
                            DPRINTF(CXL_ctrl,"%s, retryresp \n",__func__);
                            retryResp_host = false ;
                            //esj 2025-06-27
                            // upstreamRequest.sendRetryResp();
                            upstreamRequest2.sendRetryResp();
                        }

                        DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");
                        //esj 2024-12-22
                        // if(popseq){
                        //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                        //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);
                        //     cancelSeqEvent(seq_num);
                        // }
                        DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, cxl_buffer_device.size());
                        DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, packet->getAddr(), packet->cxl_pkt.seqNum);

                        //esj 2025-01-10
                        // cancelSeqEvent_device(seq_num);
                        cancelSeqEvent_device2(seq_num);

                        //esj 2024-12-25
                        // if(cxl_buffer_device.size() == 0){
                        //     // int num = packet->cxl_pkt.seqNum;
                        //     //esj 2024-12-23
                        //     int num = packet->cxl_pkt.resp_seqnum;

                        //     while(num){
                        //         cancelSeqEvent_device(num);
                        //         num--;
                        //     }
                        //     DPRINTF(CXL_ctrl, "reset timeout\n");
                        // }

                        DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, cxl_buffer_device.size(), cxl_buffer_device.maximumSize);
                    }
                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    //esj 2024-12-31
                    //cxl_buffer_device.print_buffer2();();
                    success = true;

                    //esj 2024-12-25
                    delete packet;
                }
                else{
                    //esj 2024-12-10
                    // DPRINTF(CXL_ctrl,"%s, retry_resp = true\n",__func__);
                    DPRINTF(CXL_ctrl,"%s, retry_resp = %s\n",__func__,packet->cxl_pkt.retry_resp? "true":"false");
                    DPRINTF(CXL_ctrl,"%s, crc_check = %d\n",__func__,packet->cxl_pkt.crc_check);
                    // LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                    DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,RRSM.cur_stat());

                    DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    //esj 2024-12-31
                    //cxl_buffer_device.print_buffer2();();
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum2(packet->cxl_pkt.seqNum);

                    //esj 2025-01-10
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum5(packet->cxl_pkt.seqNum);
                    //esj 2025-07-17
                    PacketPtr pkt = cloneDeviceReplayPacket(
                        packet->cxl_pkt.seqNum);
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum7(
                    //     packet->cxl_pkt.seqNum);

                    //esj 2025-01-25
                    //esj 2025-07-14
                    // pkt->cxl_pkt.retry_resp = true;

                    //esj 2025-01-17
                    for(auto it = error_resp_transmitList.begin(); it != error_resp_transmitList.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_resp_transmitList, erase it\n",__func__);
                            it = error_resp_transmitList.erase(it);
                            break;
                        }
                    }
                    //

                    //esj 2025-01-06
                    if(pkt == NULL){
                        delete packet;
                        return true;
                    }
                    //esj 2025-07-17
                    else {
                        if (!pkt->isResponse()){
                            DPRINTF(CXL_ctrl,"%s, pkt is response = %s\n",__func__,pkt->isResponse()? "true":"false");
                            cxl_buffer_device.popByseqnum3(pkt->cxl_pkt.seqNum);
                            pkt = cloneDeviceReplayPacket(
                                packet->cxl_pkt.seqNum);
                            if (pkt == NULL){
                                DPRINTF(CXL_ctrl,"%s, pkt error\n",__func__);
                                delete packet;
                                return true;
                            }
                            else{
                                DPRINTF(CXL_ctrl,"%s, pkt addr= %0x, seqnum = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum);
                            }
                        }
                    }

                    //esj 2025-07-14
                    DPRINTF(CXL_ctrl,"%s, pkt is response = %s\n",__func__,pkt->isResponse()? "true":"false");

                    // //esj 2024-12-22
                    // if(cxl_buffer_device.size() == 0){
                    //     int num = pkt->cxl_pkt.seqNum;
                    //     while(num){
                    //         cancelSeqEvent_device(num);
                    //         num--;
                    //     }
                    //     DPRINTF(CXL_ctrl, "reset timeout\n");
                    // }

                    //esj 2024-12-22
                    pkt->cxl_pkt.req_seqnum =
                        reqSeqnumByHost(packetHostIdx(pkt));

                    //esj 2024-12-19
                    pkt->cxl_pkt.LRSM_device = LRSM.cur_stat();

                    assert(pkt != NULL);

                    //esj 2024-12-18
                    pkt->cxl_pkt.retry_resp = true;

                    // if(pkt->cxl_pkt.retry_resp & pkt->isResponse()){
                    //     returnPacketInfo(pkt);
                    // }

                    DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_resp,pkt->cxl_pkt.is_controlflit);
                    schedTimingResp(pkt, curTick());


                    // DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                    // cxl_buffer_device.print_buffer();
                    success = true;

                    //esj 2025-01-10
                    delete packet;
                }
            }


        }

        DPRINTF(CXL_ctrl, "[%s] END: status=%s\n", __func__, success ? "success" : "failed");
        return success;
    }


    bool
    CXL_ctrl::CXL_ctrlRequestPort::recvTimingResp(PacketPtr pkt)
    {

        DPRINTF(CXL_ctrl,"%s\n",__func__);
        std::string portname = name();
        // if(pkt->cxl_flag & pkt->is_cxl_mem){
            bool success = false;
            if(portname.find("upstreamRequest") != std::string::npos){
                //Hot -> CXL(TL -> PHY)
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s, Host controller:: dma \n",__func__);

                    //esj 2025-01-03
                    // add flexbus
                    if(!pkt->cxl_flag){
                        DPRINTF(CXL_ctrl,"%s, send response packet to flexbus, pcielink \n",__func__);
                        success = ctrl->cxl_flexbus->CXL_out_port.recvTimingResp(pkt);
                    }
                    else
                        //esj 2025-06-22
                        success = ctrl->downstreamResponse.sendTimingResp(pkt);
                        // success = ctrl->downstreamResponse2.sendTimingResp(pkt);
                }
                else{

                    //esj 2025-01-03
                    // add flexbus
                    if(!pkt->cxl_flag){
                        DPRINTF(CXL_ctrl,"%s, send response packet to flexbus, pcielink \n",__func__);
                        return ctrl->cxl_flexbus->CXL_out_port.recvTimingResp(pkt);
                    }

                    DPRINTF(CXL_ctrl,"[%s],Device controller:: packing packet(response) : device -> host \n",__func__);
                    pkt->cxl_pkt.ack_infomation.recv_Packet_num = ctrl->recv_state.recv_packet_num - 1;

                    //esj 2024-12-18
                    pkt->cxl_pkt.retry_req = false;
                    pkt->cxl_pkt.is_from_device = true;

                    pkt->cxl_pkt.retry_resp = false;

                    DPRINTF(CXL_ctrl,"=============== send response flit ===============\n");
                    DPRINTF(CXL_ctrl,"%s,packet addr = %0x\n", __func__, pkt->getAddr());

                    //esj 2024-12-29
                    // if(ctrl->control_flit_respEvent.scheduled()){
                    //     DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_respEvent\n",__func__);
                    //     //esj 2024-12-15
                    //     // ctrl->deschedule(ctrl->control_flit_respEvent);
                    //     // DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                    //     bool pop_success = ctrl->popfromtransmitlist_resp(pkt->cxl_pkt.seqNum);
                    //     ctrl->print_retry_transmitList_resp();
                    //     //esj 2024-12-18
                    //     if(pop_success){
                    //         DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                    //         if(ctrl->getsize_retry_transmitList_resp() == 0){
                    //             DPRINTF(CXL_ctrl,"%s, deschedule control_flit_respEvent, transmitList_resp size = %d\n",__func__,ctrl->getsize_retry_transmitList_resp());
                    //             ctrl->deschedule(ctrl->control_flit_respEvent);
                    //             DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                    //         }
                    //     }
                    // }

                    //esj 2025-01-01
                    if(ctrl->control_flit_respEvent.scheduled()){
                        DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_respEvent\n",__func__);
                        // A backend response completes work for exactly one
                        // source host.  Releasing every host's pending retry
                        // here can discard a CRC-rejected request before its
                        // replay is sent when different hosts use the same
                        // device-local address.
                        const uint8_t host_idx = ctrl->packetHostIdx(pkt);
                        const uint64_t req_seq =
                            ctrl->reqSeqnumByHost(host_idx);
                        bool pop_success =
                            ctrl->popfromtransmitlist_resp(req_seq);
                        if (pop_success){
                            DPRINTF(CXL_ctrl,
                                    "%s, POP from retry_transmitList_resp "
                                    "host=%d seq = %d\n",
                                    __func__,
                                    host_idx,
                                    req_seq);
                        }

                        //esj 2025-01-18
                        bool pop_success2 = ctrl->popfromtransmitlist_resp(pkt->cxl_pkt.seqNum);
                        if(pop_success2){
                            DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                        }
                    }

                    ctrl->response_busy = true;

                    //esj 2024-12-12
                    // success = ctrl->cxl_packing->out_port.recvTimingResp(pkt);
                    // DPRINTF(CXL_ctrl, "[%s] send response packet to host =%s\n", __func__, success ? "success" : "failed");
                    //esj 2025-06-27
                    // success = ctrl->TL2PHY_interface(pkt);
                    success = ctrl->TL2PHY_interface2(pkt);
                    DPRINTF(CXL_ctrl, "[%s] send response packet to host =%s\n", __func__, success ? "success" : "failed");


                }
                return success;
            }
            else if (portname.find("downstreamRequest") != std::string::npos){
                //CXL -> Host(PHY -> TL)
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s,Host controller:: packing packet(response) : device -> host \n",__func__);
                    success = ctrl->cxl_decoder->out_port.recvTimingResp(pkt);

                }
                else{
                    DPRINTF(CXL_ctrl,"%s, Device controller:: dma \n",__func__);

                    //esj 2025-06-22
                    success = ctrl->upstreamResponse.sendTimingResp(pkt);
                    // success = ctrl->upstreamResponse2.sendTimingResp(pkt);
                }

                return success;
            }
            else if(portname.find("internal_Resquest") != std::string::npos){
                //PHY -> CXL(PHY -> out)
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s, unpacing packet to host for response\n",__func__);
                    // success = ctrl->recvresponse(pkt);

                    if(pkt->cxl_pkt.is_controlflit){
                        //esj 2025-02-19
                        // if(pkt->cxl_pkt.complete){
                        //     delete pkt;
                        //     warn("control flit is already completed\n");
                        //     return true;
                        // }

                        DPRINTF(CXL_ctrl,"%s, control flit, sending packet to host\n",__func__);
                        success = ctrl->recvresponse(pkt);

                        if(!success){
                            DPRINTF(CXL_ctrl,"failed sending control flit\n");

                            //esj 2025-05-16
                            pkt->headerDelay = 0;
                            pkt->payloadDelay = 0;

                            //esj 2025-02-19
                            // delete pkt;

                            return success;
                        }
                    }
                    else{
                        //esj 2024-12-15
                        // ctrl->control_flit_resp = ctrl->make_control_flit_req(pkt);

                        //esj 2024-12-18
                        PacketPtr pkt_temp = ctrl->make_control_flit_req(pkt);
                        bool pkt_temp_queued = false;
                        DPRINTF(CXL_ctrl,"%s, pkt_temp addr = %0x, seq = %d, crc_check = %d, is_controlflit = %d, retry_resp = %d, retry_req = %d, is_request = %s\n",
                        __func__,pkt_temp->getAddr(),pkt_temp->cxl_pkt.seqNum,pkt_temp->cxl_pkt.crc_check,pkt_temp->cxl_pkt.is_controlflit,pkt_temp->cxl_pkt.retry_resp,pkt_temp->cxl_pkt.retry_req,pkt_temp->isRequest()? "true":"false");

                        DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, crc_check = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.crc_check);
                        if(pkt->cxl_pkt.crc_check){
                            DPRINTF(CXL_ctrl,"%s, CRC check success, sending packet to device\n",__func__);

                            //esj 2024-12-18
                            // PacketPtr pkt_temp = ctrl->make_control_flit_req(pkt);

                            //esj 2025-05-13
                            // success = ctrl->recvresponse(pkt);

                            //esj 2025-07-12
                            // if(!ctrl->is_host_retry || pkt->cxl_pkt.is_retry_resp){
                            //     success = ctrl->recvresponse(pkt);
                            // }
                            // else{
                            //     success = false;
                            // }

                            success = ctrl->recvresponse(pkt);

                            //esj 2024-12-26
                            DPRINTF(CXL_ctrl, "======================[ recv_packet_num = %d ]======================\n",ctrl->recv_packet_num);
                            if(!success){
                                DPRINTF(CXL_ctrl,"failed sending responsed packet\n");
                                //esj 2025-03-03
                                // pkt->cxl_pkt.crc_check = false;
                                //esj 2025-05-13
                                // pkt_temp->cxl_pkt.crc_check = false;
                                // ctrl->retry_transmit_req(pkt_temp, curTick()+ctrl->clockPeriod());

                                //esj 2025-07-12
                                // //esj 2025-05-16
                                // pkt->headerDelay = 0;
                                // pkt->payloadDelay = 0;

                                // //esj 2025-05-13
                                // ctrl->is_host_retry = true;
                                // //esj 2025-06-27
                                // // pkt->cxl_pkt.is_retry_resp = true;
                                // // ctrl->transmitList_host.emplace_back(pkt, curTick());
                                // if(!pkt->cxl_pkt.is_retry_resp){
                                //     pkt->cxl_pkt.is_retry_resp = true;
                                //     ctrl->transmitList_host.emplace_back(pkt, curTick());
                                // }

                                delete pkt_temp;

                                return success;
                            }



                            if(pkt->cxl_pkt.retry_resp){
                                // ctrl->control_flit_resp = pkt;
                                ctrl->popfromtransmitlist_req(pkt->cxl_pkt.seqNum);
                                if(ctrl->retry_buffer_req.size() == 0){
                                    ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;

                                    //esj 2024-12-15
                                    //esj 2025-03-06
                                    // if (ctrl->retryReq_host) {
                                    //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                                    //     ctrl->retryReq_host = false ;
                                    //     ctrl->upstreamResponse.sendRetryReq();
                                    // }
                                }
                                else{
                                    ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                                }
                                ctrl->LRSM.NUM_RETRY = ctrl->retry_buffer_req.size();
                                DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,ctrl->LRSM.cur_stat(),ctrl->LRSM.NUM_RETRY);
                                //esj 2024-12-15
                                // ctrl->schedule(ctrl->control_flit_reqEvent, curTick()+ctrl->clockPeriod());
                                //esj 2024-12-18
                                // ctrl->retry_transmit_req(ctrl->control_flit_resp, curTick()+ctrl->clockPeriod());

                                //esj 2025-01-17
                                // ctrl->retry_transmit_req(pkt_temp, curTick()+ctrl->clockPeriod());
                                ctrl->retry_transmit_req(
                                    pkt_temp,
                                    curTick() + ctrl->control_flit_cycle * ctrl->clockPeriod());
                                pkt_temp_queued = true;

                            }

                            //esj 2025-01-17
                            if(pkt->cxl_pkt.crc_error_req_seqnum != -1){
                                DPRINTF(CXL_ctrl, "======================[ retry crc error resp packet ]======================\n");
                                DPRINTF(CXL_ctrl,"%s, crc_error_req_seqnum = %llu\n",__func__,pkt->cxl_pkt.crc_error_req_seqnum);

                                ctrl->RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                                DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,ctrl->RRSM.cur_stat());
                                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);

                                // ctrl->cxl_buffer_host.print_buffer2();
                                //esj 2025-01-18 pkt->packet
                                PacketPtr packet = ctrl->cxl_buffer_host.popByseqnum6(pkt->cxl_pkt.crc_error_req_seqnum);

                                //esj 2025-01-23
                                // if(packet != NULL && packet->isRequest()){
                                if(packet != NULL ){
                                    if(packet->isRequest()){
                                        DPRINTF(CXL_ctrl,"%s, crc_error_req_seqnum = %llu, not found in buffer, already sent packet\n",__func__,packet->cxl_pkt.crc_error_req_seqnum);
                                        DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.seqNum,packet->cxl_pkt.retry_resp,packet->cxl_pkt.is_controlflit);

                                        // The replay entry already holds the
                                        // piggyback ACK from the first
                                        // attempt. Do not replace it with a
                                        // newer response sequence.
                                        packet->cxl_pkt.LRSM_host =
                                            ctrl->LRSM.cur_stat();
                                        packet->cxl_pkt.retry_req = true;

                                        //esj 2025-01-18
                                        // ctrl->check_retry_req_packet(packet);

                                        ctrl->schedTimingReq(packet, curTick());

                                        DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                                    }

                                }
                                else{
                                    DPRINTF(CXL_ctrl,"%s, crc_error_resp_seqnum = %d, not found in buffer, already sent packet\n",__func__,pkt->cxl_pkt.crc_error_resp_seqnum);
                                }

                            }
                            //
                        }
                        else{
                            DPRINTF(CXL_ctrl,"%s, CRC check failed, sending packet to device\n",__func__);

                            //esj 2025-12-29
                            ctrl->stats.crc_error_count++;


                            ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                            ctrl->LRSM.NUM_RETRY = ctrl->retry_buffer_req.size();
                            DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,ctrl->LRSM.cur_stat(),ctrl->LRSM.NUM_RETRY + 1);
                            if(ctrl->control_flit_reqEvent.scheduled()){
                                DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_reqEvent\n",__func__);
                                //esj 2024-12-15
                                // ctrl->deschedule(ctrl->control_flit_reqEvent);
                                bool pop_success = ctrl->popfromtransmitlist_req(pkt->cxl_pkt.seqNum);
                                if(pop_success){
                                    DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                                }
                                //esj 2024-12-31
                                // ctrl->print_retry_transmitList_req();
                            }
                            // ctrl->control_flit_resp = pkt;
                            //esj 2024-12-15
                            // ctrl->retry_transmit_req(ctrl->control_flit_resp, curTick()+ctrl->clockPeriod());
                            // ctrl->schedule(ctrl->control_flit_reqEvent, curTick()+ctrl->clockPeriod());

                            //esj 2024-12-18
                            // ctrl->retry_transmit_req(ctrl->control_flit_resp, curTick()+ctrl->clockPeriod());
                            //esj 2024-12-19
                            // ctrl->retry_transmit_req(pkt, curTick()+ctrl->clockPeriod());

                            //esj 2025-01-17
                            // ctrl->retry_transmit_req(pkt_temp, curTick()+ctrl->clockPeriod());
                            ctrl->retry_transmit_req(
                                pkt_temp,
                                curTick() + ctrl->crc_error_control_flit_cycle * ctrl->clockPeriod());
                            pkt_temp_queued = true;

                            //esj 2025-01-18
                            // ctrl->check_retry_req_packet(pkt);

                            //

                            //esj 2025-01-18
                            // pkt->cxl_pkt.crc_error_req_seqnum = -1;
                            // pkt->cxl_pkt.crc_error_resp_seqnum = -1;

                            //esj 2025-01-17
                            //esj 2025-01-23
                            ctrl->check_retry_resp_packet(pkt);
                            ctrl->error_resp_transmitList.emplace_back(pkt->cxl_pkt.seqNum);


                            return true;
                        }

                        if(success){
                            //esj 2024-12-17
                            // ctrl->recv_state.recv_packet_num++;

                            // ctrl->control_flit_resp = pkt;
                            DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, crc_check = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.crc_check);

                            //esj 2024-12-17
                            // esj 2025-05-20
                            // if(!pkt->cxl_pkt.retry_resp){
                            if(!pkt->cxl_pkt.retry_resp && !pkt->is_cxl_write_resp){
                                //esj 2024-12-18
                                // ctrl->retry_transmit_req(ctrl->control_flit_resp, curTick()+ 10*ctrl->clockPeriod());
                                // ctrl->retry_transmit_req(pkt, curTick()+ 10*ctrl->clockPeriod());
                                //esj 2025-01-02
                                // ctrl->retry_transmit_req(pkt_temp, curTick()+ 10*ctrl->clockPeriod());
                                ctrl->retry_transmit_req(
                                    pkt_temp,
                                    curTick() + ctrl->control_flit_cycle * ctrl->clockPeriod());
                                pkt_temp_queued = true;
                            }
                            //esj 2025-02-19
                            //esj 2025-07-13
                            //else{
                            else if(pkt->is_cxl_write_resp && !pkt_temp_queued){
                                // delete pkt_temp;
                                // esj 2025-05-20
                                delete pkt_temp;
                            }

                            //esj 2024-12-25
                            ctrl->recv_packet_num--;
                            DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",ctrl->recv_packet_num);

                            //esj 2025-01-29
                            //esj 2025-02-26
                            // if(pkt->isRead() && pkt->isResponse()){
                            //esj 2025-06-30
                            // if(pkt->cmd == MemCmd::ReadResp){
                            //     if(ctrl->reserved_read_queue>0)
                            //         ctrl->reserved_read_queue--;
                            //     DPRINTF(CXL_ctrl, "reserved_read_queue num = %d\n",ctrl->reserved_read_queue);
                            // }

                            //esj 2025-03-06
                            if (ctrl->retryReq_host) {
                                DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                                ctrl->retryReq_host = false ;
                                ctrl->upstreamResponse.sendRetryReq();
                            }


                            // if(!ctrl->control_flit_reqEvent.scheduled()){
                            //     // ctrl->schedule(ctrl->control_flit_reqEvent, curTick()+ 10*ctrl->clockPeriod());
                            //     ctrl->retry_transmit_req(ctrl->control_flit_resp, curTick()+ 10*ctrl->clockPeriod());
                            // }
                            // else{
                            //     DPRINTF(CXL_ctrl,"%s, control_flit_reqEvent is already scheduled\n",__func__);
                            // }

                            // //esj 2024-12-26
                            // delete pkt;

                            //esj 2025-01-24
                            pkt->cxl_pkt.complete = true;
                            //

                            DPRINTF(CXL_ctrl,"%s, receive packet success, recv_packet_num=%d\n",__func__, ctrl->recv_state.recv_packet_num);
                        }
                    }
                }
                //esj 2025-01-03
                else if(portname.find("pcie_Resquest") != std::string::npos){
                    if(ctrl->is_host){
                        DPRINTF(CXL_ctrl,"%s,send response packet to host upstreamRequest port( flexbus --> Host )\n",__func__);
                        success = ctrl->upstreamResponse.sendTimingResp(pkt);
                    }
                    else{
                        DPRINTF(CXL_ctrl,"%s,send response packet to Device upstreamRequest port( flexbus --> Device )\n",__func__);
                        success = ctrl->upstreamResponse.sendTimingResp(pkt);
                    }

                }
                else{
                    DPRINTF(CXL_ctrl,"%s, encoding packet to host for response\n",__func__);
                    // esj 2025-05-19
                    //esj 2025-06-22
                    // success = ctrl->downstreamResponse.sendTimingResp(pkt);
                    success = ctrl->downstreamResponse2.sendTimingResp(pkt);

                    // esj 2025-05-19
                    // Port& peer = ctrl->downstreamResponse.getPeer();
                    // auto* req_port = dynamic_cast<gem5::RequestPort*>(&peer);
                    // if (req_port) {
                    //     success = req_port->recvTimingResp(pkt);
                    // }
                    //



                    //esj 2024-12-16
                    if(success){
                        //esj 2025-02-26
                        // if(!pkt->cxl_pkt.is_controlflit){
                        //     if(ctrl->recv_packet_num>0)
                        //         ctrl->recv_packet_num--;
                        //     if(pkt->cmd == MemCmd::ReadResp){
                        //         if(ctrl->reserved_read_queue>0)
                        //             ctrl->reserved_read_queue--;
                        //         DPRINTF(CXL_ctrl, "reserved_read_queue num = %d\n",ctrl->reserved_read_queue);
                        //     }
                        // }
                        //
                        //esj 2025-03-02
                        if (ctrl->retryResp_host) {
                            DPRINTF(CXL_ctrl,"%s, retryresp \n",__func__);
                            ctrl->retryResp_host = false ;
                            //esj 2025-06-27
                            // ctrl->upstreamRequest.sendRetryResp();
                            ctrl->upstreamRequest2.sendRetryResp();
                        }
                        ctrl->response_busy = false;
                    }
                    //esj 2025-03-03
                    else{
                        //esj 2025-05-21
                        // pkt->cxl_pkt.retry_resp_from_device = true;
                        //
                        DPRINTF(CXL_ctrl,"%s, retryresp device -> host \n",__func__);
                        //esj 2025-06-27
                        // ctrl->upstreamRequest.recvTimingResp(pkt);
                        ctrl->upstreamRequest2.recvTimingResp(pkt);

                    }
                }

                return success;
            }

    }

    bool
    CXL_ctrl::lrsmRequestControlReady() const
    {
        if (LRSM.Local_state != CXL_Local_state::RETRY_LLRREQ ||
            retry_transmitList_req.empty()) {
            return false;
        }

        const DeferredPacket &pending = retry_transmitList_req.front();
        return pending.valid && pending.pkt != nullptr &&
            pending.tick <= curTick();
    }

    bool
    CXL_ctrl::lrsmResponseControlReady() const
    {
        if (LRSM.Local_state != CXL_Local_state::RETRY_LLRREQ ||
            retry_transmitList_resp.empty()) {
            return false;
        }

        const DeferredPacket &pending = retry_transmitList_resp.front();
        return pending.valid && pending.pkt != nullptr &&
            pending.tick <= curTick();
    }

    size_t
    CXL_ctrl::selectRequestTxIndex() const
    {
        if (RRSM.Remote_state != CXL_Remote_state::RETRY_LLRACK)
            return 0;

        for (size_t index = 0; index < transmitList.size(); ++index) {
            const DeferredPacket &candidate = transmitList[index];
            if (candidate.pkt != nullptr &&
                candidate.pkt->cxl_pkt.retry_req &&
                candidate.tick <= curTick()) {
                return index;
            }
        }

        // Do not idle the link while the retry packet is not ready yet.
        return 0;
    }

    size_t
    CXL_ctrl::selectResponseTxIndex() const
    {
        if (RRSM.Remote_state != CXL_Remote_state::RETRY_LLRACK)
            return 0;

        for (size_t index = 0; index < transmitList_resp.size(); ++index) {
            const DeferredPacket &candidate = transmitList_resp[index];
            if (candidate.pkt != nullptr &&
                candidate.pkt->cxl_pkt.retry_resp &&
                candidate.tick <= curTick()) {
                return index;
            }
        }

        return 0;
    }

    Tick
    CXL_ctrl::nextRequestTxWakeup() const
    {
        assert(!transmitList.empty());

        Tick wakeup = transmitList.front().tick + queuing_delay;
        if (RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            for (const DeferredPacket &candidate : transmitList) {
                if (candidate.pkt != nullptr &&
                    candidate.pkt->cxl_pkt.retry_req) {
                    wakeup = std::min(wakeup, candidate.tick);
                }
            }
        }
        return wakeup;
    }

    Tick
    CXL_ctrl::nextResponseTxWakeup() const
    {
        assert(!transmitList_resp.empty());

        Tick wakeup = transmitList_resp.front().tick + queuing_delay;
        if (RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            for (const DeferredPacket &candidate : transmitList_resp) {
                if (candidate.pkt != nullptr &&
                    candidate.pkt->cxl_pkt.retry_resp) {
                    wakeup = std::min(wakeup, candidate.tick);
                }
            }
        }
        return wakeup;
    }

    void
    CXL_ctrl::schedTimingReq(PacketPtr pkt, Tick when)
    {
        // If we're about to put this packet at the head of the queue, we
        // need to schedule an event to do the transmit.  Otherwise there
        // should already be an event scheduled for sending the head
        // packet.
        if(!pkt->cxl_pkt.retry_req){
            // if (transmitList.empty()) {
            //     DPRINTF(CXL_ctrl, "[%s] scheduling sendEvent, when = %d, transmitList size %d\n",__func__,when, transmitList.size());
            //     schedule(sendEvent, when);
            // }
            DPRINTF(CXL_ctrl, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList.size());

            // esj 2025-05-16
            //esj 2025-12-29
            // assert(transmitList.size() != maxQueueSize);
            // assert((transmitList.size() + transmitList_write.size()) != maxQueueSize);

            //esj 2024-12-05
            // transmitList.emplace_back(pkt, when);

            if(!retransmit){
                // esj 2025-05-16
                transmitList.emplace_back(pkt, when);
                // if(pkt->isWrite()){
                //     transmitList_write.emplace_back(pkt, when);
                // }
                // else{
                //     transmitList.emplace_back(pkt, when);
                // }
            }
            else{
                DPRINTF(CXL_ctrl,
                        "[%s] retransmit, sendEvent is not scheduled, "
                        "scheduling sendEvent, when = %d\n",
                        __func__, when);
                // if (!this->sendEvent.scheduled()) {
                //     this->schedule(sendEvent, when);
                // }
            }
        }
        else{
            DPRINTF(CXL_ctrl,
                    "[%s] retry_req = true, enqueue for RRSM arbitration, "
                    "scheduling sendEvent, when = %d\n",
                    __func__, when);
            // Preserve retry arrival order. RRSM arbitration selects the
            // oldest ready retry ahead of normal traffic.
            transmitList.emplace_back(pkt, when);
            // if (pkt->isWrite()){
            //     transmitList_write.emplace_front(pkt, when);
            // }
            // else{
            //     transmitList.emplace_front(pkt, when);
            // }


            // if(!this->sendEvent.scheduled()){
            //     this->schedule(sendEvent, when);
            // }
        }
        const Tick wakeup = std::max(when, curTick());
        if (!this->sendEvent.scheduled()) {
            this->schedule(sendEvent, wakeup);
        }
        else if (pkt->cxl_pkt.retry_req &&
                 RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK &&
                 wakeup < sendEvent.when()) {
            this->reschedule(sendEvent, wakeup, true);
        }
        sampleEndpointStats();

    }

    void
    CXL_ctrl::trySendTiming()
    {

        assert(!transmitList.empty());

        if (lrsmRequestControlReady()) {
            PacketPtr waiting_pkt = transmitList.front().pkt;
            DPRINTF(CXL_ctrl,
                    "[LRSM] H2D control priority host=%u waiting_seq=%llu\n",
                    static_cast<unsigned>(packetHostIdx(waiting_pkt)),
                    static_cast<unsigned long long>(
                        waiting_pkt->cxl_pkt.seqNum));

            if (!control_flit_reqEvent.scheduled()) {
                schedule(control_flit_reqEvent, curTick());
            }
            else if (control_flit_reqEvent.when() > curTick()) {
                reschedule(control_flit_reqEvent, curTick(), true);
            }
        }

        const size_t selected_index = selectRequestTxIndex();
        DeferredPacket req = transmitList[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (pkt->cxl_pkt.retry_req &&
            RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            DPRINTF(CXL_ctrl,
                    "[RRSM] H2D retry priority host=%u seq=%llu index=%llu\n",
                    static_cast<unsigned>(packetHostIdx(pkt)),
                    static_cast<unsigned long long>(pkt->cxl_pkt.seqNum),
                    static_cast<unsigned long long>(selected_index));
        }

        DPRINTF(CXL_ctrl, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList.size());

        if (cxl_packing->in_port.recvTimingReq(pkt)) {
            // send successful
            transmitList.erase(transmitList.begin() + selected_index);
            if (!pkt->cxl_pkt.is_controlflit) {
                if (pkt->isWrite() || pkt->cmd == MemCmd::WriteResp ||
                    pkt->is_cxl_write_resp) {
                    stats.data_flit_req_write += packetFlitCount(pkt);
                } else {
                    stats.data_flit_req_read += packetFlitCount(pkt);
                }
            }

            Host2Device_busy = false;
            // trans_state.send_packet_num++;

            //esj 2024-12-19
            // if(!pkt->cxl_pkt.retry_req){

            //esj 2024-12-25
            if(!pkt->cxl_pkt.retry_req && !pkt->cxl_pkt.is_controlflit && !cxl_buffer_host.findByseqnum(pkt->cxl_pkt.seqNum) && !pkt->cxl_pkt.retry_resp){
                DPRINTF(CXL_ctrl,"%s, retry_req = false, pushing packet to host buffer, addr=%0x\n",__func__, pkt->getAddr());
                cxl_buffer_host.pushBack(pkt);
                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);
                //esj 2024-12-31
                // cxl_buffer_host.print_buffer2();

                DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_host.size());
            }
            else{
                DPRINTF(CXL_ctrl,"%s, retry_req = true, packet is already in buffer, addr=%0x\n",__func__, pkt->getAddr());
                DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_host.size());
                DPRINTF(CXL_ctrl,"%s, cancelSeqEvent, seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                cancelSeqEvent(pkt->cxl_pkt.seqNum);
            }

            sampleEndpointStats();
            replayPacket = NULL;
            // if (!timeoutEvent.scheduled()) {
            //     DPRINTF(CXL_ctrl,"%s, timeout start, end = %d \n",__func__,curTick() + CXL_mem_retryTime);
            //     schedule(timeoutEvent , curTick() + CXL_mem_retryTime) ;
            // }
            createSeqEvent(pkt->cxl_pkt.seqNum);

            //esj 2024-12-10
            //esj 2025-03-02
            // if (retryReq_host) {
            //     DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
            //     retryReq_host = false ;
            //     upstreamResponse.sendRetryReq();
            // }
            //

            if (!transmitList.empty()) {
                DPRINTF(CXL_ctrl, "Scheduling next send\n");
                //esj 2025-02-19
                // schedule(sendEvent, std::max(next_req.tick,curTick()+this->clockPeriod()));
                schedule(sendEvent,
                    std::max(nextRequestTxWakeup(),
                             curTick()+this->clockPeriod()));
            }

        }
        else{
            // if (!transmitList.empty()) {
            //     DeferredPacket next_req = transmitList.front();
            //     DPRINTF(CXL_ctrl, "Scheduling next send\n");
            //     schedule(sendEvent, std::max(next_req.tick,clockEdge()));
            // }
            // else{

            // }
            DPRINTF(CXL_ctrl, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEvent, curTick()+this->clockPeriod());
            sampleEndpointStats();
        }

        // esj 2025-05-16
        // PacketPtr pkt;

        // DPRINTF(CXL_ctrl, "trySendTiming, read_turn = %s\n",read_turn? "true":"false");

        // if(read_turn){
        //     if(!transmitList.empty()){
        //         DeferredPacket req_read = transmitList.front();
        //         pkt = req_read.pkt;
        //     }
        //     else{
        //         DeferredPacket req_write = transmitList_write.front();
        //         pkt = req_write.pkt;
        //         read_turn = false;
        //     }
        // }
        // else{
        //     if(!transmitList_write.empty()){
        //         DeferredPacket req_write = transmitList_write.front();
        //         pkt = req_write.pkt;
        //     }
        //     else{
        //         DeferredPacket req_read = transmitList.front();
        //         pkt = req_read.pkt;
        //         read_turn = true;
        //     }
        // }


        // DPRINTF(CXL_ctrl, "trySend request addr 0x%x, queue size %d\n",
        //         pkt->getAddr(), transmitList.size());

        // if (cxl_packing->in_port.recvTimingReq(pkt)) {
        //     // send successful
        //     if(read_turn){
        //         transmitList.pop_front();
        //     }
        //     else{
        //         transmitList_write.pop_front();
        //     }

        //     Host2Device_busy = false;

        //     if(!pkt->cxl_pkt.retry_req && !pkt->cxl_pkt.is_controlflit && !cxl_buffer_host.findByseqnum(pkt->cxl_pkt.seqNum) && !pkt->cxl_pkt.retry_resp){
        //         DPRINTF(CXL_ctrl,"%s, retry_req = false, pushing packet to host buffer, addr=%0x\n",__func__, pkt->getAddr());
        //         cxl_buffer_host.pushBack(pkt);
        //         DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_host\n",__func__);

        //         DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_host.size());
        //     }
        //     else{
        //         DPRINTF(CXL_ctrl,"%s, retry_req = true, packet is already in buffer, addr=%0x\n",__func__, pkt->getAddr());
        //         DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_host.size());
        //         DPRINTF(CXL_ctrl,"%s, cancelSeqEvent, seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
        //         cancelSeqEvent(pkt->cxl_pkt.seqNum);
        //     }

        //     replayPacket = NULL;

        //     createSeqEvent(pkt->cxl_pkt.seqNum);

        //     if(read_turn){
        //         if (!transmitList_write.empty()) {
        //             read_turn = false;
        //             DeferredPacket next_req = transmitList_write.front();
        //             DPRINTF(CXL_ctrl, "Scheduling read -> write transmitList_write next send\n");

        //             schedule(sendEvent, curTick()+this->clockPeriod()%2);
        //         }
        //         else{
        //             if(!transmitList.empty()){
        //                 DeferredPacket next_req = transmitList.front();
        //                 DPRINTF(CXL_ctrl, "Scheduling read -> read transmitList next send\n");

        //                 schedule(sendEvent, std::max(next_req.tick+queuing_delay,curTick()+this->clockPeriod()));
        //             }
        //         }
        //     }
        //     else{
        //         if (!transmitList.empty()) {
        //             read_turn = true;
        //             DeferredPacket next_req = transmitList.front();
        //             DPRINTF(CXL_ctrl, "Scheduling write -> read transmitList next send\n");

        //             schedule(sendEvent, curTick()+this->clockPeriod()%2);
        //         }
        //         else{
        //             if(!transmitList_write.empty()){
        //                 DeferredPacket next_req = transmitList_write.front();
        //                 DPRINTF(CXL_ctrl, "Scheduling write -> write transmitList_write next send\n");

        //                 schedule(sendEvent, std::max(next_req.tick+queuing_delay,curTick()+this->clockPeriod()));
        //             }
        //         }
        //     }



        // }
        // else{
        //     DPRINTF(CXL_ctrl, "[%s] rescheduling sendEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
        //     schedule(sendEvent, curTick()+this->clockPeriod());
        // }
    }

    void
    CXL_ctrl::schedTimingResp(PacketPtr pkt, Tick when)
    {
        if(!pkt->cxl_pkt.retry_resp){
            // if (transmitList_resp.empty()) {
            //     DPRINTF(CXL_ctrl, "[%s] scheduling sendrespEvent, when = %d, transmitList size %d\n",__func__,when, transmitList_resp.size());
            //     schedule(sendEventResp, when);
            // }
            DPRINTF(CXL_ctrl, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), transmitList_resp.size());

            assert(transmitList_resp.size() != maxQueueSize);

            //esj 2025-05-21
            if(!retransmit){
            // if(!retransmit && !pkt->cxl_pkt.retry_resp_from_device){
                transmitList_resp.emplace_back(pkt, when);
            }
            else{
                DPRINTF(CXL_ctrl,
                        "[%s] retransmit, sendrespEvent is not scheduled, "
                        "scheduling sendrespEvent, when = %d\n",
                        __func__, when);
                // if (!this->sendEventResp.scheduled()) {
                //     this->schedule(sendEventResp, when);
                // }
            }
        }
        else{
            DPRINTF(CXL_ctrl,
                    "[%s] retry_resp = true, enqueue for RRSM arbitration, "
                    "scheduling sendrespEvent, when = %d\n",
                    __func__, when);
            // Preserve retry arrival order. RRSM arbitration selects the
            // oldest ready retry ahead of normal traffic.
            transmitList_resp.emplace_back(pkt, when);
            // if (!this->sendEventResp.scheduled()){
            //     this->schedule(sendEventResp, when);
            // }
        }
        const Tick wakeup = std::max(when, curTick());
        if (!this->sendEventResp.scheduled()) {
            this->schedule(sendEventResp, wakeup);
        }
        else if (pkt->cxl_pkt.retry_resp &&
                 RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK &&
                 wakeup < sendEventResp.when()) {
            this->reschedule(sendEventResp, wakeup, true);
        }
        sampleEndpointStats();

    }

    void
    CXL_ctrl::trySendTimingResp()
    {
        assert(!transmitList_resp.empty());

        if (lrsmResponseControlReady()) {
            PacketPtr waiting_pkt = transmitList_resp.front().pkt;
            DPRINTF(CXL_ctrl,
                    "[LRSM] D2H control priority host=%u waiting_seq=%llu\n",
                    static_cast<unsigned>(packetHostIdx(waiting_pkt)),
                    static_cast<unsigned long long>(
                        waiting_pkt->cxl_pkt.seqNum));

            if (!control_flit_respEvent.scheduled()) {
                schedule(control_flit_respEvent, curTick());
            }
            else if (control_flit_respEvent.when() > curTick()) {
                reschedule(control_flit_respEvent, curTick(), true);
            }
        }

        const size_t selected_index = selectResponseTxIndex();
        DeferredPacket req = transmitList_resp[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (pkt->cxl_pkt.retry_resp &&
            RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            DPRINTF(CXL_ctrl,
                    "[RRSM] D2H retry priority host=%u seq=%llu index=%llu\n",
                    static_cast<unsigned>(packetHostIdx(pkt)),
                    static_cast<unsigned long long>(pkt->cxl_pkt.seqNum),
                    static_cast<unsigned long long>(selected_index));
        }

        DPRINTF(CXL_ctrl, "trySend request addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_resp.size());

        if (cxl_packing->out_port.recvTimingResp(pkt)) {
            // send successful
            transmitList_resp.erase(
                transmitList_resp.begin() + selected_index);
            if (!pkt->cxl_pkt.is_controlflit) {
                if (pkt->isWrite() || pkt->cmd == MemCmd::WriteResp ||
                    pkt->is_cxl_write_resp) {
                    stats.data_flit_resp_write += packetFlitCount(pkt);
                } else {
                    stats.data_flit_resp_read += packetFlitCount(pkt);
                }
            }

            //esj 2025-05-21
            // pkt->cxl_pkt.retry_resp_from_device = false;

            Device2Host_busy = false;
            // trans_state.send_packet_num++;

            //esj 2024-12-19
            // if(!pkt->cxl_pkt.retry_req){
            //esj 2024-12-25
            //esj 2025-01-10
            // if(!pkt->cxl_pkt.retry_resp && !pkt->cxl_pkt.is_controlflit  && !cxl_buffer_device.findByseqnum(pkt->cxl_pkt.seqNum) && !pkt->cxl_pkt.retry_req){

            //esj 2025-01-11
            //esj 2025-01-18
            // if(!pkt->cxl_pkt.retry_resp && !pkt->cxl_pkt.is_controlflit  && !pkt->cxl_pkt.retry_req){


            // esj 2025-05-20
            // if(!pkt->cxl_pkt.retry_resp && !pkt->cxl_pkt.is_controlflit  && !pkt->cxl_pkt.retry_req){
            if (needsDeviceReplaySlot(pkt)) {
            // if (!pkt->cxl_pkt.is_controlflit && !pkt->cxl_pkt.retry_req) {
                DPRINTF(CXL_ctrl,"%s, retry_resp = false, pushing packet to host buffer, addr=%0x\n",__func__, pkt->getAddr());

                //esj 2024-12-18
                // cxl_buffer_device.popByseqnum(pkt->cxl_pkt.seqNum);

                //esj 2024-12-25
                // if(!pkt->cxl_pkt.is_controlflit && !cxl_buffer_device.findByseqnum(pkt->cxl_pkt.seqNum) ){
                //     cxl_buffer_device.pushBack(pkt);
                //     DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                //     cxl_buffer_device.print_buffer();
                // }
                // else{
                //     DPRINTF(CXL_ctrl,"%s, packet is control flit, not pushing to buffer\n",__func__);
                // }
                DPRINTF(CXL_ctrl,"%s, pkt valid aaddr = %ss, valid size = %s, retry_req = %s, retry_resp = %s, is_control_flit = %s, seq = %d\n",__func__,pkt->getflags() & 0x00000100? "true":"false",pkt->getflags() & 0x00000200? "true":"false",pkt->cxl_pkt.retry_req? "true":"false",pkt->cxl_pkt.retry_resp? "true":"false",pkt->cxl_pkt.is_controlflit? "true":"false",pkt->cxl_pkt.seqNum);

                //esj 2025-01-26
                // cxl_buffer_device.pushBack(pkt);
                //esj 2025-06-30
                //esj 2025-07-14
                // if(!cxl_buffer_device.findByseqnum(pkt->cxl_pkt.seqNum)){
                // // if(!cxl_buffer_device.findByseqnum(pkt->cxl_pkt.seqNum) && (pkt->cmd != MemCmd::WriteResp)){
                //     DPRINTF(CXL_ctrl,"%s, packet is not yet in buffer, seqnum = %d\n",__func__,pkt->cxl_pkt.seqNum);
                //     cxl_buffer_device.pushBack(pkt);
                //     DPRINTF(CXL_ctrl,"%s, packet pushed to buffer, seqnum = %d\n",__func__,pkt->cxl_pkt.seqNum);
                // }
                // else{
                //     //esj 2025-07-14
                //     cxl_buffer_device.popByseqnum3(pkt->cxl_pkt.seqNum);
                //     DPRINTF(CXL_ctrl,"%s, packet is already in buffer, remove packet, seqnum = %d\n",__func__,pkt->cxl_pkt.seqNum);
                //     cxl_buffer_device.pushBack(pkt);
                //     //
                //     DPRINTF(CXL_ctrl,"%s, packet is already in buffer, addr=%0x\n",__func__, pkt->getAddr());
                // }
                // if(pkt->isResponse() && pkt->isRead()){
                if(pkt->isResponse()){
                    const bool replay_inserted =
                        insertDeviceReplaySnapshot(pkt);
                    fatal_if(
                        !replay_inserted,
                        "%s could not retain D2H replay sequence %llu\n",
                        name(),
                        (unsigned long long)pkt->cxl_pkt.seqNum);
                    DPRINTF(CXL_ctrl,
                            "%s, packet pushed to buffer, seqnum = %d\n",
                            __func__, pkt->cxl_pkt.seqNum);
                }

                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                //esj 2024-12-31
                //esj 2025-01-11
                //cxl_buffer_device.print_buffer2();();

                DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_device.size());
            }
            else{
                DPRINTF(CXL_ctrl,"%s, packet is control flit, not pushing to buffer\n",__func__);
                DPRINTF(CXL_ctrl,"%s, retry_resp = true, packet is already in buffer, addr=%0x\n",__func__, pkt->getAddr());
                DPRINTF(CXL_ctrl,"%s, current buffer size=%d\n",__func__, cxl_buffer_device.size());
                DPRINTF(CXL_ctrl,"%s, cancelSeqEvent, seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                cancelSeqEvent_device(pkt->cxl_pkt.seqNum);
            }

            sampleEndpointStats();
            replayPacket = NULL;

            //esj 2025-05-20
            // createSeqEvent_device(pkt->cxl_pkt.seqNum);
            if(!pkt->is_cxl_write_resp){
                createSeqEvent_device(pkt->cxl_pkt.seqNum);
            }

            if (!transmitList_resp.empty()) {
                DPRINTF(CXL_ctrl, "Scheduling next send\n");
                //esj 2025-02-19
                // schedule(sendEventResp, std::max(next_req.tick,curTick()+this->clockPeriod()));
                schedule(sendEventResp,
                    std::max(nextResponseTxWakeup(),
                             curTick()+this->clockPeriod()));
            }

        }
        else{
            DPRINTF(CXL_ctrl, "[%s] rescheduling sendrespEvent, when = %d\n",__func__,curTick()+this->clockPeriod());
            schedule(sendEventResp, curTick()+this->clockPeriod());
            sampleEndpointStats();
        }
    }

    bool
    CXL_ctrl :: CXL_ctrlResponsePort :: recvTimingReq(PacketPtr pkt)
    {
        DPRINTF(CXL_ctrl,"%s, %s\n",__func__,this->name());
        std::string portname = this->name();
        // if(pkt->cxl_flag & pkt->is_cxl_mem){
            bool success = false;
            if(portname.find("internal_Response") != std::string::npos){
                //PHY -> CXL(out -> PHY)
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s, Encoding packet to device\n",__func__);
                    success = ctrl->downstreamRequest.sendTimingReq(pkt);
                    //esj 2025-03-03
                    //esj 2025-07-09
                    // if(!success){
                    //     DPRINTF(CXL_ctrl,"%s, retryreq host -> device\n",__func__);
                    //     //esj 2025-05-08
                    //     ctrl->retry_pkt = true;
                    //     //esj 2025-05-10
                    //     pkt->cxl_pkt.is_retry_req = true;

                    //     //esj 2025-05-16
                    //     pkt->headerDelay = 0;
                    //     pkt->payloadDelay = 0;

                    //     ctrl->upstreamResponse.recvTimingReq(pkt);
                    // }
                    // //esj 2025-05-10
                    // else{
                    //     if(ctrl->retry_pkt){
                    //         ctrl->retry_pkt = false;
                    //     }
                    //     if(pkt->cxl_pkt.is_retry_req){
                    //         pkt->cxl_pkt.is_retry_req = false;
                    //     }
                    // }
                    //
                    if(!success){
                        //esj 2025-07-09
                        //esj 2025-07-11
                        // ctrl->retryReq_host = true;
                        DPRINTF(CXL_ctrl, "[%s] host -> device failed\n",__func__);
                        return false;
                    }
                    ctrl->request_busy = false;
                }
                else{
                    DPRINTF(CXL_ctrl,"[%s], Unpacking packet to device\n",__func__);

                    if(!pkt->cxl_pkt.is_controlflit){
                        DPRINTF(CXL_ctrl,"================ received response flit ===============\n");

                        //esj 2024-12-25
                        //esj 2025-01-10
                        // pkt->cxl_pkt.req_seqnum = ctrl->req_seqnum;

                        //esj 2024-12-10 CRC check
                        if(pkt->cxl_pkt.crc_check){
                            DPRINTF(CXL_ctrl,"%s, CRC check success, sending packet to device\n",__func__);
                            success = ctrl->upstreamRequest.sendTimingReq(pkt);

                            //esj 2024-12-26
                            //esj 2025-06-29
                            // if(!success){
                            //     DPRINTF(CXL_ctrl,"failed sending responsed packet\n");

                            //     //esj 2025-05-16
                            //     pkt->headerDelay = 0;
                            //     pkt->payloadDelay = 0;

                            //     //esj 2024-12-26
                            //     pkt->cxl_pkt.crc_check = false;
                            //     ctrl->retry_transmit_resp(pkt, curTick()+ctrl->clockPeriod());
                            //     return true;
                            // }
                            if(!success){
                                return false;
                            }



                            // //esj 2024-12-25
                            // ctrl->req_seqnum = pkt->cxl_pkt.seqNum;

                            if(pkt->cxl_pkt.retry_req){
                                //ctrl->control_flit = pkt; //esj 2024-12-18

                                //esj 2024-12-15
                                DPRINTF(CXL_ctrl,"%s, retry_buffer_resp poped, seq_num = %d\n",__func__,pkt->cxl_pkt.seqNum);
                                //esj 2025-01-11
                                // bool pop_success = ctrl->retry_buffer_resp.popByseqnum(pkt->cxl_pkt.seqNum);
                                bool pop_success = ctrl->retry_buffer_resp.popByseqnum3(pkt->cxl_pkt.seqNum);
                                //esj 2025-01-06
                                // assert(pop_success);

                                //esj 2024-12-31
                                // ctrl->retry_buffer_resp.print_buffer();
                                ctrl->LRSM.NUM_RETRY = ctrl->retry_buffer_resp.size();


                                if(ctrl->retry_buffer_resp.size() == 0){
                                    ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;

                                    //esj 2024-12-15
                                    if (ctrl->retryResp_host) {
                                        DPRINTF(CXL_ctrl,"%s, retryreq \n",__func__);
                                        ctrl->retryResp_host = false ;
                                        //esj 2025-06-27
                                        // ctrl->upstreamRequest.sendRetryResp();
                                        ctrl->upstreamRequest2.sendRetryResp();
                                    }
                                }
                                else if(ctrl->retry_buffer_resp.size() == 1){
                                    ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                                }
                                else{
                                    ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                                }
                                DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,ctrl->LRSM.cur_stat(),ctrl->LRSM.NUM_RETRY);

                                //esj 2024-12-15
                                // ctrl->schedule(ctrl->control_flit_respEvent, curTick()+ctrl->clockPeriod());
                                //esj 2025-01-17
                                // ctrl->retry_transmit_resp(pkt, curTick()+ctrl->clockPeriod());
                                ctrl->retry_transmit_resp(pkt, curTick()+ctrl->control_flit_cycle*ctrl->clockPeriod());
                            }

                            //esj 2025-01-17
                            if(pkt->cxl_pkt.crc_error_resp_seqnum != -1){
                                DPRINTF(CXL_ctrl, "======================[ retry crc error req packet ]======================\n");
                                DPRINTF(CXL_ctrl,"%s, crc_error_resp_seqnum = %llu\n",__func__,pkt->cxl_pkt.crc_error_resp_seqnum);

                                ctrl->RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                                DPRINTF(CXL_ctrl,"%s, Remote_state = %s\n",__func__,ctrl->RRSM.cur_stat());
                                DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);

                                //ctrl->cxl_buffer_device.print_buffer2();
                                //esj 2025-01-18 pkt->packet
                                //esj 2025-07-18
                                // PacketPtr packet = ctrl->cxl_buffer_device.popByseqnum6(pkt->cxl_pkt.crc_error_resp_seqnum);
                                PacketPtr packet =
                                    ctrl->cloneDeviceReplayPacket(
                                        pkt->cxl_pkt.crc_error_resp_seqnum);

                                //esj 2025-01-23
                                // if(packet != NULL && packet->isResponse() ){
                                if(packet != NULL){
                                    if(packet->isResponse()){
                                        DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.seqNum,packet->cxl_pkt.retry_resp,packet->cxl_pkt.is_controlflit);
                                        //esj 2025-07-21
                                        packet->cxl_pkt.req_seqnum =
                                            ctrl->reqSeqnumByHost(
                                                ctrl->packetHostIdx(packet));

                                        //esj 2025-01-25
                                        packet->cxl_pkt.retry_resp = true;

                                        //esj 2025-01-18
                                        // ctrl->check_retry_resp_packet(packet);

                                        ctrl->schedTimingResp(packet, curTick());

                                        DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                                    }
                                }
                                else{
                                    DPRINTF(CXL_ctrl,"%s, crc_error_resp_seqnum = %d, not found in buffer, already sent packet\n",__func__,pkt->cxl_pkt.crc_error_resp_seqnum);
                                }
                            }
                            //
                        }
                        else{
                            DPRINTF(CXL_ctrl,"%s, CRC check failed, sending packet to device\n",__func__);

                            //esj 2025-12-29
                            ctrl->stats.crc_error_count++;

                            // ctrl->RRSM.Remote_state = CXL_Remote_state::RETRY_LLRACK;
                            ctrl->LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                            ctrl->LRSM.NUM_RETRY = ctrl->retry_buffer_resp.size();
                            DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,ctrl->LRSM.cur_stat(),ctrl->LRSM.NUM_RETRY + 1);

                            if(ctrl->control_flit_respEvent.scheduled()){
                                DPRINTF(CXL_ctrl,"%s, descheduling retry control_flit\n",__func__);
                                //esj 2024-12-15
                                // ctrl->deschedule(ctrl->control_flit_respEvent);
                                // DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                                bool pop_success = ctrl->popfromtransmitlist_resp(pkt->cxl_pkt.seqNum);
                                if(pop_success){
                                    DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                                }
                                //esj 2024-12-31
                                // ctrl->print_retry_transmitList_resp();

                                //esj 2024-12-18
                                // if(pop_success){
                                //     if(ctrl->getsize_retry_transmitList_resp() == 0){
                                //         DPRINTF(CXL_ctrl,"%s, deschedule control_flit_respEvent, transmitList_resp size = %d\n",__func__,ctrl->getsize_retry_transmitList_resp());
                                //         ctrl->deschedule(ctrl->control_flit_respEvent);
                                //         DPRINTF(CXL_ctrl,"%s, pop success\n",__func__);
                                //     }
                                // }

                            }
                            //ctrl->control_flit = pkt; //esj 2024-12-18
                            //esj 2024-12-15
                            // ctrl->schedule(ctrl->control_flit_respEvent, curTick()+ctrl->clockPeriod());

                            //esj 2025-01-17
                            // ctrl->retry_transmit_resp(pkt, curTick()+ctrl->clockPeriod());
                            ctrl->retry_transmit_resp(pkt, curTick() + ctrl->crc_error_control_flit_cycle*ctrl->clockPeriod());

                            // //esj 2025-01-18
                            // ctrl->check_retry_resp_packet(pkt);

                            //esj 2025-01-18
                            // pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                            // pkt->cxl_pkt.crc_error_req_seqnum = -1;

                            //esj 2025-01-17
                            //esj 2025-01-23
                            ctrl->check_retry_req_packet(pkt);
                            //esj 2025-07-21
                            const uint8_t host_idx = ctrl->packetHostIdx(pkt);
                            DPRINTF(CXL_ctrl,"%s, error_req_transmitList host=%d emplace_back, seq_num = %d\n",__func__,host_idx,pkt->cxl_pkt.seqNum);
                            ctrl->errorReqListByHost(host_idx).emplace_back(
                                pkt->cxl_pkt.seqNum);



                            return true;
                        }

                        if(success){
                            //esj 2024-12-22
                            // uint64_t seq_num = pkt->cxl_pkt.send_information.sen_packet_num;

                            // //esj 2024-12-26
                            // ctrl->recv_packet_num++;
                            // DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",ctrl->recv_packet_num);

                            //esj 2024-12-25
                            //esj 2025-07-21
                            const uint8_t host_idx = ctrl->packetHostIdx(pkt);
                            DPRINTF(CXL_ctrl,"%s, req_seqnum host=%d = %d\n",__func__,host_idx,pkt->cxl_pkt.seqNum);
                            ctrl->reqSeqnumByHost(host_idx) =
                                pkt->cxl_pkt.seqNum;
                            // Keep legacy mirrors for compatibility/debug logs.
                            ctrl->req_seqnum = ctrl->reqSeqnumByHost(0);
                            ctrl->req_seqnum_second = ctrl->reqSeqnumByHost(1);

                            //esj 2025-01-18
                            //esj 2025-07-21
                            // for(auto it = ctrl->error_req_transmitList.begin(); it != ctrl->error_req_transmitList.end(); it++){
                            //     if(it->crc_error_seqnum == pkt->cxl_pkt.seqNum){
                            //         DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                            //         it = ctrl->error_req_transmitList.erase(it);
                            //         break;
                            //     }
                            // }
                            auto &error_req_list =
                                ctrl->errorReqListByHost(host_idx);
                            for(auto it = error_req_list.begin();
                                it != error_req_list.end();
                                it++){
                                if(it->crc_error_seqnum == pkt->cxl_pkt.seqNum){
                                    DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                                    it = error_req_list.erase(it);
                                    break;
                                }
                            }
                            //

                            uint64_t seq_num = pkt->cxl_pkt.seqNum;
                            if(ctrl->cxl_buffer_device.empty()){
                                DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                            }
                            // else{
                            //esj 2024-12-22
                            // //esj 2024-12-18
                            // else if(ctrl->LRSM.cur_stat() == "RETRY_LOCAL_NORMAL"&& pkt->cxl_pkt.LRSM_host == "RETRY_LOCAL_NORMAL"){
                            //     bool popseq = false;
                            //     int num = seq_num;
                            //     while (num) {
                            //         //esj 2024-12-17
                            //         if(ctrl->cxl_buffer_device.popByseqnum2(num) != NULL){
                            //             if(ctrl->cxl_buffer_device.popByseqnum4(num)){
                            //                 continue;
                            //             }
                            //         }
                            //         popseq = ctrl->cxl_buffer_device.popByseqnum(num);
                            //         if(popseq){
                            //             DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, ctrl->cxl_buffer_device.size());
                            //             DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, pkt->getAddr(), pkt->cxl_pkt.seqNum);
                            //             ctrl->cancelSeqEvent_device(seq_num);
                            //             DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                            //             ctrl->cxl_buffer_device.print_buffer();
                            //         }
                            //         num--;
                            //     }
                            //     DPRINTF(CXL_ctrl, "[%s] Current buffer: %d/%d\n",__func__, ctrl->cxl_buffer_device.size(), ctrl->cxl_buffer_device.maximumSize);
                            //     ///////////////////////////////
                            // }
                            //esj 2024-12-17
                            else{
                                bool popseq = false;
                                //esj 2024-12-25
                                //esj 2025-01-10
                                // popseq = ctrl->cxl_buffer_device.popByseqnum(seq_num);
                                //esj 2025-01-10
                                // popseq = ctrl->cxl_buffer_device.popByseqnum3(seq_num);
                                // DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");

                                //esj 2024-12-22
                                DPRINTF(CXL_ctrl, "[%s] pop packet(ack) from buffer: resp seq=%d\n",__func__,  pkt->cxl_pkt.resp_seqnum);
                                //esj 2025-01-10
                                // ctrl->cxl_buffer_device.popByseqnum(pkt->cxl_pkt.resp_seqnum);
                                //esj 2025-01-11
                                //ctrl->cxl_buffer_device.print_buffer2();
                                popseq = ctrl->cxl_buffer_device.popByseqnum3(
                                    pkt->cxl_pkt.resp_seqnum);
                                if (popseq) {
                                    ctrl->releaseDeviceReplaySnapshot(
                                        pkt->cxl_pkt.resp_seqnum);
                                }
                                DPRINTF(
                                    CXL_ctrl,
                                    "[%s] seq_num found in buffer = %s\n",
                                    __func__, popseq ? "success" : "failed");

                                //esj 2025-02-27
                                if (ctrl->retryResp_host & popseq) {
                                    DPRINTF(CXL_ctrl,"%s, retryresp \n",__func__);
                                    ctrl->retryResp_host = false ;
                                    //esj 2025-06-27
                                    // ctrl->upstreamRequest.sendRetryResp();
                                    ctrl->upstreamRequest2.sendRetryResp();
                                }

                                //esj 2025-01-10
                                // ctrl->cancelSeqEvent_device(pkt->cxl_pkt.resp_seqnum);
                                ctrl->cancelSeqEvent_device2(pkt->cxl_pkt.resp_seqnum);

                                //esj 2024-12-30
                                // if(ctrl->cxl_buffer_device.size() == 0){
                                //     // int num = pkt->cxl_pkt.seqNum;
                                //     //esj 2024-12-23
                                //     int num = pkt->cxl_pkt.resp_seqnum;

                                //     while(num){
                                //         ctrl->cancelSeqEvent_device(num);
                                //         num--;
                                //     }
                                //     DPRINTF(CXL_ctrl, "reset timeout\n");
                                // }


                                //esj 2024-12-22
                                // if(popseq){
                                //     DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, ctrl->cxl_buffer_device.size());
                                //     DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, pkt->getAddr(), pkt->cxl_pkt.seqNum);
                                //     ctrl->cancelSeqEvent_device(seq_num);
                                // }

                                //esj 2024-12-25
                                // DPRINTF(CXL_ctrl, "[%s] Removing from buffer: after pop, buffer_size=%d\n",__func__, ctrl->cxl_buffer_device.size());
                                // DPRINTF(CXL_ctrl, "[%s] poped packet from buffer: addr=0x%x, seq=%d\n",__func__, pkt->getAddr(), pkt->cxl_pkt.seqNum);
                                // ctrl->cancelSeqEvent_device(seq_num);

                            }

                            DPRINTF(CXL_ctrl,"%s, ctrl->cxl_buffer_device\n",__func__);
                            //esj 2024-12-26
                            //esj 2025-01-10
                            // ctrl->cxl_buffer_device.print_buffer();
                            //esj 2024-12-31
                            //ctrl->cxl_buffer_device.print_buffer2();

                            ctrl->recv_state.recv_packet_num++;
                            DPRINTF(CXL_ctrl,"%s, Host recv_packet_num = %d\n",__func__,ctrl->recv_state.recv_packet_num);


                            //ctrl->control_flit = pkt; //esj 2024-12-18
                            //esj 2024-12-09
                            //scheduling issue

                            //esj 2024-12-17
                            //esj 2025-05-20
                            // if(!pkt->cxl_pkt.retry_req){
                            if(!pkt->cxl_pkt.retry_req && !pkt->isWrite()){
                                //esj 2025-02-02
                                // ctrl->retry_transmit_resp(pkt, curTick()+10*ctrl->clockPeriod());
                                ctrl->retry_transmit_resp(pkt, curTick()+ctrl->control_flit_cycle*ctrl->clockPeriod());
                            }
                            // if(!ctrl->control_flit_respEvent.scheduled()){
                            //     //esj 2024-12-15
                            //     // ctrl->schedule(ctrl->control_flit_respEvent, curTick() + 10*ctrl->clockPeriod());
                            //     ctrl->retry_transmit_resp(pkt, curTick()+10*ctrl->clockPeriod());
                            // }
                            // else{
                            //     DPRINTF(CXL_ctrl,"%s, control_flit_respEvent is already scheduled\n",__func__);
                            // }

                            DPRINTF(CXL_ctrl,"%s, receive packet success, recv_packet_num = %d\n",__func__,ctrl->recv_state.recv_packet_num);
                        }
                    }
                    else{
                        DPRINTF(CXL_ctrl,"%s, packet is control flit, sending packet to device\n",__func__);
                        //esj 2025-02-19
                        // if(pkt->cxl_pkt.complete){
                        //     delete pkt;
                        //     warn("control flit is already completed\n");
                        //     return true;
                        // }
                        //esj 2024-12-13
                        success = ctrl->recvrequest(pkt);

                        //esj 2025-02-19
                        // if(!success){
                        //     DPRINTF(CXL_ctrl,"%s, control flit failed\n",__func__);
                        //     delete pkt;
                        // }
                    }

                }

                return success;
            }
            else if(portname.find("upstreamResponse") != std::string::npos){
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"=============== recv request flit ===============\n");
                    //esj 2025-01-03
                    // add pcielink
                    if(!pkt->cxl_flag){
                        DPRINTF(CXL_ctrl,"%s,send packet to CXL_in_port port(host -> flexbus )\n",__func__);
                        return ctrl->cxl_flexbus->CXL_in_port.recvTimingReq(pkt);
                    }

                    DPRINTF(CXL_ctrl,"%s,Host controller:: Host -> Device\n",__func__);

                    //esj 2024-12-13
                    DPRINTF(CXL_ctrl,"%s,packet addr = %0x\n", __func__, pkt->getAddr());
                    // if(ctrl->control_flit_reqEvent.scheduled()){
                    //     DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_reqEvent\n",__func__);
                    //     ctrl->deschedule(ctrl->control_flit_reqEvent);
                    // }

                    //esj 2025-01-01
                    if(ctrl->control_flit_reqEvent.scheduled()){
                        DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_reqEvent\n",__func__);
                        const uint8_t host_idx = ctrl->packetHostIdx(pkt);
                        const uint64_t resp_seq =
                            ctrl->respSeqnumByHost(host_idx);
                        bool pop_success =
                            ctrl->popfromtransmitlist_req(resp_seq);
                        if(pop_success){
                            DPRINTF(CXL_ctrl,
                                    "%s, POP from popfromtransmitlist_req host=%d seq = %d\n",
                                    __func__,
                                    host_idx,
                                    resp_seq);
                        }
                    }

                    //esj 2025-01-24
                    pkt->cxl_pkt.complete = false;
                    //


                    ctrl->request_busy = true;
                    //esj 2024-12-13
                    pkt->cxl_pkt.send_information.sen_packet_num = ctrl->recv_state.recv_packet_num-1;
                    // pkt->cxl_pkt.send_information.sen_packet_num = ctrl->recv_state.recv_packet_num;
                    DPRINTF(CXL_ctrl,"%s, pkt->cxl_pkt.send_information.sen_packet_num = %d\n",__func__,pkt->cxl_pkt.send_information.sen_packet_num);

                    success = ctrl->TL2PHY_interface(pkt);
                }
                else{
                    DPRINTF(CXL_ctrl,"%s,Device controller:: dma \n",__func__);

                    //esj 2025-01-03
                    // add pcielink
                    if(!pkt->cxl_flag){
                        DPRINTF(CXL_ctrl,"%s,send packet to CXL_in_port port(host -> flexbus )\n",__func__);
                        success = ctrl->cxl_flexbus->CXL_in_port.recvTimingReq(pkt);
                    }
                    else
                        success = ctrl->downstreamRequest.sendTimingReq(pkt);

                }

                return success;
            }
            else if(portname.find("downstreamResponse") != std::string::npos){
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s,Host controller:: dma \n",__func__);
                    success = ctrl->upstreamRequest.sendTimingReq(pkt);
                }
                else{
                    DPRINTF(CXL_ctrl,"%s,Device controller:: Host -> Device\n",__func__);
                    //esj 2024-12-17
                    // success = ctrl->PHY2TL_interface(pkt);
                    success = ctrl->cxl_decoder->in_port.recvTimingReq(pkt);

                }

                return success;
            }
            //esj 2025-01-03
            else if(portname.find("pcie_Response") != std::string::npos){
                if(ctrl->is_host){
                    DPRINTF(CXL_ctrl,"%s,send packet to host upstreamRequest port( flexbus --> Host )\n",__func__);
                    return ctrl->upstreamRequest.sendTimingReq(pkt);
                }
                else{
                    DPRINTF(CXL_ctrl,"%s,send packet to device upstreamRequest port( flexbus --> Device )\n",__func__);
                    return ctrl->upstreamRequest.sendTimingReq(pkt);
                }
            }

        return 0;

    }
    void
    CXL_ctrl::control_flit_respfunc(){
        DPRINTF(CXL_ctrl,"=============== send control flit(device --> host)===============\n");
        DPRINTF(CXL_ctrl, "[%s] Control flit response\n", __func__);

        // if(!cxl_packing->Packing_respEvent.scheduled() | !response_busy){
        // // if(transmitList_resp.empty() | !response_busy){


        //     RequestPtr req = std::make_shared<gem5::Request>(
        //     control_flit->getAddr(), control_flit->getSize(),0,0);

        //     // Create response packet
        //     PacketPtr resp_pkt = new Packet(req, MemCmd::WriteResp);
        //     resp_pkt->allocate();  // Allocate space for data payload

        //     // Copy CXL-specific information from request
        //     resp_pkt->cxl_pkt = control_flit->cxl_pkt;
        //     resp_pkt->cxl_pkt.isReq = false;  // Mark as response
        //     resp_pkt->cxl_pkt.seqNum = control_flit->cxl_pkt.seqNum;
        //     resp_pkt->cxl_pkt.is_controlflit = true;
        //     resp_pkt->cxl_pkt.is_encoded = true;

        //     DPRINTF(CXL_ctrl,"%s, retry_req = %s, retry_ack = %s\n",__func__,control_flit->cxl_pkt.retry_req? "true":"false", control_flit->cxl_pkt.retry_ack? "true":"false");

        //     if(control_flit->cxl_pkt.crc_check){
        //         if(control_flit->cxl_pkt.retry_req){
        //             resp_pkt->cxl_pkt.retry_ack = true;
        //         }
        //         resp_pkt->cxl_pkt.retry_req = false;
        //         DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_req = %s\n",__func__,resp_pkt->cxl_pkt.retry_req? "true":"false");
        //     }
        //     else{
        //         resp_pkt->cxl_pkt.retry_req = true;
        //         DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_req = %s\n",__func__,resp_pkt->cxl_pkt.retry_req? "true":"false");
        //     }

        //     bool success1 = cxl_packing->out_port.recvTimingResp(resp_pkt);
        //     DPRINTF(CXL_ctrl,"[%s], send response packet to device, success=%d\n",__func__,success1);
        //     DPRINTF(CXL_ctrl,"[%s], packet addr = 0x%x, seq = %d, retry_req = %d\n",__func__,resp_pkt->getAddr(),resp_pkt->cxl_pkt.seqNum,resp_pkt->cxl_pkt.retry_req);


        // }

        //esj 2025-01-01
        // if(!cxl_packing->Packing_respEvent.scheduled() | !response_busy){

        int retry_transmitList_resp_size = getsize_retry_transmitList_resp();
        DPRINTF(CXL_ctrl, "[%s] ransmitList_resp size = %d\n", __func__,retry_transmitList_resp_size);
        DPRINTF(CXL_ctrl, "[%s] Packing_respEvent schedule? = %s, request_busy? = %s\n", __func__,cxl_packing->Packing_respEvent.scheduled()? "true":"false",response_busy? "true":"false");

        //esj 2025-01-10
        // if(!cxl_packing->Packing_respEvent.scheduled() && !response_busy && retry_transmitList_resp_size != 0){


        if((!cxl_packing->Packing_respEvent.scheduled() || !response_busy) && retry_transmitList_resp_size != 0){
        //esj 2025-02-27
        // if((!cxl_packing->Packing_respEvent.scheduled()) && retry_transmitList_resp_size != 0){

            // //esj 2025-01-05
            // if(!retry_transmitList_resp.front().valid){
            //esj 2025-01-07
            if(!retry_transmitList_resp.front().valid || !(retry_transmitList_resp.front().pkt->getflags() & 0x00000100) || !(retry_transmitList_resp.front().pkt->getflags() & 0x00000200)){
                DPRINTF(CXL_ctrl,"retry_transmitList_resp.front packet is not valid, pop out\n");
                retry_transmitList_resp.pop_front();

                if (retry_transmitList_resp_size != 0) {
                    DPRINTF(CXL_ctrl,"%s, getsize_retry_transmitList_resp = %d\n",__func__,retry_transmitList_resp_size);
                    DeferredPacket next_req = retry_transmitList_resp.front();
                    DPRINTF(CXL_ctrl, "Scheduling next send\n");
                    this->schedule(control_flit_respEvent, std::max(next_req.tick,curTick()+this->clockPeriod()));
                }

            }
            else{
                //esj 2025-01-25
                // if(retry_transmitList_resp.front().tick > curTick()){
                //     DPRINTF(CXL_ctrl,"%s, [reschedule control flit] flit.tick = %d, curTick = %d\n",__func__,retry_transmitList_resp.front().tick,curTick());
                //     schedule(control_flit_respEvent, retry_transmitList_resp.front().tick);
                // }
                //


                PacketPtr resp_pkt = make_control_flit_resp(retry_transmitList_resp.front().pkt);

                DPRINTF(CXL_ctrl,"%s, resp_pkt addr = %x, seq = %d, isresponse = %d, iswrite = %d\n",__func__,resp_pkt->getAddr(),resp_pkt->cxl_pkt.seqNum,resp_pkt->isResponse(),resp_pkt->isWrite());

                //esj 2025-01-23
                //esj 2025-07-22
                // for(auto it = error_req_transmitList.begin(); it != error_req_transmitList.end(); it++){
                //     if(it->crc_error_seqnum == retry_transmitList_resp.front().pkt->cxl_pkt.seqNum){
                //         DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                //         it = error_req_transmitList.erase(it);
                //         break;
                //     }
                // }

                auto &error_req_list =
                    errorReqListByHost(packetHostIdx(resp_pkt));
                for(auto it = error_req_list.begin();
                    it != error_req_list.end();
                    it++){
                    if(it->crc_error_seqnum ==
                        retry_transmitList_resp.front().pkt->cxl_pkt.seqNum){
                        DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                        it = error_req_list.erase(it);
                        break;
                    }
                }


                DPRINTF(CXL_ctrl,"%s, retry_req = %s, retry_ack = %s, valid packet = %s\n",__func__,resp_pkt->cxl_pkt.retry_req? "true":"false", resp_pkt->cxl_pkt.retry_ack? "true":"false",retry_transmitList_resp.front().valid? "true":"false");

                if(resp_pkt->cxl_pkt.crc_check){
                    if(resp_pkt->cxl_pkt.retry_req){
                        resp_pkt->cxl_pkt.retry_ack = true;
                    }
                    resp_pkt->cxl_pkt.retry_req = false;

                    DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_req = %s\n",__func__,resp_pkt->cxl_pkt.retry_req? "true":"false");
                }
                else{
                    resp_pkt->cxl_pkt.retry_req = true;

                    //esj 2024-12-18
                    // retry_buffer_resp.popByseqnum(resp_pkt->cxl_pkt.seqNum);
                    if(!retry_buffer_resp.findByseqnum(resp_pkt->cxl_pkt.seqNum)){
                        retry_buffer_resp.pushBack(resp_pkt);
                        DPRINTF(CXL_ctrl,"%s, push packet to retry_buffer_resp, seq = %d\n",__func__,resp_pkt->cxl_pkt.seqNum);
                    }
                    else{
                        DPRINTF(CXL_ctrl,"%s, seq = %d already exists in retry_buffer_resp\n",__func__,resp_pkt->cxl_pkt.seqNum);
                    }
                    DPRINTF(CXL_ctrl,"%s, retry_buffer_resp\n",__func__);
                    //esj 2024-12-31
                    // retry_buffer_resp.print_buffer();
                    LRSM.NUM_RETRY = retry_buffer_resp.size();

                    //esj 2024-12-17
                    if(retry_buffer_resp.size() == 0){
                        LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;
                    }
                    else if(retry_buffer_resp.size() == 1){
                        LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    }
                    else if(retry_buffer_resp.size() < maxQueueSize){
                        LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                    }
                    else{
                        LRSM.Local_state = CXL_Local_state::RETRY_PHY_REINIT;
                    }
                    DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,LRSM.cur_stat(),LRSM.NUM_RETRY);

                    DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_req = %s\n",__func__,resp_pkt->cxl_pkt.retry_req? "true":"false");
                }

                bool success1 = cxl_packing->out_port.recvTimingResp(resp_pkt);

                if(success1){

                    //esj 2025-02-24
                    if(retry_transmitList_resp.front().pkt->isWrite()){
                        stats.control_flit_req_write++;
                    }
                    else{
                        stats.control_flit_req_read++;
                    }
                    ///

                    retry_transmitList_resp.pop_front();


                    DPRINTF(CXL_ctrl,"[%s], send response packet to device, send success\n",__func__);
                    DPRINTF(CXL_ctrl,"[%s], packet addr = 0x%x, seq = %d, retry_req = %d\n",__func__,resp_pkt->getAddr(),resp_pkt->cxl_pkt.seqNum,resp_pkt->cxl_pkt.retry_req);

                    // if (!retry_transmitList_resp.empty()) {
                    if (getsize_retry_transmitList_resp() != 0) {
                        DPRINTF(CXL_ctrl,"%s, getsize_retry_transmitList_resp = %d\n",__func__,getsize_retry_transmitList_resp());
                        DeferredPacket next_req = retry_transmitList_resp.front();
                        DPRINTF(CXL_ctrl, "Scheduling next send\n");
                        schedule(control_flit_respEvent, std::max(next_req.tick,curTick()+this->clockPeriod()));
                    }
                }
                else{
                    DPRINTF(CXL_ctrl,"[%s], send response packet to device, send failed\n",__func__);
                    //esj 2025-02-17
                    delete resp_pkt;
                    //
                    schedule(control_flit_respEvent, curTick()+clockPeriod());
                }
            }
        }
        //esj 2025-01-09
        else{

            if(retry_transmitList_resp_size != 0){
                DPRINTF(CXL_ctrl, "Scheduling next send\n");
                schedule(control_flit_respEvent, curTick()+this->clockPeriod());
            }
        }
    }

    void
    CXL_ctrl::control_flit_reqfunc(){
        DPRINTF(CXL_ctrl,"=============== send control flit(host --> device) ===============\n");
        DPRINTF(CXL_ctrl, "[%s] Control flit response\n", __func__);

        // if(!cxl_packing->Packing_respEvent.scheduled() | !request_busy){
        // // if(transmitList.empty() | !request_busy){

        //     PacketPtr req_pkt = make_control_flit_req(control_flit_resp);

        //     delete control_flit_resp;

        //     DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, retry_resp = %d\n",__func__,req_pkt->getAddr(),req_pkt->cxl_pkt.seqNum,req_pkt->cxl_pkt.retry_resp);
        //     DPRINTF(CXL_ctrl,"%s, retry_resp = %s, retry_ack = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false", req_pkt->cxl_pkt.retry_ack? "true":"false");

        //     if(req_pkt->cxl_pkt.crc_check){
        //         if(req_pkt->cxl_pkt.retry_resp){
        //             req_pkt->cxl_pkt.retry_ack = true;
        //         }
        //         req_pkt->cxl_pkt.retry_resp = false;
        //         DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_resp = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false");
        //     }
        //     else{
        //         req_pkt->cxl_pkt.retry_resp = true;
        //         DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_resp = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false");
        //     }

        //     bool success1 = cxl_packing->in_port.recvTimingReq(req_pkt);
        //     DPRINTF(CXL_ctrl,"[%s], send request packet to device, success=%d\n",__func__,success1);
        //     DPRINTF(CXL_ctrl,"[%s], packet addr = 0x%x, seq = %d, retry_req = %d\n",__func__,req_pkt->getAddr(),req_pkt->cxl_pkt.seqNum,req_pkt->cxl_pkt.retry_req);


        // }

        //esj 2025-01-01
        // if(!cxl_packing->Packing_respEvent.scheduled() | !request_busy){

        int retry_transmitList_req_size = getsize_retry_transmitList_req();
        DPRINTF(CXL_ctrl, "[%s] retry_transmitList_req_size  = %d\n", __func__,retry_transmitList_req_size);
        DPRINTF(CXL_ctrl, "[%s] PackingEvent schedule? = %s, request_busy? = %s\n", __func__,cxl_packing->PackingEvent.scheduled()? "true":"false",request_busy? "true":"false");


        //esj 2025-01-10
        // if(!cxl_packing->Packing_respEvent.scheduled() && !request_busy && retry_transmitList_req_size != 0){


        if((!cxl_packing->PackingEvent.scheduled()|| !request_busy ) && retry_transmitList_req_size != 0){
        //esj 2025-02-27
        // if((!cxl_packing->PackingEvent.scheduled()) && retry_transmitList_req_size != 0){
            // //esj 2025-01-05
            // if(!retry_transmitList_req.front().valid){
            //esj 2025-01-07
            if(!retry_transmitList_req.front().valid || !(retry_transmitList_req.front().pkt->getflags() & 0x00000100) || !(retry_transmitList_req.front().pkt->getflags() & 0x00000200)){
                DPRINTF(CXL_ctrl,"retry_transmitList_req.front packet is not valid, pop out\n");
                retry_transmitList_req.pop_front();

                if (retry_transmitList_req_size != 0) {
                    DPRINTF(CXL_ctrl,"%s, getsize_retry_transmitList_req = %d\n",__func__,retry_transmitList_req_size);
                    DeferredPacket next_req = retry_transmitList_req.front();
                    DPRINTF(CXL_ctrl, "Scheduling next send\n");
                    this->schedule(control_flit_reqEvent, std::max(next_req.tick, (curTick()+this->clockPeriod()) ));
                }

            }
            else{
                //esj 2025-01-25
                // if(retry_transmitList_req.front().tick > curTick()){
                //     DPRINTF(CXL_ctrl,"%s, [reschedule control flit] flit.tick = %d, curTick = %d\n",__func__,retry_transmitList_req.front().tick,curTick());
                //     schedule(control_flit_reqEvent, retry_transmitList_req.front().tick);
                // }
                //

                PacketPtr req_pkt = make_control_flit_req(retry_transmitList_req.front().pkt);

                //esj 2025-01-23
                for(auto it = error_resp_transmitList.begin(); it != error_resp_transmitList.end(); it++){
                    if(it->crc_error_seqnum == retry_transmitList_req.front().pkt->cxl_pkt.seqNum){
                        DPRINTF(CXL_ctrl,"%s, found error_resp_transmitList, erase it\n",__func__);
                        it = error_resp_transmitList.erase(it);
                        break;
                    }
                }

                DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, retry_resp = %d, valid packet = %s\n",__func__,req_pkt->getAddr(),req_pkt->cxl_pkt.seqNum,req_pkt->cxl_pkt.retry_resp,retry_transmitList_req.front().valid? "true":"false");
                DPRINTF(CXL_ctrl,"%s, retry_resp = %s, retry_ack = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false", req_pkt->cxl_pkt.retry_ack? "true":"false");

                if(req_pkt->cxl_pkt.crc_check){
                    if(req_pkt->cxl_pkt.retry_resp){
                        req_pkt->cxl_pkt.retry_ack = true;
                    }
                    //esj 2025-01-25
                    // req_pkt->cxl_pkt.retry_resp = false;
                    DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_resp = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false");
                }
                else{
                    req_pkt->cxl_pkt.retry_resp = true;

                    // esj 2024-12-18
                    // retry_buffer_req.popByseqnum(req_pkt->cxl_pkt.seqNum);
                    if(!retry_buffer_req.findByseqnum(req_pkt->cxl_pkt.seqNum)){
                        retry_buffer_req.pushBack(req_pkt);
                        DPRINTF(CXL_ctrl,"%s, push packet to retry_buffer_req, seq = %d\n",__func__,req_pkt->cxl_pkt.seqNum);
                    }
                    else{
                        DPRINTF(CXL_ctrl,"%s, seq = %d already exists in retry_buffer_req\n",__func__,req_pkt->cxl_pkt.seqNum);
                    }

                    DPRINTF(CXL_ctrl,"%s, retry_buffer_req\n",__func__);
                    //esj 2024-12-31
                    // retry_buffer_req.print_buffer();

                    //esj 2024-12-18
                    if(retry_buffer_req.size() == 0){
                        LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;
                    }
                    else if(retry_buffer_req.size() == 1){
                        LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                    }
                    else if(retry_buffer_req.size() < maxQueueSize){
                        LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                    }
                    else{
                        LRSM.Local_state = CXL_Local_state::RETRY_PHY_REINIT;
                    }
                    DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,LRSM.cur_stat(),LRSM.NUM_RETRY);

                    DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_resp = %s\n",__func__,req_pkt->cxl_pkt.retry_resp? "true":"false");
                }

                bool success1 = cxl_packing->in_port.recvTimingReq(req_pkt);

                if(success1){

                    //esj 2025-02-24
                    if(retry_transmitList_req.front().pkt->isWrite()){
                        stats.control_flit_resp_write++;
                    }
                    else{
                        stats.control_flit_resp_read++;
                    }
                    ///

                    //esj 2025-01-10
                    //esj 2025-07-13
                    DPRINTF(CXL_ctrl,"%s, delete packet\n",__func__);
                    delete retry_transmitList_req.front().pkt;
                    DPRINTF(CXL_ctrl,"%s, delete packet done\n",__func__);
                    retry_transmitList_req.pop_front();
                    DPRINTF(CXL_ctrl,"%s, pop packet done\n",__func__);

                    DPRINTF(CXL_ctrl,"[%s], send request packet to device, send success\n",__func__);
                    DPRINTF(CXL_ctrl,"[%s], packet addr = 0x%x, seq = %d, retry_resp = %d\n",__func__,req_pkt->getAddr(),req_pkt->cxl_pkt.seqNum,req_pkt->cxl_pkt.retry_resp);

                    // if (!retry_transmitList_req.empty()) {
                    if (getsize_retry_transmitList_req() != 0) {
                        DeferredPacket next_req = retry_transmitList_req.front();
                        DPRINTF(CXL_ctrl, "Scheduling next send\n");
                        schedule(control_flit_reqEvent, std::max(next_req.tick,curTick()+this->clockPeriod()));
                    }

                }
                else{
                    DPRINTF(CXL_ctrl,"[%s], send request packet to device, send failed\n",__func__);
                    //esj 2025-02-17
                    delete req_pkt;
                    //

                    schedule(control_flit_reqEvent, curTick()+clockPeriod());
                }
            }
        }
        //esj 2025-01-09
        else{
            if(retry_transmitList_req_size != 0){
                DPRINTF(CXL_ctrl, "Scheduling next send\n");
                schedule(control_flit_reqEvent, curTick()+this->clockPeriod());
            }
        }
    }


    void
    CXL_ctrl::check_retry_req_packet(PacketPtr pkt){
        int error_req_transmitList_size = 0;
        for (uint8_t host_idx = 0; host_idx < Packet::MaxPciRequesterIds;
             ++host_idx) {
            error_req_transmitList_size +=
                errorReqListByHost(host_idx).size();
        }
        DPRINTF(CXL_ctrl, "[%s] error_req_transmitList total size = %d\n",
                __func__,
                error_req_transmitList_size);
        // if(error_req_transmitList_size != 0){

            DPRINTF(CXL_ctrl,"%s, retry_req = %s, retry_ack = %s, valid packet = %s\n",__func__,pkt->cxl_pkt.retry_req? "true":"false", pkt->cxl_pkt.retry_ack? "true":"false",retry_transmitList_resp.front().valid? "true":"false");

            if(pkt->cxl_pkt.crc_check){
                if(pkt->cxl_pkt.retry_req){
                    pkt->cxl_pkt.retry_ack = true;
                }
                pkt->cxl_pkt.retry_req = false;

                DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_req = %s\n",__func__,pkt->cxl_pkt.retry_req? "true":"false");
            }
            else{
                pkt->cxl_pkt.retry_req = true;

                if(!retry_buffer_resp.findByseqnum(pkt->cxl_pkt.seqNum)){
                    retry_buffer_resp.pushBack(pkt);
                    DPRINTF(CXL_ctrl,"%s, push packet to retry_buffer_resp, seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                }
                else{
                    DPRINTF(CXL_ctrl,"%s, seq = %d already exists in retry_buffer_resp\n",__func__,pkt->cxl_pkt.seqNum);
                }
                DPRINTF(CXL_ctrl,"%s, retry_buffer_resp\n",__func__);

                LRSM.NUM_RETRY = retry_buffer_resp.size();

                if(retry_buffer_resp.size() == 0){
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;
                }
                else if(retry_buffer_resp.size() == 1){
                    LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                }
                else if(retry_buffer_resp.size() < maxQueueSize){
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                }
                else{
                    LRSM.Local_state = CXL_Local_state::RETRY_PHY_REINIT;
                }
                DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,LRSM.cur_stat(),LRSM.NUM_RETRY);

                DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_req = %s\n",__func__,pkt->cxl_pkt.retry_req? "true":"false");
            }
        // }

    }

    void
    CXL_ctrl::check_retry_resp_packet(PacketPtr pkt){
        DPRINTF(CXL_ctrl, "[%s] error_resp_transmitList  = %d\n", __func__,error_resp_transmitList.size());
        // if(error_resp_transmitList.size() != 0){
            DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, retry_resp = %d, valid packet = %s\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_resp,retry_transmitList_req.front().valid? "true":"false");
            DPRINTF(CXL_ctrl,"%s, retry_resp = %s, retry_ack = %s\n",__func__,pkt->cxl_pkt.retry_resp? "true":"false", pkt->cxl_pkt.retry_ack? "true":"false");

            if(pkt->cxl_pkt.crc_check){
                if(pkt->cxl_pkt.retry_resp){
                    pkt->cxl_pkt.retry_ack = true;
                }
                //esj 2025-01-25
                // pkt->cxl_pkt.retry_resp = false;
                DPRINTF(CXL_ctrl,"%s, crc_check = true, retry_resp = %s\n",__func__,pkt->cxl_pkt.retry_resp? "true":"false");
            }
            else{
                pkt->cxl_pkt.retry_resp = true;

                if(!retry_buffer_req.findByseqnum(pkt->cxl_pkt.seqNum)){
                    retry_buffer_req.pushBack(pkt);
                    DPRINTF(CXL_ctrl,"%s, push packet to retry_buffer_req, seq = %d\n",__func__,pkt->cxl_pkt.seqNum);
                }
                else{
                    DPRINTF(CXL_ctrl,"%s, seq = %d already exists in retry_buffer_req\n",__func__,pkt->cxl_pkt.seqNum);
                }

                DPRINTF(CXL_ctrl,"%s, retry_buffer_req\n",__func__);
                if(retry_buffer_req.size() == 0){
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_NORMAL;
                }
                else if(retry_buffer_req.size() == 1){
                    LRSM.Local_state = CXL_Local_state::RETRY_LLRREQ;
                }
                else if(retry_buffer_req.size() < maxQueueSize){
                    LRSM.Local_state = CXL_Local_state::RETRY_LOCAL_IDLE;
                }
                else{
                    LRSM.Local_state = CXL_Local_state::RETRY_PHY_REINIT;
                }
                DPRINTF(CXL_ctrl,"%s, Local_state = %s, num_retry = %d\n",__func__,LRSM.cur_stat(),LRSM.NUM_RETRY);

                DPRINTF(CXL_ctrl,"%s, crc_check = false, retry_resp = %s\n",__func__,pkt->cxl_pkt.retry_resp? "true":"false");
            // }
        }
    }

    PacketPtr
    CXL_ctrl::make_control_flit_req(PacketPtr pkt){
        // RequestPtr req = std::make_shared<gem5::Request>(
        // pkt->getAddr()+1, pkt->getSize(),0,0);
        //esj 2024-12-25
        // RequestPtr req = std::make_shared<gem5::Request>(
        // pkt->getAddr(), pkt->getSize(),0,0);
        //esj 2024-12-26
        RequestPtr req = std::make_shared<gem5::Request>(
        pkt->getAddr(), pkt->getSize(),0,pkt->cxl_pkt.seqNum);

        // Create request packet
        //esj 2025-02-24
        PacketPtr req_pkt;
        if(pkt->cmd == MemCmd::WriteResp){
            req_pkt = new Packet(req, MemCmd::WriteReq);
        }
        else{
            req_pkt = new Packet(req, MemCmd::ReadReq);

        }
        // PacketPtr req_pkt = new Packet(req, MemCmd::WriteReq);
        ///

        //esj 2025-01-01
        req_pkt->cxl_pkt.packing_size = 16;
        req_pkt->cxl_pkt.flit_num = 1;

        //esj 2024-12-26
        req_pkt->allocate();  // Allocate space for data payload

        //esj 2025-01-03
        req_pkt->cxl_flag = true;

        // Copy CXL-specific information from request
        req_pkt->cxl_pkt = pkt->cxl_pkt;
        req_pkt->cxl_pkt.isReq = true;  // Mark as request
        req_pkt->cxl_pkt.seqNum = pkt->cxl_pkt.seqNum;
        req_pkt->cxl_pkt.is_controlflit = true;

        //esj 2025-07-09
        uint8_t src_host_idx = pkt->source_host_idx;
        if (src_host_idx >= Packet::MaxPciRequesterIds) {
            DPRINTF(CXL_ctrl,
                    "%s, invalid source_host_idx=%d, clamp to %d\n",
                    __func__,
                    src_host_idx,
                    Packet::MaxPciRequesterIds - 1);
            src_host_idx = Packet::MaxPciRequesterIds - 1;
        }
        req_pkt->source_host_idx = src_host_idx;
        req_pkt->is_from_second_response_port = (src_host_idx != 0);

        //esj 2025-02-19
        // req_pkt->cxl_pkt.complete = pkt->cxl_pkt.complete;

        //esj 2025-01-17
        // req_pkt->cxl_pkt.crc_error_resp_seqnum = pkt->cxl_pkt.resp_seqnum;

        //esj 2024-12-18
        req_pkt->cxl_pkt.is_from_device = pkt->cxl_pkt.is_from_device;

        req_pkt->cxl_pkt.retry_req = pkt->cxl_pkt.retry_req;
        req_pkt->cxl_pkt.retry_resp = pkt->cxl_pkt.retry_resp;
        req_pkt->cxl_pkt.retry_ack = pkt->cxl_pkt.retry_ack;
        req_pkt->cxl_pkt.crc_check = pkt->cxl_pkt.crc_check;
        req_pkt->cxl_pkt.crc_data = pkt->cxl_pkt.crc_data;
        req_pkt->cxl_pkt.send_information.sen_packet_num = pkt->cxl_pkt.send_information.sen_packet_num;

        //esj 2024-12-25
        req_pkt->cxl_pkt.req_seqnum = pkt->cxl_pkt.req_seqnum;
        req_pkt->cxl_pkt.resp_seqnum = pkt->cxl_pkt.resp_seqnum;


        DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d,retry_req = %d, retry_resp = %d, crc_check = %d\n",__func__,req_pkt->getAddr(),req_pkt->cxl_pkt.seqNum,req_pkt->cxl_pkt.retry_req,req_pkt->cxl_pkt.retry_resp,req_pkt->cxl_pkt.crc_check);

        return req_pkt;
    }

    PacketPtr
    CXL_ctrl::make_control_flit_resp(PacketPtr pkt){
        //esj 2024-12-25
        // RequestPtr req = std::make_shared<gem5::Request>(
        // pkt->getAddr(), pkt->getSize(),0,0);

        //esj 2024-12-26
        RequestPtr req = std::make_shared<gem5::Request>(
        pkt->getAddr(), pkt->getSize(),0,pkt->cxl_pkt.seqNum);

        // Create response packet
        //esj 2025-07-14
        PacketPtr resp_pkt = new Packet(req, MemCmd::WriteResp);

        // PacketPtr resp_pkt;
        // if(pkt->cmd == MemCmd::WriteReq){
        //     resp_pkt = new Packet(req, MemCmd::WriteResp);
        // }
        // else{
        //     resp_pkt = new Packet(req, MemCmd::ReadResp);
        // }
        //

        //esj 2024-12-26
        resp_pkt->allocate();  // Allocate space for data payload

        //esj 2025-01-01
        resp_pkt->cxl_pkt.packing_size = 16;
        resp_pkt->cxl_pkt.flit_num = 1;

        //esj 2025-01-03
        resp_pkt->cxl_flag = true;

        // Copy CXL-specific information from request
        resp_pkt->cxl_pkt = pkt->cxl_pkt;
        resp_pkt->cxl_pkt.isReq = false;  // Mark as response
        resp_pkt->cxl_pkt.seqNum = pkt->cxl_pkt.seqNum;
        resp_pkt->cxl_pkt.is_controlflit = true;

        //esj 2025-07-09
        uint8_t src_host_idx = pkt->source_host_idx;
        if (src_host_idx >= Packet::MaxPciRequesterIds) {
            DPRINTF(CXL_ctrl,
                    "%s, invalid source_host_idx=%d, clamp to %d\n",
                    __func__,
                    src_host_idx,
                    Packet::MaxPciRequesterIds - 1);
            src_host_idx = Packet::MaxPciRequesterIds - 1;
        }
        resp_pkt->source_host_idx = src_host_idx;
        resp_pkt->is_from_second_response_port = (src_host_idx != 0);

        //esj 2025-02-19
        // resp_pkt->cxl_pkt.complete = pkt->cxl_pkt.complete;

        //esj 2025-01-17
        // resp_pkt->cxl_pkt.crc_error_req_seqnum = pkt->cxl_pkt.req_seqnum;

        //esj 2024-12-18
        resp_pkt->cxl_pkt.is_from_device = pkt->cxl_pkt.is_from_device;

        resp_pkt->cxl_pkt.retry_req = pkt->cxl_pkt.retry_req;
        resp_pkt->cxl_pkt.retry_resp = pkt->cxl_pkt.retry_resp;
        resp_pkt->cxl_pkt.retry_ack = pkt->cxl_pkt.retry_ack;
        resp_pkt->cxl_pkt.crc_check = pkt->cxl_pkt.crc_check;
        resp_pkt->cxl_pkt.crc_data = pkt->cxl_pkt.crc_data;
        resp_pkt->cxl_pkt.send_information.sen_packet_num = pkt->cxl_pkt.send_information.sen_packet_num;

        //esj 2024-12-25
        resp_pkt->cxl_pkt.req_seqnum = pkt->cxl_pkt.req_seqnum;
        resp_pkt->cxl_pkt.resp_seqnum = pkt->cxl_pkt.resp_seqnum;

        DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, retry_req = %d, crc_check = %d\n",__func__,resp_pkt->getAddr(),resp_pkt->cxl_pkt.seqNum,resp_pkt->cxl_pkt.retry_req,resp_pkt->cxl_pkt.crc_check);

        return resp_pkt;
    }




    void
    CXL_ctrl::retry_transmit_req(PacketPtr pkt, Tick when){
        // if (retry_transmitList_req.empty()) {
        //         DPRINTF(CXL_ctrl, "[%s] scheduling control_flit_reqEvent, when = %d, transmitList size %d\n",__func__,when, retry_transmitList_req.size());
        //         schedule(control_flit_reqEvent, when);
        //     }
        DPRINTF(CXL_ctrl, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), retry_transmitList_req.size());

        //esj 2024-12-23
        // assert(retry_transmitList_req.size() != maxQueueSize);

        //esj 2025-01-02
        //esj 2025-01-17
        // check_seqnum_req(pkt->cxl_pkt.seqNum);

        if(pkt->cxl_pkt.retry_resp){
            //esj 2025-01-17
            // retry_transmitList_req.emplace_front(pkt, when);
            DeferredPacket dp(pkt, when);
            int iter = check_seqnum_req(when);
            retry_transmitList_req.insert(retry_transmitList_req.begin()+iter,dp);

            //esj 2025-01-25
            // if(iter == 0){
            //     DPRINTF(CXL_ctrl, "[%s] retransmit, list size = 0, rescheduling control_flit_reqEvent, when = %d\n",__func__,when);
            //     if(this->control_flit_reqEvent.scheduled()){
            //         this->deschedule(control_flit_reqEvent);
            //     }
            //     this->schedule(control_flit_reqEvent, when);
            // }
            //

            //esj 2025-01-04
            // if(this->control_flit_reqEvent.scheduled()){
            //     //esj 2024-12-17
            //     DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_req is scheduled, canceling retry_transmit_req, when = %d\n",__func__,when);
            //     this->deschedule(control_flit_reqEvent);
            //     this->schedule(control_flit_reqEvent, when);
            // }
        }
        else{
            retry_transmitList_req.emplace_back(pkt, when);

        }

        // DPRINTF(CXL_ctrl, "[%s] retry_num(Host --> Device) = %d\n",__func__,retry_transmitList_req.size());
        DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_req = %d, crc_check = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.retry_req,pkt->cxl_pkt.crc_check);


        if(!this->control_flit_reqEvent.scheduled()){
            //esj 2024-12-17
            DPRINTF(CXL_ctrl, "[%s] retransmit, control_flit_reqEvent is not scheduled, scheduling control_flit_reqEvent, when = %d\n",__func__,when);
            this->schedule(control_flit_reqEvent, when);
        }

    }

    void
    CXL_ctrl::retry_transmit_resp(PacketPtr pkt,Tick when){
        // if (retry_transmitList_resp.empty()) {
        //         DPRINTF(CXL_ctrl, "[%s] scheduling retry_transmit_resp, when = %d, transmitList size %d\n",__func__,when, retry_transmitList_resp.size());
        //         schedule(control_flit_respEvent, when);
        //     }
        DPRINTF(CXL_ctrl, "[%s]trySend response addr 0x%x, transmitList size %d, retry_req = %d\n",__func__,pkt->getAddr(), retry_transmitList_resp.size(),pkt->cxl_pkt.retry_req);

        //esj 2024-12-23
        // assert(retry_transmitList_resp.size() != maxQueueSize);

        //esj 2025-01-02
        //esj 2025-01-17
        // check_seqnum_resp(pkt->cxl_pkt.seqNum);

        if(pkt->cxl_pkt.retry_req){
            //esj 2025-01-17
            // retry_transmitList_resp.emplace_front(pkt, when);
            DeferredPacket dp(pkt, when);
            retry_transmitList_resp.insert(retry_transmitList_resp.begin()+check_seqnum_resp(when),dp);

            //esj 2024-12-31
            // print_retry_transmitList_resp();


            //esj 2025-01-04
            // if(this->control_flit_respEvent.scheduled()){
            //     //esj 2024-12-17
            //     DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_resp is scheduled, canceling retry_transmit_resp, when = %d\n",__func__,when);
            //     this->deschedule(control_flit_respEvent);
            //     this->schedule(control_flit_respEvent, when);
            // }
        }
        else{
            retry_transmitList_resp.emplace_back(pkt, when);
            //esj 2024-12-31
            // print_retry_transmitList_resp();

        }


        // DPRINTF(CXL_ctrl, "[%s] retry_num(Device --> Host) = %d\n",__func__,retry_transmitList_resp.size());

        // DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_resp is not scheduled, scheduling retry_transmit_resp, when = %d\n",__func__,when);
        if(!this->control_flit_respEvent.scheduled()){
            //esj 2024-12-17
            DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_resp is not scheduled, scheduling retry_transmit_resp, when = %d\n",__func__,when);
            this->schedule(control_flit_respEvent, when);
        }
    }


    // Create and schedule sequence event
    void
    CXL_ctrl::createSeqEvent(uint64_t seqNum) {
        DPRINTF(CXL_ctrl, "[%s] Creating event for seqNum=%d\n", __func__, seqNum);

        auto old = seqEvents.find(seqNum);
        if (old != seqEvents.end()) {
            if (old->second && old->second->scheduled()) {
                deschedule(*(old->second));
            }
            delete old->second;
            seqEvents.erase(old);
        }

        // Create new event
        auto event = new EventFunctionWrapper(
            [this, seqNum]{ handleSeqTimeout(seqNum); },
            name() + ".seqEvent" + std::to_string(seqNum)
        );

        // Store in map
        seqEvents[seqNum] = event;

        // Schedule event
        schedule(*event, curTick() + CXL_mem_retryTime);
    }

    // Cancel sequence event
    void
    CXL_ctrl::cancelSeqEvent(uint64_t seqNum) {
        // DPRINTF(CXL_ctrl, "[%s] Canceling event for seqNum=%d\n", __func__, seqNum);

        auto it = seqEvents.find(seqNum);
        if (it != seqEvents.end()) {
            if (it->second->scheduled()) {
                deschedule(*(it->second));
            }
            delete it->second;
            seqEvents.erase(it);
        }
    }

    // esj 2025-05-16
    void
    CXL_ctrl::cancelSeqEvent_double(uint64_t seqNum, uint64_t seqNum2) {

        for(auto it = seqEvents.begin(); it != seqEvents.end();){
            if(it->first == seqNum || it->first == seqNum2){
                if (it->second->scheduled()) {
                    DPRINTF(CXL_ctrl, "[%s] canceling event for seqNum=%d\n", __func__, it->first);
                    deschedule(*(it->second));
                }
                delete it->second;
                it = seqEvents.erase(it);
            }
            else{
                it++;
            }
        }
    }


    // Timeout handler
    void
    CXL_ctrl::handleSeqTimeout(uint64_t seqNum) {
        DPRINTF(CXL_ctrl, "[%s] Timeout for seqNum=%d\n", __func__, seqNum);

        DPRINTF(CXL_ctrl, "[%s] START: checking timeout conditions\n", __func__);

		//esj 2024-12-23
        // if (cxl_buffer_host.size() == 0 || cxl_buffer_host.front() == NULL || retransmit == true) {
        //     DPRINTF(CXL_ctrl, "[%s] SKIP: buffer_empty=%d, front_null=%d, retransmit=%d\n",

        //esj 2024-12-31
        // PacketPtr temp = cxl_buffer_host.popByseqnum2(seqNum);
        PacketPtr temp = cxl_buffer_host.popByseqnum5(seqNum);

        //esj 2024-12-25
        if(temp == NULL){
            DPRINTF(CXL_ctrl, "[%s] skip timeout: packet does not exited in buffer\n",__func__);
            cancelSeqEvent(seqNum);
            return;
        }

        //esj 2024-12-25
        if(temp->isResponse()){
            DPRINTF(CXL_ctrl, "[%s] skip timeout: packet is already responsed\n",__func__);
            cancelSeqEvent(seqNum);
            cxl_buffer_host.popByseqnum(seqNum);
            //esj 2025-07-18
            handleSeqTimeout_device(seqNum);
            return;
        }


		if (cxl_buffer_host.size() == 0 || retransmit == true ) {
            DPRINTF(CXL_ctrl, "[%s] SKIP: buffer_empty=%d, retransmit=%d\n",
                    __func__, cxl_buffer_host.size() == 0, retransmit);
                    //esj 2024-12-22
                    cancelSeqEvent(seqNum);
            return;
        }

        //esj 2024-12-23
        // if(temp->isResponse()){
        //     returnPacketInfo(temp);
        // }

        DPRINTF(CXL_ctrl, "[%s] Initiating retransmission\n", __func__);
        // bool success = transmit_cxl(cxl_buffer_host.front());
        //esj 2024-12-22
        // bool success = transmit_cxl(cxl_buffer_host.popByseqnum2(seqNum));
        bool success = transmit_cxl(temp);

		//esj 2024-12-23
        // retransmit = true;
        // retransmitIdx = (success) ? 1 : 0;

        DPRINTF(CXL_ctrl, "[%s] END: retransmission_status=%s, next_idx=%d\n",
                __func__, success ? "success" : "failed", retransmitIdx);

        // Cleanup event
        cancelSeqEvent(seqNum);
    }


    // Create and schedule sequence event
    void
    CXL_ctrl::createSeqEvent_device(uint64_t seqNum) {
        // DPRINTF(CXL_ctrl, "[%s] Creating event for seqNum=%d\n", __func__, seqNum);

        auto old = seqEvents_device.find(seqNum);
        if (old != seqEvents_device.end()) {
            if (old->second && old->second->scheduled()) {
                deschedule(*(old->second));
            }
            delete old->second;
            seqEvents_device.erase(old);
        }

        // Create new event
        auto event = new EventFunctionWrapper(
            [this, seqNum]{ handleSeqTimeout_device(seqNum); },
            name() + ".seqEvent" + std::to_string(seqNum)
        );

        // Store in map
        seqEvents_device[seqNum] = event;

        // Schedule event
        schedule(*event, curTick() + CXL_mem_retryTime);
    }

    // Cancel sequence event
    void
    CXL_ctrl::cancelSeqEvent_device(uint64_t seqNum) {
        // DPRINTF(CXL_ctrl, "[%s] Canceling event for seqNum=%d\n", __func__, seqNum);

        auto it = seqEvents_device.find(seqNum);
        if (it != seqEvents_device.end()) {
            if (it->second->scheduled()) {
                deschedule(*(it->second));
            }
            delete it->second;
            seqEvents_device.erase(it);
        }
    }

    //esj 2025-01-10
    void
    CXL_ctrl::cancelSeqEvent_device2(uint64_t seqNum) {
        // DPRINTF(CXL_ctrl, "[%s] Canceling event for seqNum=%d\n", __func__, seqNum);

        for(auto it = seqEvents_device.begin(); it != seqEvents_device.end();){
            if(it->first <= seqNum){
                if (it->second->scheduled()) {
                    deschedule(*(it->second));
                }
                delete it->second;
                it = seqEvents_device.erase(it);
            }
            else{
                it++;
            }
        }
    }

    // Timeout handler
    void
    CXL_ctrl::handleSeqTimeout_device(uint64_t seqNum) {
        DPRINTF(CXL_ctrl, "[%s] Timeout for seqNum=%d\n", __func__, seqNum);

        DPRINTF(CXL_ctrl, "[%s] START: checking timeout conditions\n", __func__);

        //esj 20224-12-23
        // if (cxl_buffer_device.size() == 0 || cxl_buffer_device.front() == NULL || retransmit == true) {
        //     DPRINTF(CXL_ctrl, "[%s] SKIP: buffer_empty=%d, front_null=%d, retransmit=%d\n",

        //esj 2024-12-31
        // PacketPtr temp = cxl_buffer_device.popByseqnum2(seqNum);
        //esj 2025-01-11
        // PacketPtr temp = cxl_buffer_device.popByseqnum5(seqNum);
        PacketPtr temp = cloneDeviceReplayPacket(seqNum);

        //esj 2025-01-06
        if(temp == NULL){
            DPRINTF(CXL_ctrl, "[%s] skip timeout: packet does not exited in buffer\n",__func__);
            return;
        }

		if (cxl_buffer_device.size() == 0 || retransmit == true || temp->isRequest()) {
            DPRINTF(CXL_ctrl, "[%s] SKIP: buffer_empty=%d, retransmit=%d\n",
                    __func__, cxl_buffer_device.size() == 0,  retransmit);

            //esj 20224-12-22
            cancelSeqEvent_device(seqNum);
            delete temp;
            return;
        }

        DPRINTF(CXL_ctrl, "[%s] Initiating retransmission\n", __func__);
        // bool success = transmit_cxl_device(cxl_buffer_device.front());
        //esj 2024-12-22
        // bool success = transmit_cxl_device(cxl_buffer_device.popByseqnum2(seqNum));
        bool success = transmit_cxl_device(temp);

        if (!success) {
            if (cxl_packet_device == temp)
                cxl_packet_device = nullptr;
            Device2Host_busy = false;
            delete temp;
        }


        // retransmit = true;
        // retransmitIdx = (success) ? 1 : 0;

        DPRINTF(CXL_ctrl, "[%s] END: retransmission_status=%s, next_idx=%d\n",
                __func__, success ? "success" : "failed", retransmitIdx);

        // Cleanup event
        cancelSeqEvent_device(seqNum);
    }


    void
    CXL_ctrl :: CXL_ctrlResponsePort :: recvRespRetry(){
        //esj 2025-05-13
        DPRINTF(CXL_ctrl, "recvRespRetry is called, transmitList_host size = %d\n",ctrl->transmitList_host.size());
        bool success = false;

        //esj 2025-06-27
        // for(int i = 0; i < ctrl->transmitList_host.size(); i++){
        //     PacketPtr pkt = ctrl->transmitList_host.front().pkt;

        //     success = ctrl->internal_Resquest.recvTimingResp(pkt);
        //     if(success){
        //         DPRINTF(CXL_ctrl, "[%s] success, pop_front\n",__func__);
        //         ctrl->transmitList_host.pop_front();
        //     }
        //     //esj 2025-05-14
        //     // else{
        //     //     break;
        //     // }
        // }

        // //esj 2025-05-14
        // // if(success){
        // if(ctrl->transmitList_host.size() == 0){
        //     // warn("recvRespRetry is called, transmitList_host size = %d\n",ctrl->transmitList_host.size());
        //     ctrl->is_host_retry = false;
        //     if(ctrl->cxl_unpacking->transmitList_device.size() > 0){
        //         DPRINTF(CXL_ctrl, "[%s] transmitList_device size = %d\n",__func__,ctrl->cxl_unpacking->transmitList_device.size());
        //         ctrl->cxl_unpacking->getResponsePort("in_port",0).recvRespRetry();
        //     }
        // }

        //esj 2025-07-12
        // for (auto it = ctrl->transmitList_host.begin(); it != ctrl->transmitList_host.end();) {
        //     if (ctrl->transmitList_host.empty()) {
        //         ctrl->is_host_retry = false;
        //         return ;
        //     }

        //     PacketPtr pkt = it->pkt;
        //     success = ctrl->internal_Resquest.recvTimingResp(pkt);
        //     if(success){
        //         DPRINTF(CXL_ctrl, "[%s] success, pop_front\n",__func__);
        //         it = ctrl->transmitList_host.erase(it);
        //         continue;
        //     }
        //     else{
        //         return;
        //     }
        // }
        // if(ctrl->transmitList_host.size() == 0){
        //     ctrl->is_host_retry = false;
        //     if(ctrl->cxl_unpacking->transmitList_device.size() > 0){
        //         DPRINTF(CXL_ctrl, "[%s] transmitList_device size = %d\n",__func__,ctrl->cxl_unpacking->transmitList_device.size());
        //         ctrl->cxl_unpacking->getResponsePort("in_port",0).recvRespRetry();
        //     }
        // }

        if(name().find("upstreamResponse") != std::string::npos){
            ctrl->cxl_unpacking->getResponsePort("in_port",0).recvRespRetry();
        }
        else{
            ctrl->cxl_encoder->getResponsePort("in_port",0).recvRespRetry();
        }

    }

    void
    CXL_ctrl :: CXL_ctrlResponsePort :: recvFunctional(PacketPtr pkt)
    {
        recvTimingReq(pkt);
    }

    Tick
    CXL_ctrl :: CXL_ctrlResponsePort :: recvAtomic(PacketPtr pkt)
    {
        DPRINTF(CXL_ctrl,"%s\n",__func__);
        Tick delay = 0;
        std::string portname = name();
        if (pkt->cxl_flag && pkt->is_cxl_io) {
            if (ctrl->is_host) {
                delay = ctrl->downstreamRequest2.sendAtomic(pkt);
            } else {
                delay = ctrl->upstreamRequest2.sendAtomic(pkt);
            }
        }
        else if(pkt->cxl_flag & pkt->is_cxl_mem){
            if(portname.find("upstreamResponse") != std::string::npos){
                delay = ctrl->downstreamRequest.sendAtomic(pkt);
            }
            else if (portname.find("downstreamResponse") != std::string::npos){
                delay = ctrl->upstreamRequest.sendAtomic(pkt);
            }
        }
        else {
            if(portname.find("upstreamResponse") != std::string::npos){
                delay = ctrl->downstreamRequest.sendAtomic(pkt);
            }
            else if (portname.find("downstreamResponse") != std::string::npos){
                delay = ctrl->upstreamRequest.sendAtomic(pkt);
            }
        }

        return delay;
    }

    //esj 2025-05-08
    void
    CXL_ctrl :: CXL_ctrlRequestPort :: recvReqRetry(){
        DPRINTF(CXL_ctrl,"%s\n",__func__);
        //esj 2025-05-08
        if(name().find("downstreamRequest") != std::string::npos){
            //esj 2025-07-09
            // ctrl->retry_pkt = false;
            // if(ctrl->getsize_retry_transmitList_resp() > 0){
            //     ctrl->control_flit_respfunc();
            // }
            // if(ctrl->transmitList.size() > 0){
            //     ctrl->trySendTiming();
            // }
            DPRINTF(CXL_ctrl, "[%s] recvReqRetry is called\n",__func__);
            ctrl->cxl_encoder->getRequestPort("out_port",0).recvReqRetry();
        }
        //esj 2025-06-29`
        else{
            ctrl->cxl_unpacking->getRequestPort("out_port",0).recvReqRetry();
        }
    }


    CXL_ctrl::CXL_ctrlRequestPort&
    CXL_ctrl::getRequestPort(const std::string &if_name, PortID idx){
        DPRINTF(CXL_ctrl, "getRequestPort is called %s\n",if_name);
        if (if_name == "downstreamRequest") {
            return downstreamRequest;
        } else if (if_name == "upstreamRequest") {
            return upstreamRequest ;
        } else if(if_name == "internal_Resquest"){
            return internal_Resquest ;
        } else if (if_name == "pcie_Resquest"){
            return pcie_Resquest;
        }
        //esj 2025-06-22
        else if (if_name == "upstreamRequest2") {
            return upstreamRequest2 ;
        }
        else if (if_name == "downstreamRequest2") {
            return downstreamRequest2 ;
        }

        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }
    CXL_ctrl::CXL_ctrlResponsePort&
    CXL_ctrl::getResponsePort(const std::string &if_name, PortID idx)
    {
        DPRINTF(CXL_ctrl, "getResponsePort is called %s\n",if_name);
        if (if_name == "upstreamResponse") {
            return upstreamResponse;
        } else if (if_name == "downstreamResponse") {
            return downstreamResponse ;
        } else if (if_name == "internal_Response") {
            return internal_Response ;
        } else if (if_name == "pcie_Response"){
            return pcie_Response;
        }
        //esj 2025-22
        else if (if_name == "downstreamResponse2") {
            return downstreamResponse2 ;
        }
        else if (if_name == "upstreamResponse2") {
            return upstreamResponse2 ;
        }
        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }

    Port & CXL_ctrl::getPort(const std::string &if_name, PortID idx)
    {
        DPRINTF(CXL_ctrl, "getPort is called %s\n",if_name);
        if (if_name == "downstreamRequest") {
            return downstreamRequest;
        } else if (if_name == "upstreamRequest") {
            return upstreamRequest ;
        } else if (if_name == "upstreamResponse") {
            return upstreamResponse;
        } else if (if_name == "downstreamResponse") {
            return downstreamResponse ;
        } else if (if_name == "internal_Response") {
            return internal_Response ;
        } else if (if_name == "internal_Resquest") {
            return internal_Resquest ;
        } else if (if_name == "pcie_Response"){
            return pcie_Response;
        } else if (if_name == "pcie_Resquest"){
            return pcie_Resquest;
        }
        //esj 2025-22
        else if (if_name == "downstreamResponse2") {
            return downstreamResponse2 ;
        }
        else if (if_name == "upstreamRequest2") {
            return upstreamRequest2 ;
        }
        else if (if_name == "downstreamRequest2") {
            return downstreamRequest2 ;
        }
        else if (if_name == "upstreamResponse2") {
            return upstreamResponse2 ;
        }

        else
            fatal("%s does not have any port named %s\n", name(), if_name);
    }




}
