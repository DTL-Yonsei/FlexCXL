#include "dev/CXL/CXL_ctrl.hh"
#include "dev/CXL/CXL_CRC.hh"
#include "dev/CXL/CXL_Decoder.hh"
#include "dev/CXL/CXL_Deframer.hh"
#include "dev/CXL/CXL_Encoder.hh"
#include "dev/CXL/CXL_FlexBus.hh"
#include "dev/CXL/CXL_Framer.hh"
#include "dev/CXL/CXL_Packing.hh"
#include "dev/CXL/CXL_Unpacking.hh"


#include <cmath>
#include "base/random.hh"
#include "base/trace.hh"

#include "base/inifile.hh"
#include "base/intmath.hh"
#include "base/str.hh"
#include "debug/CXL_ctrl.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include "sim/protocol_validation_logger.hh"

namespace gem5
{

    namespace
    {

    class ReplayBufferMutationGuard
    {
      public:
        explicit ReplayBufferMutationGuard(cxl_ReplayBuffer &buffer)
            : buffer(buffer), oldSize(buffer.queue.size())
        {}

        ~ReplayBufferMutationGuard()
        {
            buffer.notifyOccupancyIfChanged(oldSize);
        }

      private:
        cxl_ReplayBuffer &buffer;
        const size_t oldSize;
    };

    } // anonymous namespace

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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
        ReplayBufferMutationGuard occupancy_guard(*this);

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
                DPRINTF(CXL_ctrl, "[%s] %dth packet is null, is control flit = %s, seqnum = %d\n", __func__, count++, (*it)->cxl_pkt.is_controlflit? "true":"false", (*it)->cxl_pkt.seqNum);
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                //esj 2025-07-14
                DPRINTF(CXL_ctrl, "[%s] %dth packet is invalid, is control flit = %s, seqnum = %d\n", __func__, count++, (*it)->cxl_pkt.is_controlflit? "true":"false", (*it)->cxl_pkt.seqNum);
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
        ReplayBufferMutationGuard occupancy_guard(*this);
    
        
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
                DPRINTF(CXL_ctrl, "[%s], invalid adddr, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-05-12    
                PacketPtr packet = *it;
                if(packet->cxl_pkt.is_controlflit){
                    delete packet;
                }
                // 
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-05-12
                PacketPtr packet = *it;
                if(packet->cxl_pkt.is_controlflit){
                    delete packet;
                }
                // 
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                continue;
            }
            //esj 2025-01-18
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                //esj 2025-05-12
                PacketPtr packet = *it;
                if(packet->cxl_pkt.is_controlflit){
                    delete packet;
                }
                // 
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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
        
        int count = 0;
        for (auto it = queue.begin(); it != queue.end();) {
            if (queue.empty()) {
                return false;
            }
            if (*it == NULL) {
                DPRINTF(CXL_ctrl, "[%s], null\n",__func__);
                it = queue.erase(it);
                count++;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000100)) {
                DPRINTF(CXL_ctrl, "[%s], invalid adddr, # = %d, seq = %d\n",__func__, count, (*it)->cxl_pkt.seqNum);
                //esj 2025-05-12    
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // 
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                count++;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size, # = %d, seq = %d\n",__func__, count, (*it)->cxl_pkt.seqNum);
                //esj 2025-05-12
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // 
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;

                count++;
                continue;
            }
            //esj 2025-01-18
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
                DPRINTF(CXL_ctrl, "[%s], invalid seqnum, # = %d, seq = %d\n",__func__, count, (*it)->cxl_pkt.seqNum);
                //esj 2025-05-12
                // PacketPtr packet = *it;
                // if(packet->cxl_pkt.is_controlflit){
                //     delete packet;
                // }
                // 
                it = queue.erase(it);
                if(buffer_size > 0)
                    --buffer_size;
                count++;
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

                    DPRINTF(CXL_ctrl, "[%s], erase seqnum = %d, # = %d\n",__func__,(*it)->cxl_pkt.seqNum, count);
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
        ReplayBufferMutationGuard occupancy_guard(*this);
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
            //esj 2025-01-18
            DPRINTF(CXL_ctrl, "[%s] %dth packet, seqnum = %d\n", __func__, count, (*it)->cxl_pkt.seqNum);

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
                DPRINTF(CXL_ctrl, "[%s], invalid adddr, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                ++it;
                continue;
            }
            else if((*it)->cxl_pkt.is_controlflit){
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
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
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
            else if((*it)->cxl_pkt.complete){
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
        ReplayBufferMutationGuard occupancy_guard(*this);

        DPRINTF(CXL_ctrl, "[%s] queue size = %d, seq_num = %d\n", __func__, queue.size(), seq_num);

        if (queue.empty()) {
            DPRINTF(CXL_ctrl, "[%s], empty\n",__func__);
            return NULL;
        }
        //esj 2025-01-18
        DPRINTF(CXL_ctrl, "[%s] find start\n", __func__);
        int count = 0;

        
        for (auto it = queue.begin(); it != queue.end();) {
            //esj 2025-01-18
            DPRINTF(CXL_ctrl, "[%s] %dth packet, seqnum = %d\n", __func__, count, (*it)->cxl_pkt.seqNum);

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
                DPRINTF(CXL_ctrl, "[%s], invalid adddr, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                ++it;
                continue;
            }
            else if (!((*it)->getflags() & 0x00000200)) {
                DPRINTF(CXL_ctrl, "[%s], invalid size, seqnum = %d\n",__func__,(*it)->cxl_pkt.seqNum);
                ++it;
                continue;
            }
            else if((*it)->cxl_pkt.is_controlflit){
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
            else if((*it)->cxl_pkt.seqNum == 0 && !(*it)->cxl_pkt.start){
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
            else if((*it)->cxl_pkt.complete){
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
        ReplayBufferMutationGuard occupancy_guard(*this);
        DPRINTF(CXL_ctrl, "[%s] queue size = %d\n", __func__, queue.size());

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
    cxl_ReplayBuffer::peekByseqnum(uint64_t seq_num) const
    {
        for (const PacketPtr packet : queue) {
            if (packet == nullptr ||
                !(packet->getflags() & 0x00000100) ||
                packet->cxl_pkt.is_controlflit) {
                continue;
            }
            if (packet->cxl_pkt.seqNum == seq_num)
                return packet;
        }
        return nullptr;
    }

    // Historical lookup that also removes invalid/control entries. Do not use
    // it for validation observation; use peekByseqnum() instead.
    PacketPtr
    cxl_ReplayBuffer :: popByseqnum5(uint64_t seq_num) {
        ReplayBufferMutationGuard occupancy_guard(*this);
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
                ++it;
                continue;
            }
            else if (!((*it).pkt->getflags() & 0x00000100)) {
                // //esj 2025-01-03
                // it = retry_transmitList_req.erase(it);
                ++it;
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
    CXL_ctrl::popfromtransmitlist_resp(
        uint64_t seq_num, bool carried_by_data) {
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
                        const bool is_nak = !it->pkt->cxl_pkt.crc_check;
                        if (carried_by_data) {
                            if (is_nak)
                                ++stats.nak_piggyback_count;
                            else
                                ++stats.ack_piggyback_count;
                            ++stats.cft_cancel_count;
                            ProtocolValidationEvent piggyback;
                            piggyback.event = is_nak ?
                                "NAK_TX" : "ACK_PIGGYBACK";
                            piggyback.component = name();
                            piggyback.layer = "CXL_PROTOCOL";
                            piggyback.direction = "D2H";
                            piggyback.packetSeq = seq_num;
                            if (is_nak) {
                                piggyback.nakSeq = seq_num;
                                piggyback.reason = "CRC_NAK_PIGGYBACK";
                            } else {
                                piggyback.ackSeq = seq_num;
                                piggyback.reason = "DATA_PIGGYBACK";
                            }
                            piggyback.timerId =
                                name() + "-resp-" + std::to_string(seq_num);
                            ProtocolValidationLogger::recordPacket(
                                piggyback, it->pkt);

                            ProtocolValidationEvent canceled = piggyback;
                            canceled.event = "CFT_CANCEL";
                            ProtocolValidationLogger::recordPacket(
                                canceled, it->pkt);
                        } else {
                            ++stats.cft_non_piggyback_cancel_count;
                            ProtocolValidationEvent canceled;
                            canceled.event = "CFT_CANCEL";
                            canceled.component = name();
                            canceled.layer = "CXL_PROTOCOL";
                            canceled.direction = "D2H";
                            canceled.packetSeq = seq_num;
                            canceled.timerId =
                                name() + "-resp-" + std::to_string(seq_num);
                            canceled.reason = "CONTROL_STATE_REPLACED";
                            ProtocolValidationLogger::recordPacket(
                                canceled, it->pkt);
                        }

                        //esj 2025-01-05
                        it->valid =false;

                        //esj 2025-02-17
                        // if(it->pkt->cxl_pkt.is_controlflit){
                        //     delete it->pkt;
                        // }
                        //

                        retry_transmitList_resp.erase(it);
                        syncStrictCftRespEvent();
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
    CXL_ctrl::popfromtransmitlist_req(
        uint64_t seq_num, bool carried_by_data) {
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
                        const bool is_nak = !it->pkt->cxl_pkt.crc_check;
                        if (carried_by_data) {
                            if (is_nak)
                                ++stats.nak_piggyback_count;
                            else
                                ++stats.ack_piggyback_count;
                            ++stats.cft_cancel_count;
                            ProtocolValidationEvent piggyback;
                            piggyback.event = is_nak ?
                                "NAK_TX" : "ACK_PIGGYBACK";
                            piggyback.component = name();
                            piggyback.layer = "CXL_PROTOCOL";
                            piggyback.direction = "H2D";
                            piggyback.packetSeq = seq_num;
                            if (is_nak) {
                                piggyback.nakSeq = seq_num;
                                piggyback.reason = "CRC_NAK_PIGGYBACK";
                            } else {
                                piggyback.ackSeq = seq_num;
                                piggyback.reason = "DATA_PIGGYBACK";
                            }
                            piggyback.timerId =
                                name() + "-req-" + std::to_string(seq_num);
                            ProtocolValidationLogger::recordPacket(
                                piggyback, it->pkt);

                            ProtocolValidationEvent canceled = piggyback;
                            canceled.event = "CFT_CANCEL";
                            ProtocolValidationLogger::recordPacket(
                                canceled, it->pkt);
                        } else {
                            ++stats.cft_non_piggyback_cancel_count;
                            ProtocolValidationEvent canceled;
                            canceled.event = "CFT_CANCEL";
                            canceled.component = name();
                            canceled.layer = "CXL_PROTOCOL";
                            canceled.direction = "H2D";
                            canceled.packetSeq = seq_num;
                            canceled.timerId =
                                name() + "-req-" + std::to_string(seq_num);
                            canceled.reason = "CONTROL_STATE_REPLACED";
                            ProtocolValidationLogger::recordPacket(
                                canceled, it->pkt);
                        }

                        //esj 2025-01-05
                        it->valid =false;

                        //esj 2025-01-10
                        delete it->pkt;
                        //

                        retry_transmitList_req.erase(it);
                        syncStrictCftReqEvent();
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
        ,ADD_STAT(replay_buffer,
                  "Replay occupancy sampled at admission attempts")
        ,ADD_STAT(replay_buffer_without_zero,
                  "Nonzero replay occupancy sampled at admission attempts")
        ,ADD_STAT(stall_req_count, "Number of stall request")//esj 2025-11-21
        ,ADD_STAT(stall_resp_count, "Number of stall response")
        ,ADD_STAT(stall_req_retry_count, "Number of stall request retry")
        ,ADD_STAT(stall_resp_retry_count, "Number of stall response retry")
        ,ADD_STAT(crc_error_count, "Number of crc error") //esj 2025-11-29
        ,ADD_STAT(ack_control_flit_count, "Number of retry ACK control flits")
        ,ADD_STAT(nak_control_flit_count, "Number of retry NAK control flits")
        ,ADD_STAT(retry_req_count, "Number of retry_req control flits")
        ,ADD_STAT(retry_resp_count, "Number of retry_resp control flits")
        ,ADD_STAT(retry_ack_count, "Number of retry_ack control flits")
        ,ADD_STAT(retransmit_req_count, "Number of queued request retransmits")
        ,ADD_STAT(retransmit_resp_count,
                  "Number of queued response retransmits")
        ,ADD_STAT(seq_timeout_req_count, "Number of request sequence timeouts")
        ,ADD_STAT(seq_timeout_resp_count,
                  "Number of response sequence timeouts")
        ,ADD_STAT(replay_buffer_high_watermark,
                  "Largest observed CXL replay buffer occupancy")
        ,ADD_STAT(ack_rx_count,
                  "Exact-sequence ACKs received in both directions")
        ,ADD_STAT(ack_rx_h2d_count,
                  "Exact-sequence ACKs received in the H2D direction")
        ,ADD_STAT(ack_rx_d2h_count,
                  "Exact-sequence ACKs received in the D2H direction")
        ,ADD_STAT(nak_rx_count, "NAKs received in both directions")
        ,ADD_STAT(nak_rx_h2d_count, "NAKs received in the H2D direction")
        ,ADD_STAT(nak_rx_d2h_count, "NAKs received in the D2H direction")
        ,ADD_STAT(ack_piggyback_count,
                  "ACK information carried by a data response/request")
        ,ADD_STAT(nak_piggyback_count,
                  "NAK information carried by a data response/request")
        ,ADD_STAT(standalone_ack_tx_count,
                  "Standalone ACK control flits transmitted")
        ,ADD_STAT(nak_tx_count, "NAK control flits transmitted")
        ,ADD_STAT(control_flit_tx_count,
                  "All standalone ACK/NAK control flits transmitted")
        ,ADD_STAT(cft_arm_count, "Control-flit timers armed")
        ,ADD_STAT(cft_cancel_count,
                  "Control-flit timers canceled by data-packet piggyback")
        ,ADD_STAT(cft_non_piggyback_cancel_count,
                  "Control-flit timers removed by control-state replacement")
        ,ADD_STAT(cft_expiry_count,
                  "Expired control-flit timers that transmitted a flit")
        ,ADD_STAT(exact_retry_req_tx_count,
                  "Actual exact-sequence request retries transmitted")
        ,ADD_STAT(exact_retry_resp_tx_count,
                  "Actual exact-sequence response retries transmitted")
        ,ADD_STAT(replay_admission_stall_episodes,
                  "Host replay-buffer admission-stall episodes")
        ,ADD_STAT(replay_admission_stall_ticks,
                  "Closed replay admission-stall duration in ticks")
        ,ADD_STAT(replay_admission_stall_open,
                  "Replay admission-stall episode open at stats dump")
        ,ADD_STAT(host_replay_current,
                  "Current host request replay-buffer occupancy")
        ,ADD_STAT(device_replay_current,
                  "Current device response replay-buffer occupancy")
        ,ADD_STAT(host_replay_max,
                  "Maximum H2D host replay occupancy since stats reset")
        ,ADD_STAT(device_replay_max,
                  "Maximum D2H device replay occupancy since stats reset")
        ,ADD_STAT(host_replay_occupancy_ticks,
                  "H2D replay occupancy integral in entry-ticks")
        ,ADD_STAT(device_replay_occupancy_ticks,
                  "D2H replay occupancy integral in entry-ticks")
        ,ADD_STAT(host_replay_average,
                  "Time-average H2D replay occupancy since stats reset")
        ,ADD_STAT(device_replay_average,
                  "Time-average D2H replay occupancy since stats reset")
        ,ADD_STAT(host_replay_carry_in_at_reset,
                  "H2D replay entries present when stats were reset")
        ,ADD_STAT(device_replay_carry_in_at_reset,
                  "D2H replay entries present when stats were reset")
        ,ADD_STAT(cft_pending_current,
                  "Current valid control-flit timers in both directions")
        ,ADD_STAT(cft_h2d_pending_current,
                  "Current valid H2D control-flit timers")
        ,ADD_STAT(cft_d2h_pending_current,
                  "Current valid D2H control-flit timers")
        ,ADD_STAT(cft_pending_carry_in_at_reset,
                  "Valid control-flit timers present at stats reset")
        ,ADD_STAT(cft_h2d_pending_carry_in_at_reset,
                  "Valid H2D control-flit timers present at stats reset")
        ,ADD_STAT(cft_d2h_pending_carry_in_at_reset,
                  "Valid D2H control-flit timers present at stats reset")
        ,ADD_STAT(admission_stall_host_replay_count,
                  "H2D admissions rejected by replay occupancy")
        ,ADD_STAT(admission_stall_host_tx_queue_count,
                  "H2D admissions rejected by the transmit queue")
        ,ADD_STAT(admission_stall_host_recv_window_count,
                  "H2D admissions rejected by the receive window")
        ,ADD_STAT(admission_stall_device_replay_count,
                  "D2H admissions rejected by replay occupancy")
        ,ADD_STAT(admission_stall_device_tx_queue_count,
                  "D2H admissions rejected by the transmit queue")
        ,ADD_STAT(admission_stall_read_reservation_count,
                  "H2D read admissions rejected by reserved-read capacity")
        ,ADD_STAT(host_outstanding_window_current,
                  "Current H2D controller outstanding-window occupancy")
        ,ADD_STAT(host_outstanding_window_max,
                  "Maximum H2D controller outstanding-window occupancy")
        ,ADD_STAT(host_outstanding_window_occupancy_ticks,
                  "H2D outstanding-window occupancy integral in entry-ticks")
        ,ADD_STAT(host_outstanding_window_average,
                  "Time-average H2D outstanding-window occupancy")
        ,ADD_STAT(host_outstanding_window_carry_in_at_reset,
                  "H2D outstanding entries present at stats reset")
        ,ADD_STAT(host_outstanding_window_stall_episodes,
                  "H2D outstanding-window-full stall episodes")
        ,ADD_STAT(host_outstanding_window_stall_ticks,
                  "Closed H2D outstanding-window-full stall duration")
        ,ADD_STAT(host_outstanding_window_stall_open,
                  "H2D outstanding-window stall open at stats dump")
        ,ADD_STAT(host_deferred_tx_current,
                  "Current host H2D deferred transmit queue occupancy")
        ,ADD_STAT(host_deferred_tx_max,
                  "Maximum host H2D deferred transmit queue occupancy")
        ,ADD_STAT(host_deferred_tx_occupancy_ticks,
                  "Host H2D deferred transmit occupancy integral in entry-ticks")
        ,ADD_STAT(host_deferred_tx_average,
                  "Time-average host H2D deferred transmit occupancy")
        ,ADD_STAT(host_deferred_tx_carry_in_at_reset,
                  "Host H2D deferred transmit entries present at stats reset")
    {
        control_flit_req_read.reset();
        control_flit_req_write.reset();
        control_flit_resp_read.reset();
        control_flit_resp_write.reset();
        // replay_buffer.reset(); //esj 2025-09-09
        
        //esj 2025-11-21
        stall_req_count.reset();
        stall_resp_count.reset();
        stall_req_retry_count.reset();
        stall_resp_retry_count.reset();
        //esj 2025-11-29
        crc_error_count.reset();
        ack_control_flit_count.reset();
        nak_control_flit_count.reset();
        retry_req_count.reset();
        retry_resp_count.reset();
        retry_ack_count.reset();
        retransmit_req_count.reset();
        retransmit_resp_count.reset();
        seq_timeout_req_count.reset();
        seq_timeout_resp_count.reset();
        replay_buffer_high_watermark.reset();
        ack_rx_count.reset();
        ack_rx_h2d_count.reset();
        ack_rx_d2h_count.reset();
        nak_rx_count.reset();
        nak_rx_h2d_count.reset();
        nak_rx_d2h_count.reset();
        ack_piggyback_count.reset();
        nak_piggyback_count.reset();
        standalone_ack_tx_count.reset();
        nak_tx_count.reset();
        control_flit_tx_count.reset();
        cft_arm_count.reset();
        cft_cancel_count.reset();
        cft_non_piggyback_cancel_count.reset();
        cft_expiry_count.reset();
        exact_retry_req_tx_count.reset();
        exact_retry_resp_tx_count.reset();
        replay_admission_stall_episodes.reset();
        replay_admission_stall_ticks.reset();
        replay_admission_stall_open.reset();
        host_replay_current.reset();
        device_replay_current.reset();
        host_replay_max.reset();
        device_replay_max.reset();
        host_replay_occupancy_ticks.reset();
        device_replay_occupancy_ticks.reset();
        host_replay_carry_in_at_reset.reset();
        device_replay_carry_in_at_reset.reset();
        cft_pending_current.reset();
        cft_h2d_pending_current.reset();
        cft_d2h_pending_current.reset();
        cft_pending_carry_in_at_reset.reset();
        cft_h2d_pending_carry_in_at_reset.reset();
        cft_d2h_pending_carry_in_at_reset.reset();
        admission_stall_host_replay_count.reset();
        admission_stall_host_tx_queue_count.reset();
        admission_stall_host_recv_window_count.reset();
        admission_stall_device_replay_count.reset();
        admission_stall_device_tx_queue_count.reset();
        admission_stall_read_reservation_count.reset();

        host_outstanding_window_current.reset();
        host_outstanding_window_max.reset();
        host_outstanding_window_occupancy_ticks.reset();
        host_outstanding_window_carry_in_at_reset.reset();
        host_outstanding_window_stall_episodes.reset();
        host_outstanding_window_stall_ticks.reset();
        host_outstanding_window_stall_open.reset();
        host_deferred_tx_current.reset();
        host_deferred_tx_max.reset();
        host_deferred_tx_occupancy_ticks.reset();
        host_deferred_tx_carry_in_at_reset.reset();

        host_replay_average = host_replay_occupancy_ticks / simTicks;
        device_replay_average = device_replay_occupancy_ticks / simTicks;
        host_outstanding_window_average =
            host_outstanding_window_occupancy_ticks / simTicks;
        host_deferred_tx_average =
            host_deferred_tx_occupancy_ticks / simTicks;
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



    // }
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

        //esj 2025-09-09
        replay_buffer
        .init(ctrl.maxQueueSize)
        .flags(statistics::nozero | statistics::pdf | statistics::oneline);

        replay_buffer_size
        .init(ctrl.maxQueueSize);

        replay_buffer_without_zero
        .init(ctrl.maxQueueSize)
        .flags(statistics::nozero | statistics::pdf | statistics::oneline);

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

        //esj 2025-11-29
        crc_error_count
        .name(ctrl.name() + ".crc_error_count")
        .flags(statistics::nozero)
        ;

        ack_control_flit_count
        .name(ctrl.name() + ".ack_control_flit_count")
        ;

        nak_control_flit_count
        .name(ctrl.name() + ".nak_control_flit_count")
        ;

        retry_req_count
        .name(ctrl.name() + ".retry_req_count")
        ;

        retry_resp_count
        .name(ctrl.name() + ".retry_resp_count")
        ;

        retry_ack_count
        .name(ctrl.name() + ".retry_ack_count")
        ;

        retransmit_req_count
        .name(ctrl.name() + ".retransmit_req_count")
        ;

        retransmit_resp_count
        .name(ctrl.name() + ".retransmit_resp_count")
        ;

        seq_timeout_req_count
        .name(ctrl.name() + ".seq_timeout_req_count")
        ;

        seq_timeout_resp_count
        .name(ctrl.name() + ".seq_timeout_resp_count")
        ;

        replay_buffer_high_watermark
        .name(ctrl.name() + ".replay_buffer_high_watermark")
        ;

        ack_rx_count.name(ctrl.name() + ".ack_rx_count");
        ack_rx_h2d_count.name(ctrl.name() + ".ack_rx_h2d_count");
        ack_rx_d2h_count.name(ctrl.name() + ".ack_rx_d2h_count");
        nak_rx_count.name(ctrl.name() + ".nak_rx_count");
        nak_rx_h2d_count.name(ctrl.name() + ".nak_rx_h2d_count");
        nak_rx_d2h_count.name(ctrl.name() + ".nak_rx_d2h_count");
        ack_piggyback_count.name(ctrl.name() + ".ack_piggyback_count");
        nak_piggyback_count.name(ctrl.name() + ".nak_piggyback_count");
        standalone_ack_tx_count.name(
            ctrl.name() + ".standalone_ack_tx_count");
        nak_tx_count.name(ctrl.name() + ".nak_tx_count");
        control_flit_tx_count.name(ctrl.name() + ".control_flit_tx_count");
        cft_arm_count.name(ctrl.name() + ".cft_arm_count");
        cft_cancel_count.name(ctrl.name() + ".cft_cancel_count");
        cft_non_piggyback_cancel_count.name(
            ctrl.name() + ".cft_non_piggyback_cancel_count");
        cft_expiry_count.name(ctrl.name() + ".cft_expiry_count");
        exact_retry_req_tx_count.name(
            ctrl.name() + ".exact_retry_req_tx_count");
        exact_retry_resp_tx_count.name(
            ctrl.name() + ".exact_retry_resp_tx_count");
        replay_admission_stall_episodes.name(
            ctrl.name() + ".replay_admission_stall_episodes");
        replay_admission_stall_ticks.name(
            ctrl.name() + ".replay_admission_stall_ticks");
        replay_admission_stall_open.name(
            ctrl.name() + ".replay_admission_stall_open");
        host_replay_current.name(ctrl.name() + ".host_replay_current");
        device_replay_current.name(ctrl.name() + ".device_replay_current");
        host_replay_max.name(ctrl.name() + ".host_replay_max");
        device_replay_max.name(ctrl.name() + ".device_replay_max");
        host_replay_occupancy_ticks.name(
            ctrl.name() + ".host_replay_occupancy_ticks");
        device_replay_occupancy_ticks.name(
            ctrl.name() + ".device_replay_occupancy_ticks");
        host_replay_average
            .name(ctrl.name() + ".host_replay_average")
            .flags(statistics::nonan);
        device_replay_average
            .name(ctrl.name() + ".device_replay_average")
            .flags(statistics::nonan);
        host_replay_carry_in_at_reset.name(
            ctrl.name() + ".host_replay_carry_in_at_reset");
        device_replay_carry_in_at_reset.name(
            ctrl.name() + ".device_replay_carry_in_at_reset");
        cft_pending_current.name(ctrl.name() + ".cft_pending_current");
        cft_h2d_pending_current.name(
            ctrl.name() + ".cft_h2d_pending_current");
        cft_d2h_pending_current.name(
            ctrl.name() + ".cft_d2h_pending_current");
        cft_pending_carry_in_at_reset.name(
            ctrl.name() + ".cft_pending_carry_in_at_reset");
        cft_h2d_pending_carry_in_at_reset.name(
            ctrl.name() + ".cft_h2d_pending_carry_in_at_reset");
        cft_d2h_pending_carry_in_at_reset.name(
            ctrl.name() + ".cft_d2h_pending_carry_in_at_reset");
        admission_stall_host_replay_count.name(
            ctrl.name() + ".admission_stall_host_replay_count");
        admission_stall_host_tx_queue_count.name(
            ctrl.name() + ".admission_stall_host_tx_queue_count");
        admission_stall_host_recv_window_count.name(
            ctrl.name() + ".admission_stall_host_recv_window_count");
        admission_stall_device_replay_count.name(
            ctrl.name() + ".admission_stall_device_replay_count");
        admission_stall_device_tx_queue_count.name(
            ctrl.name() + ".admission_stall_device_tx_queue_count");
        admission_stall_read_reservation_count.name(
            ctrl.name() + ".admission_stall_read_reservation_count");
        host_outstanding_window_current.name(
            ctrl.name() + ".host_outstanding_window_current");
        host_outstanding_window_max.name(
            ctrl.name() + ".host_outstanding_window_max");
        host_outstanding_window_occupancy_ticks.name(
            ctrl.name() + ".host_outstanding_window_occupancy_ticks");
        host_outstanding_window_average
            .name(ctrl.name() + ".host_outstanding_window_average")
            .flags(statistics::nonan);
        host_outstanding_window_carry_in_at_reset.name(
            ctrl.name() + ".host_outstanding_window_carry_in_at_reset");
        host_outstanding_window_stall_episodes.name(
            ctrl.name() + ".host_outstanding_window_stall_episodes");
        host_outstanding_window_stall_ticks.name(
            ctrl.name() + ".host_outstanding_window_stall_ticks");
        host_outstanding_window_stall_open.name(
            ctrl.name() + ".host_outstanding_window_stall_open");
        host_deferred_tx_current.name(
            ctrl.name() + ".host_deferred_tx_current");
        host_deferred_tx_max.name(
            ctrl.name() + ".host_deferred_tx_max");
        host_deferred_tx_occupancy_ticks.name(
            ctrl.name() + ".host_deferred_tx_occupancy_ticks");
        host_deferred_tx_average
            .name(ctrl.name() + ".host_deferred_tx_average")
            .flags(statistics::nonan);
        host_deferred_tx_carry_in_at_reset.name(
            ctrl.name() + ".host_deferred_tx_carry_in_at_reset");
    }

    void CXL_ctrl::CXLStats::resetStats() {
        statistics::Group::resetStats();

        control_flit_req_read.reset();
        control_flit_req_write.reset();
        control_flit_resp_read.reset();
        control_flit_resp_write.reset();

        //esj 2025-11-21
        stall_req_count.reset();
        stall_resp_count.reset();
        stall_req_retry_count.reset();
        stall_resp_retry_count.reset();

        //esj 2025-11-29
        crc_error_count.reset();
        ack_control_flit_count.reset();
        nak_control_flit_count.reset();
        retry_req_count.reset();
        retry_resp_count.reset();
        retry_ack_count.reset();
        retransmit_req_count.reset();
        retransmit_resp_count.reset();
        seq_timeout_req_count.reset();
        seq_timeout_resp_count.reset();
        replay_buffer_high_watermark.reset();
        ctrl.replayBufferHighWatermark = std::max(
            static_cast<uint64_t>(ctrl.cxl_buffer_host.size()),
            static_cast<uint64_t>(ctrl.cxl_buffer_device.size()));
        ack_rx_count.reset();
        ack_rx_h2d_count.reset();
        ack_rx_d2h_count.reset();
        nak_rx_count.reset();
        nak_rx_h2d_count.reset();
        nak_rx_d2h_count.reset();
        ack_piggyback_count.reset();
        nak_piggyback_count.reset();
        standalone_ack_tx_count.reset();
        nak_tx_count.reset();
        control_flit_tx_count.reset();
        cft_arm_count.reset();
        cft_cancel_count.reset();
        cft_non_piggyback_cancel_count.reset();
        cft_expiry_count.reset();
        exact_retry_req_tx_count.reset();
        exact_retry_resp_tx_count.reset();
        replay_admission_stall_episodes.reset();
        replay_admission_stall_ticks.reset();
        replay_admission_stall_open.reset();
        host_replay_current.reset();
        device_replay_current.reset();
        host_replay_max.reset();
        device_replay_max.reset();
        host_replay_occupancy_ticks.reset();
        device_replay_occupancy_ticks.reset();
        host_replay_carry_in_at_reset.reset();
        device_replay_carry_in_at_reset.reset();
        cft_pending_current.reset();
        cft_h2d_pending_current.reset();
        cft_d2h_pending_current.reset();
        cft_pending_carry_in_at_reset.reset();
        cft_h2d_pending_carry_in_at_reset.reset();
        cft_d2h_pending_carry_in_at_reset.reset();
        admission_stall_host_replay_count.reset();
        admission_stall_host_tx_queue_count.reset();
        admission_stall_host_recv_window_count.reset();
        admission_stall_device_replay_count.reset();
        admission_stall_device_tx_queue_count.reset();
        admission_stall_read_reservation_count.reset();

        ctrl.resetReplayOccupancyTracking();
        host_outstanding_window_current.reset();
        host_outstanding_window_max.reset();
        host_outstanding_window_occupancy_ticks.reset();
        host_outstanding_window_carry_in_at_reset.reset();
        host_outstanding_window_stall_episodes.reset();
        host_outstanding_window_stall_ticks.reset();
        host_outstanding_window_stall_open.reset();
        host_deferred_tx_current.reset();
        host_deferred_tx_max.reset();
        host_deferred_tx_occupancy_ticks.reset();
        host_deferred_tx_carry_in_at_reset.reset();
        ctrl.resetHostOutstandingWindowTracking();
        ctrl.resetHostDeferredTxTracking();
        const uint64_t cft_h2d = ctrl.cftReqPending();
        const uint64_t cft_d2h = ctrl.cftRespPending();
        cft_h2d_pending_current = cft_h2d;
        cft_d2h_pending_current = cft_d2h;
        cft_pending_current = cft_h2d + cft_d2h;
        cft_h2d_pending_carry_in_at_reset = cft_h2d;
        cft_d2h_pending_carry_in_at_reset = cft_d2h;
        cft_pending_carry_in_at_reset = cft_h2d + cft_d2h;
        if (ctrl.hostRbStallActive) {
            ctrl.hostRbStallStart = curTick();
            replay_admission_stall_episodes = 1;
        }
        if (ctrl.hostOutstandingWindowStallActive) {
            ctrl.hostOutstandingWindowStallStart = curTick();
            host_outstanding_window_stall_episodes = 1;
        }

        //esj 2025-09-09
        // replay_buffer.reset();
    }

    void CXL_ctrl::CXLStats::preDumpStats() {
        statistics::Group::preDumpStats();
        ctrl.updateReplayOccupancy(
            true, static_cast<uint64_t>(ctrl.cxl_buffer_host.size()));
        ctrl.updateReplayOccupancy(
            false, static_cast<uint64_t>(ctrl.cxl_buffer_device.size()));
        ctrl.updateHostOutstandingWindowOccupancy(ctrl.recv_packet_num);
        ctrl.updateHostDeferredTxOccupancy(ctrl.transmitList.size());
        replay_buffer_high_watermark = ctrl.replayBufferHighWatermark;
        replay_admission_stall_open = ctrl.hostRbStallActive ? 1 : 0;
        host_outstanding_window_stall_open =
            ctrl.hostOutstandingWindowStallActive ? 1 : 0;
        host_replay_current = ctrl.cxl_buffer_host.size();
        device_replay_current = ctrl.cxl_buffer_device.size();
        const uint64_t cft_h2d = ctrl.cftReqPending();
        const uint64_t cft_d2h = ctrl.cftRespPending();
        cft_h2d_pending_current = cft_h2d;
        cft_d2h_pending_current = cft_d2h;
        cft_pending_current = cft_h2d + cft_d2h;
    }

    void
    CXL_ctrl::updateReplayOccupancy(bool host, uint64_t occupancy)
    {
        ReplayOccupancyTracker &tracker = host ?
            hostReplayOccupancy : deviceReplayOccupancy;
        const Tick now = curTick();

        // The observer runs after a replay-buffer mutation.  Charge the
        // elapsed interval to the previously observed level, then install
        // the new level.  Multiple mutations in one tick correctly add no
        // elapsed occupancy between them.
        if (now >= tracker.lastUpdate) {
            tracker.occupancyTicks += tracker.current *
                (now - tracker.lastUpdate);
        }
        tracker.lastUpdate = now;
        tracker.current = occupancy;
        tracker.maximum = std::max(tracker.maximum, occupancy);

        if (host) {
            stats.host_replay_current = tracker.current;
            stats.host_replay_max = tracker.maximum;
            stats.host_replay_occupancy_ticks = tracker.occupancyTicks;
        } else {
            stats.device_replay_current = tracker.current;
            stats.device_replay_max = tracker.maximum;
            stats.device_replay_occupancy_ticks = tracker.occupancyTicks;
        }
    }

    void
    CXL_ctrl::resetReplayOccupancyTracking()
    {
        const Tick now = curTick();
        const uint64_t host_occupancy = cxl_buffer_host.size();
        const uint64_t device_occupancy = cxl_buffer_device.size();

        hostReplayOccupancy = {
            now, host_occupancy, host_occupancy, 0, host_occupancy};
        deviceReplayOccupancy = {
            now, device_occupancy, device_occupancy, 0, device_occupancy};

        stats.host_replay_current = host_occupancy;
        stats.device_replay_current = device_occupancy;
        stats.host_replay_max = host_occupancy;
        stats.device_replay_max = device_occupancy;
        stats.host_replay_occupancy_ticks = 0;
        stats.device_replay_occupancy_ticks = 0;
        stats.host_replay_carry_in_at_reset = host_occupancy;
        stats.device_replay_carry_in_at_reset = device_occupancy;
    }

    void
    CXL_ctrl::updateHostOutstandingWindowOccupancy(uint64_t occupancy)
    {
        if (!is_host)
            return;

        ReplayOccupancyTracker &tracker = hostOutstandingWindowOccupancy;
        const Tick now = curTick();
        if (now >= tracker.lastUpdate) {
            tracker.occupancyTicks += tracker.current *
                (now - tracker.lastUpdate);
        }
        tracker.lastUpdate = now;
        tracker.current = occupancy;
        tracker.maximum = std::max(tracker.maximum, occupancy);
        stats.host_outstanding_window_current = tracker.current;
        stats.host_outstanding_window_max = tracker.maximum;
        stats.host_outstanding_window_occupancy_ticks =
            tracker.occupancyTicks;
    }

    void
    CXL_ctrl::resetHostOutstandingWindowTracking()
    {
        const Tick now = curTick();
        const uint64_t occupancy = is_host ? recv_packet_num : 0;
        hostOutstandingWindowOccupancy = {
            now, occupancy, occupancy, 0, occupancy};
        stats.host_outstanding_window_current = occupancy;
        stats.host_outstanding_window_max = occupancy;
        stats.host_outstanding_window_occupancy_ticks = 0;
        stats.host_outstanding_window_carry_in_at_reset = occupancy;
    }

    int
    CXL_ctrl::hostOutstandingWindowCapacity() const
    {
        return !is_host || validationOutstandingWindowCapacity < 0 ?
            maxQueueSize : validationOutstandingWindowCapacity;
    }

    void
    CXL_ctrl::recordHostOutstandingWindowChange(
        PacketPtr pkt, const std::string &event_name,
        const std::string &reason)
    {
        if (!is_host)
            return;

        updateHostOutstandingWindowOccupancy(recv_packet_num);
        ProtocolValidationEvent event;
        event.event = event_name;
        event.component = name();
        event.layer = "CXL_CONTROLLER";
        event.direction = "H2D";
        event.queueId = "host_outstanding_window";
        event.queueOccupancy = recv_packet_num;
        event.queueCapacity = hostOutstandingWindowCapacity();
        event.reason = reason;
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    void
    CXL_ctrl::beginHostOutstandingWindowStall(PacketPtr pkt)
    {
        if (!is_host || hostOutstandingWindowStallActive ||
            recv_packet_num < hostOutstandingWindowCapacity()) {
            return;
        }

        hostOutstandingWindowStallActive = true;
        hostOutstandingWindowStallStart = curTick();
        ++stats.host_outstanding_window_stall_episodes;
        ProtocolValidationEvent event;
        event.event = "CTRL_WINDOW_STALL_BEGIN";
        event.component = name();
        event.layer = "CXL_CONTROLLER";
        event.direction = "H2D";
        event.queueId = "host_outstanding_window";
        event.queueOccupancy = recv_packet_num;
        event.queueCapacity = hostOutstandingWindowCapacity();
        event.stallReason = "OUTSTANDING_WINDOW_FULL";
        event.reason = "NO_CONTROLLER_WINDOW_SLOT";
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    void
    CXL_ctrl::endHostOutstandingWindowStallIfReady(PacketPtr pkt)
    {
        if (!is_host || !hostOutstandingWindowStallActive ||
            recv_packet_num >= hostOutstandingWindowCapacity()) {
            return;
        }

        const Tick wait = curTick() - hostOutstandingWindowStallStart;
        stats.host_outstanding_window_stall_ticks += wait;
        hostOutstandingWindowStallActive = false;
        ProtocolValidationEvent event;
        event.event = "CTRL_WINDOW_STALL_END";
        event.component = name();
        event.layer = "CXL_CONTROLLER";
        event.direction = "H2D";
        event.queueId = "host_outstanding_window";
        event.queueOccupancy = recv_packet_num;
        event.queueCapacity = hostOutstandingWindowCapacity();
        event.waitTicks = wait;
        event.stallReason = "OUTSTANDING_WINDOW_RELEASE";
        event.reason = "CONTROLLER_WINDOW_SLOT_AVAILABLE";
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    void
    CXL_ctrl::updateHostDeferredTxOccupancy(uint64_t occupancy)
    {
        if (!is_host)
            return;

        ReplayOccupancyTracker &tracker = hostDeferredTxOccupancy;
        const Tick now = curTick();
        if (now >= tracker.lastUpdate) {
            tracker.occupancyTicks += tracker.current *
                (now - tracker.lastUpdate);
        }
        tracker.lastUpdate = now;
        tracker.current = occupancy;
        tracker.maximum = std::max(tracker.maximum, occupancy);
        stats.host_deferred_tx_current = tracker.current;
        stats.host_deferred_tx_max = tracker.maximum;
        stats.host_deferred_tx_occupancy_ticks = tracker.occupancyTicks;
    }

    void
    CXL_ctrl::resetHostDeferredTxTracking()
    {
        const Tick now = curTick();
        const uint64_t occupancy = is_host ? transmitList.size() : 0;
        hostDeferredTxOccupancy = {
            now, occupancy, occupancy, 0, occupancy};
        stats.host_deferred_tx_current = occupancy;
        stats.host_deferred_tx_max = occupancy;
        stats.host_deferred_tx_occupancy_ticks = 0;
        stats.host_deferred_tx_carry_in_at_reset = occupancy;
    }

    void
    CXL_ctrl::recordHostDeferredTxChange(
        PacketPtr pkt, const std::string &event_name,
        const std::string &reason)
    {
        if (!is_host)
            return;

        updateHostDeferredTxOccupancy(transmitList.size());
        ProtocolValidationEvent event;
        event.event = event_name;
        event.component = name();
        event.layer = "CXL_CONTROLLER";
        event.direction = "H2D";
        event.queueId = "host_deferred_tx";
        event.queueOccupancy = transmitList.size();
        event.queueCapacity = maxQueueSize;
        event.reason = reason;
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    uint64_t
    CXL_ctrl::cftReqPending() const
    {
        uint64_t count = 0;
        for (const auto &pending : retry_transmitList_req) {
            // Do not dereference PacketPtr here: legacy invalid entries can
            // retain a stale raw pointer until their scheduler cleanup path.
            if (pending.valid && pending.pkt != nullptr)
                ++count;
        }
        return count;
    }

    uint64_t
    CXL_ctrl::cftRespPending() const
    {
        uint64_t count = 0;
        for (const auto &pending : retry_transmitList_resp) {
            if (pending.valid && pending.pkt != nullptr)
                ++count;
        }
        return count;
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
    maxQueueSize(p.max_queue_size),
    validationReplayPolicyEnable(p.validation_replay_policy_enable),
    validationReplayPolicy(p.validation_replay_policy),
    validationRbStallThreshold(p.validation_rb_stall_threshold),
    validationRbResumeThreshold(p.validation_rb_resume_threshold),
    validationStrictCftDeadline(p.validation_strict_cft_deadline),
    validationOutstandingWindowCapacity(
        p.validation_outstanding_window_capacity),
    LRSM(),RRSM(),recv_state(),trans_state(),
    TL2Packingevent([this]{TL2Packing();} , p.name),PHY2Decodingevent([this]{PHY2Decoding();}, p.name),cxl_buffer_host(p.max_queue_size),cxl_packet(NULL),cxl_packet_host(NULL),Host2Device_busy(false)
    ,is_host(p.Host),timeoutEvent([this]{timeoutfunc();} , p.name),retransmit(false),retransmitIdx(0),txQueueEvent([this]{processTxQueue();} , p.name),retryReq(false),mps(p.mps),
    delay(p.delay),delay_var(p.delay_var),sendSeqNum(0),recvSeqNum(0),temp_pkt(NULL),sendEvent([this]{trySendTiming();}, p.name),popseq(false),control_flit_respEvent([this]{control_flit_respfunc();}, p.name)
    ,response_busy(false),retryReq_host(false),retryResp_host(false),sendEventResp([this]{trySendTimingResp();}, p.name),
    cxl_buffer_device(p.max_queue_size),recv_state_device(),trans_state_device(),retryResp(false),
    control_flit_reqEvent([this]{control_flit_reqfunc();}, p.name),request_busy(false),control_flit_resp(NULL),control_flit(NULL),
    retry_buffer_req(p.max_queue_size),retry_buffer_resp(p.max_queue_size),
    req_seqnum(0),resp_seqnum(0),recv_packet_num(0),control_flit_cycle(p.control_flit_cycle),
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

        if (validationReplayPolicyEnable) {
            if (validationReplayPolicy != "hard_full" &&
                validationReplayPolicy != "high_watermark" &&
                validationReplayPolicy != "hysteretic") {
                fatal("%s: unknown validation replay policy '%s'\n",
                      name(), validationReplayPolicy);
            }
            if (validationRbStallThreshold < 0) {
                validationRbStallThreshold = maxQueueSize;
            }
            if (validationRbResumeThreshold < 0) {
                validationRbResumeThreshold =
                    validationRbStallThreshold - 1;
            }
            if (validationRbStallThreshold < 1 ||
                validationRbStallThreshold > maxQueueSize) {
                fatal("%s: validation RB stall threshold %d must be in [1,%d]\n",
                      name(), validationRbStallThreshold, maxQueueSize);
            }
            if (validationRbResumeThreshold < 0 ||
                validationRbResumeThreshold >= validationRbStallThreshold) {
                fatal("%s: validation RB resume threshold %d must be in [0,%d)\n",
                      name(), validationRbResumeThreshold,
                      validationRbStallThreshold);
            }
            if (validationReplayPolicy == "hard_full" &&
                (validationRbStallThreshold != maxQueueSize ||
                 validationRbResumeThreshold != maxQueueSize - 1)) {
                fatal("%s: hard_full requires stall/resume %d/%d\n",
                      name(), maxQueueSize, maxQueueSize - 1);
            }
            if (validationReplayPolicy == "high_watermark" &&
                validationRbResumeThreshold !=
                    validationRbStallThreshold - 1) {
                fatal("%s: high_watermark requires resume=stall-1\n", name());
            }
            if (validationReplayPolicy == "hysteretic" &&
                validationRbResumeThreshold >=
                    validationRbStallThreshold - 1) {
                fatal("%s: hysteretic requires resume<stall-1\n", name());
            }
        }
        if (validationOutstandingWindowCapacity == 0 ||
            validationOutstandingWindowCapacity < -1) {
            fatal("%s: validation outstanding-window capacity %d must be "
                  "-1 or greater than zero\n",
                  name(), validationOutstandingWindowCapacity);
        }

        cxl_buffer_host.setOccupancyObserver(
            [this](uint64_t occupancy) {
                updateReplayOccupancy(true, occupancy);
            });
        cxl_buffer_device.setOccupancyObserver(
            [this](uint64_t occupancy) {
                updateReplayOccupancy(false, occupancy);
            });
        resetReplayOccupancyTracking();
        resetHostOutstandingWindowTracking();
        resetHostDeferredTxTracking();
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

    int
    CXL_ctrl::hostRbStallThreshold() const
    {
        return validationReplayPolicyEnable ?
            validationRbStallThreshold : maxQueueSize;
    }

    int
    CXL_ctrl::hostRbResumeThreshold() const
    {
        return validationReplayPolicyEnable ?
            validationRbResumeThreshold : maxQueueSize - 1;
    }

    std::string
    CXL_ctrl::hostRbPolicyLabel() const
    {
        if (!validationReplayPolicyEnable ||
            validationReplayPolicy == "hard_full") {
            return "HARD_FULL";
        }
        if (validationReplayPolicy == "high_watermark") {
            return "HIGH_WATERMARK";
        }
        return "HYSTERETIC";
    }

    uint64_t
    CXL_ctrl::hostReplayReservationWeight(PacketPtr pkt) const
    {
        if (pkt == nullptr || pkt->cxl_pkt.retry_req ||
            pkt->cxl_pkt.retry_resp || pkt->cxl_pkt.is_retry_req ||
            pkt->cxl_pkt.is_retry_resp || pkt->cxl_pkt.is_controlflit) {
            return 0;
        }

        return pkt->cxlMergedParent && pkt->cxlMergedPkt != nullptr ? 2 : 1;
    }

    uint64_t
    CXL_ctrl::hostReplayReservedOccupancy()
    {
        uint64_t occupancy = cxl_buffer_host.size();

        // A newly accepted request is inserted into the replay buffer only
        // after the deferred transmit succeeds.  Count every pending initial
        // transmission in production as well as validation: the outstanding
        // window may be larger than the replay buffer.  Exact retries already
        // own their replay slots and must not reserve them a second time.
        for (const auto &deferred : transmitList)
            occupancy += hostReplayReservationWeight(deferred.pkt);

        return occupancy;
    }

    bool
    CXL_ctrl::hostReplayBlocksAdmission(PacketPtr pkt)
    {
        if (!validationReplayPolicyEnable) {
            return hostReplayReservedOccupancy() +
                hostReplayReservationWeight(pkt) >
                static_cast<uint64_t>(maxQueueSize);
        }
        if (hostRbStallActive) {
            // Only the release path may clear an active hysteresis episode.
            // This keeps admission state and STALL_BEGIN/END evidence atomic.
            return hostReplayReservationWeight(pkt) != 0;
        }
        // Admission invariant for every validation policy:
        // committed RB + deferred initial reservations + candidate <= limit.
        return hostReplayReservedOccupancy() +
            hostReplayReservationWeight(pkt) > hostRbStallThreshold();
    }

    void
    CXL_ctrl::beginHostRbStall(PacketPtr pkt)
    {
        const uint64_t admission_occupancy = hostReplayReservedOccupancy();
        if (!is_host || hostRbStallActive ||
            admission_occupancy + hostReplayReservationWeight(pkt) <=
                static_cast<uint64_t>(hostRbStallThreshold())) {
            return;
        }

        hostRbStallActive = true;
        hostRbStallStart = curTick();
        ++stats.replay_admission_stall_episodes;
        ProtocolValidationEvent event;
        event.event = "RB_STALL_BEGIN";
        event.component = name();
        event.layer = "CXL_PROTOCOL";
        event.direction = "H2D";
        event.rbId = "host_req";
        event.rbOccupancy = cxl_buffer_host.size();
        event.rbCapacity = maxQueueSize;
        event.queueId = "host_req_effective_admission";
        event.queueOccupancy = admission_occupancy;
        event.queueCapacity = hostRbStallThreshold();
        event.stallReason = "REPLAY_BUFFER_POLICY";
        event.reason = hostRbPolicyLabel();
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    void
    CXL_ctrl::endHostRbStallIfReady(PacketPtr pkt)
    {
        const uint64_t admission_occupancy = hostReplayReservedOccupancy();
        if (!hostRbStallActive ||
            admission_occupancy > hostRbResumeThreshold()) {
            return;
        }

        stats.replay_admission_stall_ticks += curTick() - hostRbStallStart;
        hostRbStallActive = false;
        ProtocolValidationEvent event;
        event.event = "RB_STALL_END";
        event.component = name();
        event.layer = "CXL_PROTOCOL";
        event.direction = "H2D";
        event.packetSeq = pkt->cxl_pkt.seqNum;
        event.rbId = "host_req";
        event.rbOccupancy = cxl_buffer_host.size();
        event.rbCapacity = maxQueueSize;
        event.queueId = "host_req_effective_admission";
        event.queueOccupancy = admission_occupancy;
        event.queueCapacity = hostRbStallThreshold();
        event.stallReason = "REPLAY_BUFFER_RELEASE";
        event.reason = hostRbPolicyLabel() + "_RESUME";
        ProtocolValidationLogger::recordPacket(event, pkt);
    }

    void
    CXL_ctrl::countAckRx(bool d2h, uint64_t count)
    {
        stats.ack_rx_count += count;
        if (d2h)
            stats.ack_rx_d2h_count += count;
        else
            stats.ack_rx_h2d_count += count;
    }

    void
    CXL_ctrl::countNakRx(bool d2h, uint64_t count)
    {
        stats.nak_rx_count += count;
        if (d2h)
            stats.nak_rx_d2h_count += count;
        else
            stats.nak_rx_h2d_count += count;
    }

    PacketPtr
    CXL_ctrl::cloneDeviceReplayPacket(PacketPtr pkt) const
    {
        if (!pkt)
            return nullptr;

        // Packet(pkt, ..., true) deliberately aliases STATIC_DATA.  A replay
        // snapshot must outlive the downstream packet regardless of whether
        // its producer used static or dynamic storage, so allocate and copy
        // the payload explicitly for both cases.
        PacketPtr copy = new Packet(pkt, true, false);
        copy->senderState = nullptr;
        copy->headerDelay = 0;
        copy->payloadDelay = 0;
        copy->snoopDelay = 0;

        // Packet's generic copy constructor intentionally does not know about
        // FlexCXL's transport metadata.  A replay attempt must preserve it.
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

    bool
    CXL_ctrl::insertDeviceReplaySnapshot(PacketPtr pkt)
    {
        const uint64_t seq_num = pkt->cxl_pkt.seqNum;

        // A non-retry response owns one exact-sequence slot.  Remove a stale
        // duplicate from the raw-pointer index before replacing its owner.
        cxl_buffer_device.findByseqnum2(seq_num);
        deviceReplaySnapshots.erase(seq_num);

        PacketPtr snapshot = cloneDeviceReplayPacket(pkt);
        const int size_before = cxl_buffer_device.size();
        cxl_buffer_device.pushBack(snapshot);
        if (cxl_buffer_device.size() == size_before + 1) {
            deviceReplaySnapshots.emplace(
                seq_num, std::unique_ptr<Packet>(snapshot));
            return true;
        } else {
            delete snapshot;
            return false;
        }
    }

    void
    CXL_ctrl::releaseDeviceReplaySnapshot(uint64_t seq_num)
    {
        deviceReplaySnapshots.erase(seq_num);
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
                transmitList.size(), maxQueueSize, RRSM.cur_stat(),
                reserved_read_queue, read_queue_size, recv_packet_num,
                hostOutstandingWindowCapacity());
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

            //esj 2025-09-09
            if(cxl_buffer_host.size() > 0){
                stats.replay_buffer_without_zero.sample(cxl_buffer_host.size());
            }
            stats.replay_buffer.sample(cxl_buffer_host.size());
            if (cxl_buffer_host.size() > replayBufferHighWatermark) {
                replayBufferHighWatermark = cxl_buffer_host.size();
            }
            //esj 2025-11-20
            // stats.replay_buffer_size[cxl_buffer_host.size()]++;
            

            //esj 2025-05-15
            //esj 2025-06-30
            // if(pkt->isRead() && (reserved_read_queue > read_queue_size -1)){
            //     retryReq_host = true;
            //     warn("esj  %s cxl ctrl blocked, reserved_read_queue/read_queue_size = %d/%d\n",__func__,reserved_read_queue,read_queue_size);
            //     return false;
            // }
            
            // esj 2025-05-16
            const bool replay_blocked = hostReplayBlocksAdmission(pkt);
            const bool tx_queue_blocked =
                transmitList.size() > maxQueueSize - 1;
            const bool recv_window_blocked =
                recv_packet_num >= static_cast<uint64_t>(
                    hostOutstandingWindowCapacity());
            if (tx_queue_blocked || replay_blocked
            // if ((transmitList.size() + transmitList_write.size()) > (maxQueueSize -1) || cxl_buffer_host.size() > (maxQueueSize-1)  
                || recv_window_blocked //esj 2024-12-25
                // || reserved_read_queue > read_queue_size -1 //esj 2025-01-29 //esj 2025-05-15
                // LRSM.Local_state == LRSM.RETRY_LLRREQ) {
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL 
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d\n",
                        __func__, cxl_buffer_host.size(), maxQueueSize, 
                        transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size);
                retryReq_host = true;

                if (replay_blocked) {
                    beginHostRbStall(pkt);
                    ++stats.admission_stall_host_replay_count;
                }
                if (tx_queue_blocked)
                    ++stats.admission_stall_host_tx_queue_count;
                if (recv_window_blocked) {
                    ++stats.admission_stall_host_recv_window_count;
                    beginHostOutstandingWindowStall(pkt);
                }

                //esj 2025-11-21
                stats.stall_req_count++;
                
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
                recordHostOutstandingWindowChange(
                    pkt, "CTRL_WINDOW_ACQUIRE", "REQUEST_ACCEPTED");

                if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                    recv_packet_num++;
                    recordHostOutstandingWindowChange(
                        pkt->cxlMergedPkt, "CTRL_WINDOW_ACQUIRE",
                        "MERGED_REQUEST_ACCEPTED");
                }
            }
            DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",recv_packet_num);

            //esj 2025-09-09
            // stats.replay_buffer.sample(cxl_buffer_host.size());

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

            if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                uint64_t parent_seq = trans_state.send_packet_num;
                pkt->cxl_pkt.seqNum = parent_seq;
                pkt->cxlMergedPkt->cxl_pkt.seqNum = parent_seq + 1;
            } else {
                pkt->cxl_pkt.seqNum = trans_state.send_packet_num;
            }

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

            pkt->cxl_pkt.resp_seqnum = resp_seqnum;

            ProtocolValidationEvent seq_assigned;
            seq_assigned.event = "PACKET_SEQ_ASSIGN";
            seq_assigned.component = name();
            seq_assigned.layer = "CXL_PROTOCOL";
            seq_assigned.direction = "H2D";
            seq_assigned.packetSeq = pkt->cxl_pkt.seqNum;
            seq_assigned.txAttempt = 0;
            ProtocolValidationLogger::recordPacket(seq_assigned, pkt);

            //esj 2025-01-17
            if(error_resp_transmitList.size() > 0){
                uint64_t seq_num = error_resp_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_resp_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                error_resp_transmitList.pop_front();

                if(control_flit_reqEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_reqEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_reqEvent\n",__func__);
                    bool pop_success =
                        popfromtransmitlist_req(seq_num, true);

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



            DPRINTF(CXL_ctrl, "[%s] Packet details: seqNum=%d, isReq=%d, isRwD=%d, isDRS=%d, isNDR=%d, mergedParent=%d,\n",
                    __func__, pkt->cxl_pkt.seqNum, 
                    pkt->isRead() && pkt->isRequest(),
                    pkt->isWrite() && pkt->isRequest(),
                    pkt->isRead() && pkt->isResponse(),
                    pkt->isWrite() && pkt->isResponse(),
                    pkt->cxlMergedParent);
            //esj 2025-03-02
            if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                trans_state.send_packet_num =
                    pkt->cxlMergedPkt->cxl_pkt.seqNum + 1;
            } else {
                trans_state.send_packet_num++;
            }
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
            const bool tx_queue_blocked =
                transmitList_resp.size() > maxQueueSize - 1;
            const size_t pending_replay = pendingDeviceReplaySlots();
            const bool replay_blocked = deviceReplayBlocksAdmission(pkt);
            if (tx_queue_blocked || replay_blocked
            // || recv_packet_num > maxQueueSize -1 //esj 2024-12-26
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL 
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: cxl_buffer_device size=%d/%d, pending_replay=%d, transmitList_resp_size=%d/%d, state=%s\n",
                        __func__, cxl_buffer_device.size(), maxQueueSize, 
                        static_cast<int>(pending_replay),
                        transmitList_resp.size(), maxQueueSize,
                        RRSM.cur_stat());
                // warn("esj  %s cxl ctrl blocked\n",__func__);
                retryResp_host = true;
                if (tx_queue_blocked)
                    ++stats.admission_stall_device_tx_queue_count;
                if (replay_blocked)
                    ++stats.admission_stall_device_replay_count;
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
            pkt->cxl_pkt.req_seqnum = req_seqnum;

            //esj 2025-01-17
            if(error_req_transmitList.size() > 0){
                uint64_t seq_num = error_req_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_req_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                error_req_transmitList.pop_front();
                DPRINTF(CXL_ctrl, "======================[ error_req_transmitList ]======================\n");
                DPRINTF(CXL_ctrl, "error_req_transmitList front seqnum = %llu, list size = %d\n",seq_num,error_req_transmitList.size());

                if(control_flit_respEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_respEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_respEvent\n",__func__);
                    bool pop_success =
                        popfromtransmitlist_resp(seq_num, true);

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
                transmitList.size(), maxQueueSize, RRSM.cur_stat(),
                reserved_read_queue, read_queue_size, recv_packet_num,
                hostOutstandingWindowCapacity());
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
                ++stats.admission_stall_read_reservation_count;
                return false;
            }
            
            // esj 2025-05-16
            const bool replay_blocked = hostReplayBlocksAdmission(pkt);
            const bool tx_queue_blocked =
                transmitList.size() > maxQueueSize - 1;
            const bool recv_window_blocked =
                recv_packet_num >= static_cast<uint64_t>(
                    hostOutstandingWindowCapacity());
            if (tx_queue_blocked || replay_blocked
            // if ((transmitList.size() + transmitList_write.size()) > (maxQueueSize -1) || cxl_buffer_host.size() > (maxQueueSize-1)  
                || recv_window_blocked //esj 2024-12-25
                // || reserved_read_queue > read_queue_size -1 //esj 2025-01-29 //esj 2025-05-15
                // LRSM.Local_state == LRSM.RETRY_LLRREQ) {
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL 
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: buffer_host_size=%d/%d, transmitList_size=%d/%d, state=%s, read queue size =%d/%d\n",
                        __func__, cxl_buffer_host.size(), maxQueueSize, 
                        transmitList.size(), maxQueueSize, RRSM.cur_stat(),reserved_read_queue,read_queue_size);
                retryReq_host = true;
                if (replay_blocked) {
                    beginHostRbStall(pkt);
                    ++stats.admission_stall_host_replay_count;
                }
                if (tx_queue_blocked)
                    ++stats.admission_stall_host_tx_queue_count;
                if (recv_window_blocked) {
                    ++stats.admission_stall_host_recv_window_count;
                    beginHostOutstandingWindowStall(pkt);
                }
                return false;
            }

            //esj 2024-12-25
            //esj 2025-02-26
            // recv_packet_num++;
            if(!pkt->cxl_pkt.is_controlflit){
                recv_packet_num++;
                recordHostOutstandingWindowChange(
                    pkt, "CTRL_WINDOW_ACQUIRE", "REQUEST_ACCEPTED");
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

            if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                uint64_t parent_seq = trans_state.send_packet_num;
                pkt->cxl_pkt.seqNum = parent_seq;
                pkt->cxlMergedPkt->cxl_pkt.seqNum = parent_seq + 1;
            } else {
                pkt->cxl_pkt.seqNum = trans_state.send_packet_num;
            }

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

            pkt->cxl_pkt.resp_seqnum = resp_seqnum;

            //esj 2025-01-17
            if(error_resp_transmitList.size() > 0){
                uint64_t seq_num = error_resp_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_resp_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_req_seqnum = -1;
                error_resp_transmitList.pop_front();

                if(control_flit_reqEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_reqEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_reqEvent\n",__func__);
                    bool pop_success =
                        popfromtransmitlist_req(seq_num, true);

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
            if (pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)) {
                trans_state.send_packet_num =
                    pkt->cxlMergedPkt->cxl_pkt.seqNum + 1;
            } else {
                trans_state.send_packet_num++;
            }
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
            const bool tx_queue_blocked =
                transmitList_resp.size() > maxQueueSize - 1;
            const size_t pending_replay = pendingDeviceReplaySlots();
            const bool replay_blocked = deviceReplayBlocksAdmission(pkt);
            if (tx_queue_blocked || replay_blocked
            // || recv_packet_num > maxQueueSize -1 //esj 2024-12-26
                // || RRSM.Remote_state != CXL_Remote_state::RETRY_REMOTE_NORMAL 
                // || LRSM.Local_state != CXL_Local_state::RETRY_LOCAL_NORMAL
                ) { //esj 2024-12-17
                DPRINTF(CXL_ctrl, "[%s] BLOCKED: cxl_buffer_device size=%d/%d, pending_replay=%d, transmitList_resp_size=%d/%d, state=%s\n",
                        __func__, cxl_buffer_device.size(), maxQueueSize, 
                        static_cast<int>(pending_replay),
                        transmitList_resp.size(), maxQueueSize,
                        RRSM.cur_stat());
                retryResp_host = true;
                if (tx_queue_blocked)
                    ++stats.admission_stall_device_tx_queue_count;
                if (replay_blocked)
                    ++stats.admission_stall_device_replay_count;
                // warn("esj  %s cxl ctrl blocked\n",__func__);

                //esj 2025-11-21
                stats.stall_resp_count++;
                return false;
            }

            //esj 2025-01-29
            //esj 2025-06-30
            cxl_buffer_device.eraseinvalid();
            //

            //esj 2025-09-09
            if(cxl_buffer_device.size() > 0){
                stats.replay_buffer_without_zero.sample(
                    cxl_buffer_device.size());
            }
            stats.replay_buffer.sample(cxl_buffer_device.size());
            if (cxl_buffer_device.size() > replayBufferHighWatermark) {
                replayBufferHighWatermark = cxl_buffer_device.size();
            }
            //esj 2025-11-20
            // stats.replay_buffer_size[cxl_buffer_device.size()]++;

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
            pkt->cxl_pkt.req_seqnum = req_seqnum;

            //esj 2025-01-17
            if(error_req_transmitList.size() > 0){
                uint64_t seq_num = error_req_transmitList.front().crc_error_seqnum;
                pkt->cxl_pkt.crc_error_req_seqnum = seq_num;
                pkt->cxl_pkt.crc_error_resp_seqnum = -1;
                error_req_transmitList.pop_front();
                DPRINTF(CXL_ctrl, "======================[ error_req_transmitList ]======================\n");
                DPRINTF(CXL_ctrl, "error_req_transmitList front seqnum = %llu, list size = %d\n",seq_num,error_req_transmitList.size());

                if(control_flit_respEvent.scheduled()){
                    DPRINTF(CXL_ctrl, "======================[ descheduling error control_flit_respEvent ]======================\n");
                    DPRINTF(CXL_ctrl,"[%s], descheduling retry control_flit_respEvent\n",__func__);
                    bool pop_success =
                        popfromtransmitlist_resp(seq_num, true);

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
            //esj 2025-11-21
            stats.stall_req_retry_count++;
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
            //esj 2025-11-21
            stats.stall_resp_retry_count++;
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
                replayBufferHighWatermark = std::max(
                    replayBufferHighWatermark,
                    static_cast<uint64_t>(cxl_buffer_host.size()));
                
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
                uint64_t seq_num = packet->cxl_pkt.seqNum;

                //esj 2025-01-18
                for(auto it = error_resp_transmitList.begin(); it != error_resp_transmitList.end(); it++){
                    if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                        DPRINTF(CXL_ctrl,"%s, found error_resp_transmitList, erase it\n",__func__);
                        it = error_resp_transmitList.erase(it);
                        break;
                    }
                }

                //esj 2024-12-25
                // resp_seqnum = packet->cxl_pkt.resp_seqnum;
                resp_seqnum = packet->cxl_pkt.seqNum;


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
                    const uint64_t req_ack_seq = packet->cxl_pkt.req_seqnum;
                    countAckRx(
                        true, (req_ack_seq == seq_num) ? 1 : 2);
                    const bool validation_logging =
                        ProtocolValidationLogger::enabled();
                    PacketPtr seq_packet = validation_logging ?
                        cxl_buffer_host.peekByseqnum(seq_num) : nullptr;
                    PacketPtr req_seq_packet = req_ack_seq == seq_num ?
                        seq_packet : (validation_logging ?
                            cxl_buffer_host.peekByseqnum(req_ack_seq) : nullptr);
                    const int rb_size_before = validation_logging ?
                        cxl_buffer_host.size() : 0;

                    if (validation_logging) {
                        ProtocolValidationEvent seq_ack;
                        seq_ack.event = "ACK_RX";
                        seq_ack.component = name();
                        seq_ack.layer = "CXL_PROTOCOL";
                        seq_ack.direction = "D2H";
                        seq_ack.ackSeq = seq_num;
                        seq_ack.packetSeq = seq_num;
                        seq_ack.rbId = "host_req";
                        seq_ack.reason = "NORMAL_RESPONSE_EXACT";
                        ProtocolValidationLogger::recordPacket(
                            seq_ack, seq_packet ? seq_packet : packet);

                        if (req_ack_seq != seq_num) {
                            ProtocolValidationEvent req_ack = seq_ack;
                            req_ack.ackSeq = req_ack_seq;
                            req_ack.packetSeq = req_ack_seq;
                            ProtocolValidationLogger::recordPacket(
                                req_ack,
                                req_seq_packet ? req_seq_packet : packet);
                        }
                    }

                    cxl_buffer_host.popByseqnum_double(seq_num, packet->cxl_pkt.req_seqnum);

                    if (validation_logging) {
                        int reconstructed_occupancy = rb_size_before;
                        if (seq_packet &&
                            !cxl_buffer_host.findByseqnum(seq_num)) {
                            --reconstructed_occupancy;
                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "D2H";
                            released.packetSeq = seq_num;
                            released.ackSeq = seq_num;
                            released.rbId = "host_req";
                            released.rbOccupancy = reconstructed_occupancy;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "NORMAL_RESPONSE_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, seq_packet);
                        }
                        if (req_seq_packet && req_ack_seq != seq_num &&
                            !cxl_buffer_host.findByseqnum(req_ack_seq)) {
                            --reconstructed_occupancy;
                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "D2H";
                            released.packetSeq = req_ack_seq;
                            released.ackSeq = req_ack_seq;
                            released.rbId = "host_req";
                            released.rbOccupancy = reconstructed_occupancy;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "NORMAL_RESPONSE_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, req_seq_packet);
                        }
                    }

                    // esj 2025-05-16
                    // cancelSeqEvent(packet->cxl_pkt.req_seqnum);
                    cancelSeqEvent_double(seq_num, packet->cxl_pkt.req_seqnum);
                    endHostRbStallIfReady(packet);

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

                    countAckRx(true);
                    if(cxl_buffer_host.empty()){
                        DPRINTF(CXL_ctrl, "[%s] buffer is empty\n", __func__);
                    }
                    else{
                        const bool validation_logging =
                            ProtocolValidationLogger::enabled();
                        PacketPtr acked_packet = validation_logging ?
                            cxl_buffer_host.peekByseqnum(seq_num) : nullptr;
                        const int rb_size_before = validation_logging ?
                            cxl_buffer_host.size() : 0;

                        if (validation_logging) {
                            ProtocolValidationEvent ack;
                            ack.event = "ACK_RX";
                            ack.component = name();
                            ack.layer = "CXL_PROTOCOL";
                            ack.direction = "D2H";
                            ack.packetSeq = seq_num;
                            ack.ackSeq = seq_num;
                            ack.rbId = "host_req";
                            ack.reason = "STANDALONE_CONTROL_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                ack, acked_packet ? acked_packet : packet);
                        }

                        popseq = cxl_buffer_host.popByseqnum(seq_num);

                        if (validation_logging && popseq && acked_packet) {
                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "D2H";
                            released.packetSeq = seq_num;
                            released.ackSeq = seq_num;
                            released.rbId = "host_req";
                            released.rbOccupancy = rb_size_before - 1;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "STANDALONE_CONTROL_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, acked_packet);
                        }

                        if (popseq)
                            endHostRbStallIfReady(packet);
                        
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

                    countNakRx(true);
                    ProtocolValidationEvent nak_rx;
                    nak_rx.event = "NAK_RX";
                    nak_rx.component = name();
                    nak_rx.layer = "CXL_PROTOCOL";
                    nak_rx.direction = "D2H";
                    nak_rx.packetSeq = packet->cxl_pkt.seqNum;
                    nak_rx.nakSeq = packet->cxl_pkt.seqNum;
                    nak_rx.reason = "CRC_NAK";
                    ProtocolValidationLogger::recordPacket(
                        nak_rx, pkt ? pkt : packet);

                    //esj 2025-01-17
                    for(auto it = error_req_transmitList.begin(); it != error_req_transmitList.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                            it = error_req_transmitList.erase(it);
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

                    //esj 2024-12-22
                    //esj 2024-12-25
                    // Retransmit the exact ACK metadata carried by the first
                    // attempt.  The current global resp_seqnum may already
                    // refer to a newer response.

                    assert(pkt != NULL);

                    pkt->cxl_pkt.retry_req = true;

                    ProtocolValidationEvent retry_begin;
                    retry_begin.event = "RETRY_BEGIN";
                    retry_begin.component = name();
                    retry_begin.layer = "CXL_PROTOCOL";
                    retry_begin.direction = "H2D";
                    retry_begin.packetSeq = pkt->cxl_pkt.seqNum;
                    retry_begin.nakSeq = pkt->cxl_pkt.seqNum;
                    retry_begin.txAttempt = 1;
                    retry_begin.reason = "SELECTIVE_EXACT_SEQUENCE";
                    ProtocolValidationLogger::recordPacket(retry_begin, pkt);

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
                    countAckRx(true);
                    
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
                        const bool validation_logging =
                            ProtocolValidationLogger::enabled();
                        PacketPtr acked_packet = validation_logging ?
                            cxl_buffer_host.peekByseqnum(seq_num) : nullptr;
                        const int rb_size_before = validation_logging ?
                            cxl_buffer_host.size() : 0;
                        popseq = cxl_buffer_host.popByseqnum(seq_num);

                        if (validation_logging && popseq && acked_packet) {
                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "D2H";
                            released.packetSeq = seq_num;
                            released.ackSeq = seq_num;
                            released.rbId = "host_req";
                            released.rbOccupancy = rb_size_before - 1;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "DEVICE_CONTROL_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, acked_packet);
                        }
                        if (popseq)
                            endHostRbStallIfReady(packet);
                        
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
                    countNakRx(true);
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
                    for(auto it = error_req_transmitList.begin(); it != error_req_transmitList.end(); it++){
                        if(it->crc_error_seqnum == packet->cxl_pkt.seqNum){
                            DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                            it = error_req_transmitList.erase(it);
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

                    //esj 2024-12-25
                    // Retransmit the exact ACK metadata carried by the first
                    // attempt.  The current global resp_seqnum may already
                    // refer to a newer response.

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
                    if (popseq) {
                        releaseDeviceReplaySnapshot(seq_num);
                        countAckRx(false);
                    }
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
                    countAckRx(false);
                    
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
                        const int device_rb_size_before =
                            cxl_buffer_device.size();
                        PacketPtr acked_device_resp =
                            ProtocolValidationLogger::enabled() ?
                            cxl_buffer_device.peekByseqnum(seq_num) : nullptr;
                        popseq = cxl_buffer_device.popByseqnum3(seq_num);

                        if (popseq) {
                            if (acked_device_resp) {
                                ProtocolValidationEvent ack;
                                ack.event = "ACK_RX";
                                ack.component = name();
                                ack.layer = "CXL_PROTOCOL";
                                ack.direction = "H2D";
                                ack.packetSeq = seq_num;
                                ack.ackSeq = seq_num;
                                ack.rbId = "device_resp";
                                ack.reason = "STANDALONE_CONTROL_EXACT";
                                ProtocolValidationLogger::recordPacket(
                                    ack, acked_device_resp);
                            }

                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "H2D";
                            released.packetSeq = seq_num;
                            released.ackSeq = seq_num;
                            released.rbId = "device_resp";
                            released.rbOccupancy =
                                device_rb_size_before - 1;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "STANDALONE_CONTROL_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, packet);
                            releaseDeviceReplaySnapshot(seq_num);
                        }
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
                    countNakRx(false);
                    ProtocolValidationEvent nak_rx;
                    nak_rx.event = "NAK_RX";
                    nak_rx.component = name();
                    nak_rx.layer = "CXL_PROTOCOL";
                    nak_rx.direction = "H2D";
                    nak_rx.packetSeq = packet->cxl_pkt.seqNum;
                    nak_rx.nakSeq = packet->cxl_pkt.seqNum;
                    nak_rx.reason = "CRC_NAK";
                    ProtocolValidationLogger::recordPacket(nak_rx, packet);
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
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum7(packet->cxl_pkt.seqNum);


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
                            if(pkt == NULL){
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

                    ProtocolValidationEvent retry_begin;
                    retry_begin.event = "RETRY_BEGIN";
                    retry_begin.component = name();
                    retry_begin.layer = "CXL_PROTOCOL";
                    retry_begin.direction = "D2H";
                    retry_begin.packetSeq = pkt->cxl_pkt.seqNum;
                    retry_begin.nakSeq = packet->cxl_pkt.seqNum;
                    retry_begin.txAttempt = 1;
                    retry_begin.reason = "SELECTIVE_EXACT_SEQUENCE";
                    ProtocolValidationLogger::recordPacket(
                        retry_begin, pkt);

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
                    pkt->cxl_pkt.req_seqnum = req_seqnum;
                    
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
                    countAckRx(false);
                    
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
                        const int device_rb_size_before =
                            cxl_buffer_device.size();
                        PacketPtr acked_device_resp =
                            ProtocolValidationLogger::enabled() ?
                            cxl_buffer_device.peekByseqnum(seq_num) : nullptr;
                        popseq = cxl_buffer_device.popByseqnum3(seq_num);

                        if (popseq) {
                            if (acked_device_resp) {
                                ProtocolValidationEvent ack;
                                ack.event = "ACK_RX";
                                ack.component = name();
                                ack.layer = "CXL_PROTOCOL";
                                ack.direction = "H2D";
                                ack.packetSeq = seq_num;
                                ack.ackSeq = seq_num;
                                ack.rbId = "device_resp";
                                ack.reason = "STANDALONE_CONTROL_EXACT";
                                ProtocolValidationLogger::recordPacket(
                                    ack, acked_device_resp);
                            }

                            ProtocolValidationEvent released;
                            released.event = "RB_RELEASE";
                            released.component = name();
                            released.layer = "CXL_PROTOCOL";
                            released.direction = "H2D";
                            released.packetSeq = seq_num;
                            released.ackSeq = seq_num;
                            released.rbId = "device_resp";
                            released.rbOccupancy =
                                device_rb_size_before - 1;
                            released.rbCapacity = maxQueueSize;
                            released.reason = "STANDALONE_CONTROL_EXACT";
                            ProtocolValidationLogger::recordPacket(
                                released, packet);
                            releaseDeviceReplaySnapshot(seq_num);
                        }

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
                    countNakRx(false);
                    ProtocolValidationEvent nak_rx;
                    nak_rx.event = "NAK_RX";
                    nak_rx.component = name();
                    nak_rx.layer = "CXL_PROTOCOL";
                    nak_rx.direction = "H2D";
                    nak_rx.packetSeq = packet->cxl_pkt.seqNum;
                    nak_rx.nakSeq = packet->cxl_pkt.seqNum;
                    nak_rx.reason = "CRC_NAK";
                    ProtocolValidationLogger::recordPacket(nak_rx, packet);
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
                    // PacketPtr pkt = cxl_buffer_device.popByseqnum7(packet->cxl_pkt.seqNum);

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
                            if(pkt == NULL){
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
                    pkt->cxl_pkt.req_seqnum = req_seqnum;

                    //esj 2024-12-19
                    pkt->cxl_pkt.LRSM_device = LRSM.cur_stat();

                    assert(pkt != NULL);
                    
                    //esj 2024-12-18
                    pkt->cxl_pkt.retry_resp = true;

                    ProtocolValidationEvent retry_begin;
                    retry_begin.event = "RETRY_BEGIN";
                    retry_begin.component = name();
                    retry_begin.layer = "CXL_PROTOCOL";
                    retry_begin.direction = "D2H";
                    retry_begin.packetSeq = pkt->cxl_pkt.seqNum;
                    retry_begin.nakSeq = packet->cxl_pkt.seqNum;
                    retry_begin.txAttempt = 1;
                    retry_begin.reason = "SELECTIVE_EXACT_SEQUENCE";
                    ProtocolValidationLogger::recordPacket(
                        retry_begin, pkt);

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
                        bool pop_success = ctrl->popfromtransmitlist_resp(
                            ctrl->req_seqnum, true);
                        
                        if(pop_success){
                            DPRINTF(CXL_ctrl,"%s, POP from retry_transmitList_resp seq = %d\n",__func__,ctrl->req_seqnum);
                        }
                        //esj 2025-01-18
                        bool pop_success2 = ctrl->popfromtransmitlist_resp(
                            pkt->cxl_pkt.seqNum, true);
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
                        DPRINTF(CXL_ctrl,"%s, pkt_temp addr = %0x, seq = %d, crc_check = %d, is_controlflit = %d, retry_resp = %d, retry_req = %d, is_request = %s\n",
                        __func__,pkt_temp->getAddr(),pkt_temp->cxl_pkt.seqNum,pkt_temp->cxl_pkt.crc_check,pkt_temp->cxl_pkt.is_controlflit,pkt_temp->cxl_pkt.retry_resp,pkt_temp->cxl_pkt.retry_req,pkt_temp->isRequest()? "true":"false");
                        
                        DPRINTF(CXL_ctrl,"%s, packet addr = %0x, seq = %d, crc_check = %d\n",__func__,pkt->getAddr(),pkt->cxl_pkt.seqNum,pkt->cxl_pkt.crc_check);
                        if(pkt->cxl_pkt.crc_check){
                            DPRINTF(CXL_ctrl,"%s, CRC check success, sending packet to device\n",__func__);

                            if (pkt->cxl_pkt.retry_resp) {
                                ProtocolValidationEvent retry_end;
                                retry_end.event = "RETRY_END";
                                retry_end.component = ctrl->name();
                                retry_end.layer = "CXL_PROTOCOL";
                                retry_end.direction = "D2H";
                                retry_end.packetSeq = pkt->cxl_pkt.seqNum;
                                retry_end.txAttempt = 1;
                                retry_end.reason =
                                    "SELECTIVE_EXACT_SEQUENCE";
                                ProtocolValidationLogger::recordPacket(
                                    retry_end, pkt);
                            }

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
                                ctrl->retry_transmit_req(pkt_temp, curTick()+ctrl->control_flit_cycle*ctrl->clockPeriod());
                                
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

                                ctrl->countNakRx(true);
                                ProtocolValidationEvent nak_rx;
                                nak_rx.event = "NAK_RX";
                                nak_rx.component = ctrl->name();
                                nak_rx.layer = "CXL_PROTOCOL";
                                nak_rx.direction = "D2H";
                                nak_rx.packetSeq = pkt->cxl_pkt.crc_error_req_seqnum;
                                nak_rx.nakSeq = pkt->cxl_pkt.crc_error_req_seqnum;
                                nak_rx.reason = "CRC_NAK_PIGGYBACK";
                                ProtocolValidationLogger::recordPacket(
                                    nak_rx, packet ? packet : pkt);

                                //esj 2025-01-23
                                // if(packet != NULL && packet->isRequest()){
                                if(packet != NULL ){
                                    if(packet->isRequest()){
                                        DPRINTF(CXL_ctrl,"%s, crc_error_req_seqnum = %llu, not found in buffer, already sent packet\n",__func__,packet->cxl_pkt.crc_error_req_seqnum);
                                        DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.seqNum,packet->cxl_pkt.retry_resp,packet->cxl_pkt.is_controlflit);

                                        // The replay entry already holds the
                                        // piggyback ACK from the first
                                        // attempt.  Do not replace it with a
                                        // newer global response sequence.
                                        packet->cxl_pkt.LRSM_host = ctrl->LRSM.cur_stat();
                                        packet->cxl_pkt.retry_req = true;

                                        ProtocolValidationEvent retry_begin;
                                        retry_begin.event = "RETRY_BEGIN";
                                        retry_begin.component = ctrl->name();
                                        retry_begin.layer = "CXL_PROTOCOL";
                                        retry_begin.direction = "H2D";
                                        retry_begin.packetSeq =
                                            packet->cxl_pkt.seqNum;
                                        retry_begin.nakSeq =
                                            pkt->cxl_pkt.crc_error_req_seqnum;
                                        retry_begin.txAttempt = 1;
                                        retry_begin.reason =
                                            "SELECTIVE_EXACT_SEQUENCE";
                                        ProtocolValidationLogger::recordPacket(
                                            retry_begin, packet);

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
                            
                            //esj 2025-11-29
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
                            ctrl->retry_transmit_req(pkt_temp, curTick() + ctrl->crc_error_control_flit_cycle*ctrl->clockPeriod());

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
                                ctrl->retry_transmit_req(pkt_temp, curTick()+ ctrl->control_flit_cycle*ctrl->clockPeriod());
                            }
                            //esj 2025-02-19
                            //esj 2025-07-13
                            //else{
                            else if(pkt->is_cxl_write_resp){
                                // delete pkt_temp;
                                // esj 2025-05-20
                                delete pkt_temp;
                            }

                            //esj 2024-12-25
                            if(ctrl->recv_packet_num>0) {
                                ctrl->recv_packet_num--;
                                ctrl->recordHostOutstandingWindowChange(
                                    pkt, "CTRL_WINDOW_RELEASE",
                                    "RESPONSE_COMPLETED");
                                ctrl->endHostOutstandingWindowStallIfReady(pkt);
                            }
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

        // Waiting for a retry packet or its ACK must not idle the link.  A
        // ready normal request remains eligible until replay work is ready.
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
            assert(transmitList.size() != maxQueueSize);
            // assert((transmitList.size() + transmitList_write.size()) != maxQueueSize);

            //esj 2024-12-05
            // transmitList.emplace_back(pkt, when);

            if(!retransmit){
                // esj 2025-05-16
                transmitList.emplace_back(pkt, when);
                recordHostDeferredTxChange(
                    pkt, "CTRL_TX_QUEUE_ENQUEUE", "INITIAL_REQUEST");

                // Admission succeeded and the queued packet has captured its
                // exact ACK sequence.  Only now may it replace the standalone
                // ACK.  Canceling at recvTimingReq entry loses the ACK when
                // replay/window backpressure rejects the candidate request.
                if (hostReplayReservationWeight(pkt) != 0) {
                    popfromtransmitlist_req(pkt->cxl_pkt.resp_seqnum, true);
                }
                // if(pkt->isWrite()){
                //     transmitList_write.emplace_back(pkt, when);
                // }
                // else{
                //     transmitList.emplace_back(pkt, when);
                // }
            }
            else{
                DPRINTF(CXL_ctrl, "[%s] retransmit, sendEvent is not scheduled, scheduling sendEvent, when = %d\n",__func__,when);
                // if(!this->sendEvent.scheduled()){
                //     this->schedule(sendEvent, when);
                // }
            }
        }
        else{
            DPRINTF(CXL_ctrl, "[%s] retry_req = true, enqueue for RRSM arbitration, scheduling sendEvent, when = %d\n",__func__,when);
            // Keep retry packets in arrival order.  RRSM arbitration selects
            // the oldest ready retry ahead of normal traffic.
            transmitList.emplace_back(pkt, when);
            recordHostDeferredTxChange(
                pkt, "CTRL_TX_QUEUE_ENQUEUE", "RETRY_REQUEST");
            // if(pkt->isWrite()){
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
        if(!this->sendEvent.scheduled()){
            this->schedule(sendEvent, wakeup);
        }
        else if (pkt->cxl_pkt.retry_req &&
                 RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK &&
                 wakeup < sendEvent.when()) {
            this->reschedule(sendEvent, wakeup, true);
        }
       
    } 

    void
    CXL_ctrl::trySendTiming()
    {

        assert(!transmitList.empty());

        if (lrsmRequestControlReady()) {
            PacketPtr waiting_pkt = transmitList.front().pkt;
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "H2D";
            arbitration.queueId = "host_req_tx";
            arbitration.queueOccupancy = transmitList.size();
            arbitration.reason = "LRSM_CONTROL_PRIORITY";
            ProtocolValidationLogger::recordPacket(
                arbitration, waiting_pkt);

            if (!control_flit_reqEvent.scheduled()) {
                schedule(control_flit_reqEvent, curTick());
            }
            else if (control_flit_reqEvent.when() > curTick()) {
                reschedule(control_flit_reqEvent, curTick(), true);
            }
            // Do not hold the ready normal request here.  Materialize the
            // LRSM control flit in parallel and let CXL_Packing arbitrate it
            // ahead of data once both packets are available.  This also
            // preserves the existing piggyback opportunity for a request
            // that can carry the pending ACK/NAK itself.
        }

        const size_t selected_index = selectRequestTxIndex();
        DeferredPacket req = transmitList[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (pkt->cxl_pkt.retry_req &&
            RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "H2D";
            arbitration.packetSeq = pkt->cxl_pkt.seqNum;
            arbitration.queueId = "host_req_tx";
            arbitration.queueOccupancy = transmitList.size();
            arbitration.reason = selected_index == 0 ?
                "RRSM_RETRY_PRIORITY_HEAD" :
                "RRSM_RETRY_PRIORITY_BYPASS";
            ProtocolValidationLogger::recordPacket(arbitration, pkt);
        }
    
        DPRINTF(CXL_ctrl, "trySend request addr 0x%x, queue size %d\n",
                pkt->getAddr(), transmitList.size());

        if (cxl_packing->in_port.recvTimingReq(pkt)) {
            // send successful
            transmitList.erase(transmitList.begin() + selected_index);
            recordHostDeferredTxChange(
                pkt, "CTRL_TX_QUEUE_DEQUEUE", "PACKING_ACCEPTED");

            if (pkt->cxl_pkt.retry_req) {
                ++stats.exact_retry_req_tx_count;
                ProtocolValidationEvent retry_tx;
                retry_tx.event = "RETRY_TX";
                retry_tx.component = name();
                retry_tx.layer = "CXL_PROTOCOL";
                retry_tx.direction = "H2D";
                retry_tx.packetSeq = pkt->cxl_pkt.seqNum;
                retry_tx.txAttempt = 1;
                retry_tx.reason = "SELECTIVE_EXACT_SEQUENCE";
                ProtocolValidationLogger::recordPacket(retry_tx, pkt);
            }

            Host2Device_busy = false;
            // trans_state.send_packet_num++;

            //esj 2024-12-19
            // if(!pkt->cxl_pkt.retry_req){
			
            //esj 2024-12-25
            if(!pkt->cxl_pkt.retry_req && !pkt->cxl_pkt.is_controlflit && !cxl_buffer_host.findByseqnum(pkt->cxl_pkt.seqNum) && !pkt->cxl_pkt.retry_resp){
                DPRINTF(CXL_ctrl,"%s, retry_req = false, pushing packet to host buffer, addr=%0x\n",__func__, pkt->getAddr());
                const int size_before = cxl_buffer_host.size();
                cxl_buffer_host.pushBack(pkt);
                fatal_if(
                    cxl_buffer_host.size() != size_before + 1,
                    "%s could not retain H2D replay sequence %llu\n",
                    name(), (unsigned long long)pkt->cxl_pkt.seqNum);

                ProtocolValidationEvent rb_insert;
                rb_insert.event = "RB_INSERT";
                rb_insert.component = name();
                rb_insert.layer = "CXL_PROTOCOL";
                rb_insert.direction = "H2D";
                rb_insert.packetSeq = pkt->cxl_pkt.seqNum;
                rb_insert.txAttempt = 0;
                rb_insert.rbId = "host_req";
                rb_insert.rbOccupancy = cxl_buffer_host.size();
                rb_insert.rbCapacity = maxQueueSize;
                if (validationReplayPolicyEnable) {
                    rb_insert.queueId = "host_req_effective_admission";
                    rb_insert.queueOccupancy =
                        hostReplayReservedOccupancy() +
                        (pkt->cxlMergedParent &&
                         pkt->cxlMergedPkt != nullptr ? 1 : 0);
                    rb_insert.queueCapacity = hostRbStallThreshold();
                }
                if (validationReplayPolicyEnable && hostRbStallActive) {
                    rb_insert.reason = "PRE_STALL_RESERVATION_COMMIT";
                }
                ProtocolValidationLogger::recordPacket(rb_insert, pkt);


                if(pkt->cxlMergedParent && (pkt->cxlMergedPkt != nullptr)){
                    DPRINTF(CXL_ctrl,"%s, merged packet addr = %0x, seq = %d\n",__func__,pkt->cxlMergedPkt->getAddr(),pkt->cxlMergedPkt->cxl_pkt.seqNum);
                    const int merged_size_before = cxl_buffer_host.size();
                    cxl_buffer_host.pushBack(pkt->cxlMergedPkt);
                    fatal_if(
                        cxl_buffer_host.size() != merged_size_before + 1,
                        "%s could not retain merged H2D replay sequence %llu\n",
                        name(),
                        (unsigned long long)pkt->cxlMergedPkt->cxl_pkt.seqNum);
                }
                replayBufferHighWatermark = std::max(
                    replayBufferHighWatermark,
                    static_cast<uint64_t>(cxl_buffer_host.size()));

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
                DPRINTF(CXL_ctrl, "[%s] retransmit, sendrespEvent is not scheduled, scheduling sendrespEvent, when = %d\n",__func__,when);
                // if(!this->sendEventResp.scheduled()){
                //     this->schedule(sendEventResp, when);
                // }
            }
        }
        else{
            DPRINTF(CXL_ctrl, "[%s] retry_resp = true, enqueue for RRSM arbitration, scheduling sendrespEvent, when = %d\n",__func__,when);
            // Preserve retry arrival order; selectResponseTxIndex() gives the
            // oldest ready retry priority while RRSM is active.
            transmitList_resp.emplace_back(pkt, when);
            // if(!this->sendEventResp.scheduled()){
            //     this->schedule(sendEventResp, when);
            // }
        }
        const Tick wakeup = std::max(when, curTick());
        if(!this->sendEventResp.scheduled()){
            this->schedule(sendEventResp, wakeup);
        }
        else if (pkt->cxl_pkt.retry_resp &&
                 RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK &&
                 wakeup < sendEventResp.when()) {
            this->reschedule(sendEventResp, wakeup, true);
        }
       
    } 

    void
    CXL_ctrl::trySendTimingResp()
    {
        assert(!transmitList_resp.empty());

        if (lrsmResponseControlReady()) {
            PacketPtr waiting_pkt = transmitList_resp.front().pkt;
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "D2H";
            arbitration.queueId = "device_resp_tx";
            arbitration.queueOccupancy = transmitList_resp.size();
            arbitration.reason = "LRSM_CONTROL_PRIORITY";
            ProtocolValidationLogger::recordPacket(
                arbitration, waiting_pkt);

            if (!control_flit_respEvent.scheduled()) {
                schedule(control_flit_respEvent, curTick());
            }
            else if (control_flit_respEvent.when() > curTick()) {
                reschedule(control_flit_respEvent, curTick(), true);
            }
            // Keep normal responses work-conserving while the LRSM control
            // flit is created.  Downstream arbitration gives the completed
            // control flit priority without turning LRSM into a queue gate.
        }

        const size_t selected_index = selectResponseTxIndex();
        DeferredPacket req = transmitList_resp[selected_index];

        assert(req.tick <= curTick());

        PacketPtr pkt = req.pkt;

        if (pkt->cxl_pkt.retry_resp &&
            RRSM.Remote_state == CXL_Remote_state::RETRY_LLRACK) {
            ProtocolValidationEvent arbitration;
            arbitration.event = "TX_ARBITRATION";
            arbitration.component = name();
            arbitration.layer = "CXL_PROTOCOL";
            arbitration.direction = "D2H";
            arbitration.packetSeq = pkt->cxl_pkt.seqNum;
            arbitration.queueId = "device_resp_tx";
            arbitration.queueOccupancy = transmitList_resp.size();
            arbitration.reason = selected_index == 0 ?
                "RRSM_RETRY_PRIORITY_HEAD" :
                "RRSM_RETRY_PRIORITY_BYPASS";
            ProtocolValidationLogger::recordPacket(arbitration, pkt);
        }
    
        DPRINTF(CXL_ctrl, "trySend request addr 0x%x, queue size %d\n",
                    pkt->getAddr(), transmitList_resp.size());

        if (cxl_packing->out_port.recvTimingResp(pkt)) {
            // send successful
            transmitList_resp.erase(
                transmitList_resp.begin() + selected_index);

            if (pkt->cxl_pkt.retry_resp) {
                ++stats.exact_retry_resp_tx_count;
                ProtocolValidationEvent retry_tx;
                retry_tx.event = "RETRY_TX";
                retry_tx.component = name();
                retry_tx.layer = "CXL_PROTOCOL";
                retry_tx.direction = "D2H";
                retry_tx.packetSeq = pkt->cxl_pkt.seqNum;
                retry_tx.txAttempt = 1;
                retry_tx.reason = "SELECTIVE_EXACT_SEQUENCE";
                ProtocolValidationLogger::recordPacket(retry_tx, pkt);
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
            // if(!pkt->cxl_pkt.is_controlflit  && !pkt->cxl_pkt.retry_req){
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
                    replayBufferHighWatermark = std::max(
                        replayBufferHighWatermark,
                        static_cast<uint64_t>(cxl_buffer_device.size()));
                    DPRINTF(CXL_ctrl,"%s, packet pushed to buffer, seqnum = %d\n",__func__,pkt->cxl_pkt.seqNum);

                    ProtocolValidationEvent rb_insert;
                    rb_insert.event = "RB_INSERT";
                    rb_insert.component = name();
                    rb_insert.layer = "CXL_PROTOCOL";
                    rb_insert.direction = "D2H";
                    rb_insert.packetSeq = pkt->cxl_pkt.seqNum;
                    rb_insert.txAttempt = 0;
                    rb_insert.rbId = "device_resp";
                    rb_insert.rbOccupancy = cxl_buffer_device.size();
                    rb_insert.rbCapacity = maxQueueSize;
                    ProtocolValidationLogger::recordPacket(rb_insert, pkt);
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
        }
    }

    bool
    CXL_ctrl::CXL_ctrlResponsePort::canAcceptCxlRequest(PacketPtr pkt)
    {
        // Only the host Encoder -> switch request path uses this optional
        // arbitration query. Unsupported peers retain normal send/retry flow.
        if (this == &ctrl->internal_Response && ctrl->is_host &&
            ctrl->downstreamRequest.isConnected()) {
            auto *admission = dynamic_cast<CXLRequestAdmission *>(
                &ctrl->downstreamRequest.getPeer());
            if (admission)
                return admission->canAcceptCxlRequest(pkt);
        }
        return true;
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

                            if (pkt->cxl_pkt.retry_req) {
                                ProtocolValidationEvent retry_end;
                                retry_end.event = "RETRY_END";
                                retry_end.component = ctrl->name();
                                retry_end.layer = "CXL_PROTOCOL";
                                retry_end.direction = "H2D";
                                retry_end.packetSeq = pkt->cxl_pkt.seqNum;
                                retry_end.txAttempt = 1;
                                retry_end.reason =
                                    "SELECTIVE_EXACT_SEQUENCE";
                                ProtocolValidationLogger::recordPacket(
                                    retry_end, pkt);
                            }

                            ProtocolValidationEvent device_forward;
                            device_forward.event = "DEVICE_CTRL_FORWARD";
                            device_forward.component = ctrl->name();
                            device_forward.layer = "CXL_PROTOCOL";
                            device_forward.direction = "H2D";
                            device_forward.pathStage = "DEVICE_CTRL";
                            device_forward.packetSeq = pkt->cxl_pkt.seqNum;
                            device_forward.txAttempt =
                                pkt->cxl_pkt.retry_req ? 1 : 0;
                            ProtocolValidationLogger::recordPacket(
                                device_forward, pkt);

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
                                ctrl->countNakRx(false);
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

                                ProtocolValidationEvent nak_rx;
                                nak_rx.event = "NAK_RX";
                                nak_rx.component = ctrl->name();
                                nak_rx.layer = "CXL_PROTOCOL";
                                nak_rx.direction = "H2D";
                                nak_rx.packetSeq =
                                    pkt->cxl_pkt.crc_error_resp_seqnum;
                                nak_rx.nakSeq =
                                    pkt->cxl_pkt.crc_error_resp_seqnum;
                                nak_rx.reason = "CRC_NAK_PIGGYBACK";
                                ProtocolValidationLogger::recordPacket(
                                    nak_rx, packet ? packet : pkt);

                                //esj 2025-01-23
                                // if(packet != NULL && packet->isResponse() ){
                                if(packet != NULL){
                                    if(packet->isResponse()){
                                        DPRINTF(CXL_ctrl,"%s, packet addr = 0x%x, seq = %d, retry_resp = %d, is control flit = %d\n",__func__,packet->getAddr(),packet->cxl_pkt.seqNum,packet->cxl_pkt.retry_resp,packet->cxl_pkt.is_controlflit);
                                        packet->cxl_pkt.req_seqnum = ctrl->req_seqnum;

                                        //esj 2025-01-25
                                        packet->cxl_pkt.retry_resp = true;

                                        ProtocolValidationEvent retry_begin;
                                        retry_begin.event = "RETRY_BEGIN";
                                        retry_begin.component = ctrl->name();
                                        retry_begin.layer = "CXL_PROTOCOL";
                                        retry_begin.direction = "D2H";
                                        retry_begin.packetSeq =
                                            packet->cxl_pkt.seqNum;
                                        retry_begin.nakSeq =
                                            pkt->cxl_pkt.crc_error_resp_seqnum;
                                        retry_begin.txAttempt = 1;
                                        retry_begin.reason =
                                            "SELECTIVE_EXACT_SEQUENCE";
                                        ProtocolValidationLogger::recordPacket(
                                            retry_begin, packet);

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

                            //esj 2025-11-29
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
                            ctrl->error_req_transmitList.emplace_back(pkt->cxl_pkt.seqNum);
                            


                            return true;
                        }

                        if(success){
                            //esj 2024-12-22
                            // uint64_t seq_num = pkt->cxl_pkt.send_information.sen_packet_num;

                            // //esj 2024-12-26
                            // ctrl->recv_packet_num++;
                            // DPRINTF(CXL_ctrl, "recv_packet_num = %d\n",ctrl->recv_packet_num);

                            //esj 2024-12-25
                            ctrl->req_seqnum = pkt->cxl_pkt.seqNum;

                            //esj 2025-01-18
                            for(auto it = ctrl->error_req_transmitList.begin(); it != ctrl->error_req_transmitList.end(); it++){
                                if(it->crc_error_seqnum == pkt->cxl_pkt.seqNum){
                                    DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                                    it = ctrl->error_req_transmitList.erase(it);
                                    break;
                                }
                            }

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
                                const int device_rb_size_before =
                                    ctrl->cxl_buffer_device.size();
                                const uint64_t resp_ack_seq =
                                    pkt->cxl_pkt.resp_seqnum;
                                PacketPtr acked_device_resp =
                                    ProtocolValidationLogger::enabled() ?
                                    ctrl->cxl_buffer_device.peekByseqnum(
                                        resp_ack_seq) : nullptr;
                                popseq = ctrl->cxl_buffer_device.popByseqnum3(
                                    resp_ack_seq);
                                DPRINTF(CXL_ctrl, "[%s] seq_num found in buffer = %s\n", __func__, popseq ? "success" : "failed");

                                if (popseq) {
                                    if (acked_device_resp) {
                                        ProtocolValidationEvent ack;
                                        ack.event = "ACK_RX";
                                        ack.component = ctrl->name();
                                        ack.layer = "CXL_PROTOCOL";
                                        ack.direction = "H2D";
                                        ack.packetSeq = resp_ack_seq;
                                        ack.ackSeq = resp_ack_seq;
                                        ack.rbId = "device_resp";
                                        ack.reason =
                                            "DATA_PIGGYBACK_EXACT";
                                        ProtocolValidationLogger::recordPacket(
                                            ack, acked_device_resp);
                                    }

                                    ProtocolValidationEvent released;
                                    released.event = "RB_RELEASE";
                                    released.component = ctrl->name();
                                    released.layer = "CXL_PROTOCOL";
                                    released.direction = "H2D";
                                    released.packetSeq = resp_ack_seq;
                                    released.ackSeq = resp_ack_seq;
                                    released.rbId = "device_resp";
                                    released.rbOccupancy =
                                        device_rb_size_before - 1;
                                    released.rbCapacity = ctrl->maxQueueSize;
                                    released.reason =
                                        "DATA_PIGGYBACK_EXACT";
                                    ProtocolValidationLogger::recordPacket(
                                        released, pkt);
                                    ctrl->releaseDeviceReplaySnapshot(
                                        resp_ack_seq);
                                }

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
                            if (pkt->cxlMergedParent &&
                                (pkt->cxlMergedPkt != nullptr)) {
                                ctrl->recv_state.recv_packet_num++;
                            }
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
                    static unsigned host_cxl_mem_debug_prints = 0;
                    if (pkt->cxl_flag && pkt->is_cxl_mem &&
                        host_cxl_mem_debug_prints < 128) {
                        warn("esj CXL_ctrl host route port=%s addr=%#llx "
                             "origin_addr=%#llx origin_dest=%#llx size=%u "
                             "cmd=%s cxl_flag=%d cxl_io=%d cxl_mem=%d\n",
                             name().c_str(),
                             (unsigned long long)pkt->getAddr(),
                             (unsigned long long)pkt->origin_addr,
                             (unsigned long long)pkt->origin_dest,
                             pkt->getSize(), pkt->cmdString(), pkt->cxl_flag,
                             pkt->is_cxl_io, pkt->is_cxl_mem);
                        host_cxl_mem_debug_prints++;
                    }
                    
                    //esj 2024-12-13
                    DPRINTF(CXL_ctrl,"%s,packet addr = %0x\n", __func__, pkt->getAddr());
                    // if(ctrl->control_flit_reqEvent.scheduled()){
                    //     DPRINTF(CXL_ctrl,"[%s], descheduling control_flit_reqEvent\n",__func__);
                    //     ctrl->deschedule(ctrl->control_flit_reqEvent);
                    // }

                    //esj 2025-01-24
                    pkt->cxl_pkt.complete = false;
                    //


                    ctrl->request_busy = true;

                    ProtocolValidationEvent host_forward;
                    host_forward.event = "HOST_CTRL_FORWARD";
                    host_forward.component = ctrl->name();
                    host_forward.layer = "CXL_PROTOCOL";
                    host_forward.direction = "H2D";
                    host_forward.pathStage = "HOST_CTRL";
                    ProtocolValidationLogger::recordPacket(host_forward, pkt);

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
                    static unsigned device_cxl_mem_debug_prints = 0;
                    if (pkt->cxl_flag && pkt->is_cxl_mem &&
                        device_cxl_mem_debug_prints < 128) {
                        warn("esj CXL_ctrl device route port=%s addr=%#llx "
                             "origin_addr=%#llx origin_dest=%#llx size=%u "
                             "cmd=%s cxl_flag=%d cxl_io=%d cxl_mem=%d\n",
                             name().c_str(),
                             (unsigned long long)pkt->getAddr(),
                             (unsigned long long)pkt->origin_addr,
                             (unsigned long long)pkt->origin_dest,
                             pkt->getSize(), pkt->cmdString(), pkt->cxl_flag,
                             pkt->is_cxl_io, pkt->is_cxl_mem);
                        device_cxl_mem_debug_prints++;
                    }
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

        // A legacy control-flit event can remain scheduled after the packet
        // which armed it is canceled.  In strict validation mode, never let
        // that stale event expire the new queue head before its own deadline.
        if (validationStrictCftDeadline &&
            retry_transmitList_resp_size != 0 &&
            retry_transmitList_resp.front().valid &&
            retry_transmitList_resp.front().pkt != nullptr &&
            retry_transmitList_resp.front().tick > curTick()) {
            syncStrictCftRespEvent();
            return;
        }

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


                PacketPtr pending_pkt = retry_transmitList_resp.front().pkt;
                ProtocolValidationEvent expired;
                expired.event = "CFT_EXPIRE";
                expired.component = name();
                expired.layer = "CXL_PROTOCOL";
                expired.direction = "D2H";
                expired.packetSeq = pending_pkt->cxl_pkt.seqNum;
                expired.ackSeq = pending_pkt->cxl_pkt.seqNum;
                expired.timerId = name() + "-resp-" +
                    std::to_string(pending_pkt->cxl_pkt.seqNum);
                expired.expiryTick = retry_transmitList_resp.front().tick;
                expired.reason = pending_pkt->cxl_pkt.crc_check ?
                    "ACK" : "NAK";
                ProtocolValidationLogger::recordPacket(expired, pending_pkt);

                PacketPtr resp_pkt = make_control_flit_resp(pending_pkt);

                ProtocolValidationEvent generated = expired;
                generated.event = "CTRL_FLIT_GEN";
                generated.reason = pending_pkt->cxl_pkt.crc_check ?
                    "CFT_EXPIRE_ACK" : "CFT_EXPIRE_NAK";
                ProtocolValidationLogger::recordPacket(generated, pending_pkt);

                //esj 2025-01-23
                for(auto it = error_req_transmitList.begin(); it != error_req_transmitList.end(); it++){
                    if(it->crc_error_seqnum == retry_transmitList_resp.front().pkt->cxl_pkt.seqNum){
                        DPRINTF(CXL_ctrl,"%s, found error_req_transmitList, erase it\n",__func__);
                        it = error_req_transmitList.erase(it);
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
                        auto replay_size = retry_buffer_resp.size();
                        if (replay_size > replayBufferHighWatermark) {
                            replayBufferHighWatermark = replay_size;
                        }
                        DPRINTF(CXL_ctrl,
                                "%s, push packet to retry_buffer_resp, "
                                "seq = %d\n",
                                __func__, resp_pkt->cxl_pkt.seqNum);
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
                    ++stats.cft_expiry_count;
                    ++stats.control_flit_tx_count;
                    // A successful retry keeps retry_req set, but this
                    // control flit acknowledges recovery; it is not a NAK.
                    const bool is_nak = !resp_pkt->cxl_pkt.crc_check;
                    if (is_nak)
                        ++stats.nak_tx_count;
                    else
                        ++stats.standalone_ack_tx_count;
                    ProtocolValidationEvent control_tx;
                    control_tx.event = is_nak ? "NAK_TX" : "ACK_TX";
                    control_tx.component = name();
                    control_tx.layer = "CXL_PROTOCOL";
                    control_tx.direction = "D2H";
                    control_tx.packetSeq = resp_pkt->cxl_pkt.seqNum;
                    if (is_nak)
                        control_tx.nakSeq = resp_pkt->cxl_pkt.seqNum;
                    else
                        control_tx.ackSeq = resp_pkt->cxl_pkt.seqNum;
                    control_tx.reason = is_nak ?
                        "CRC_NAK" : "CONTROL_ACK";
                    ProtocolValidationLogger::recordPacket(
                        control_tx, retry_transmitList_resp.front().pkt);

                    //esj 2025-02-24
                    if(retry_transmitList_resp.front().pkt->isWrite()){
                        stats.control_flit_req_write++;
                    }
                    else{
                        stats.control_flit_req_read++;
                    }
                    if (resp_pkt->cxl_pkt.retry_ack) {
                        stats.ack_control_flit_count++;
                        stats.retry_ack_count++;
                    }
                    if (resp_pkt->cxl_pkt.retry_req ||
                        resp_pkt->cxl_pkt.retry_resp ||
                        !resp_pkt->cxl_pkt.crc_check) {
                        stats.nak_control_flit_count++;
                    }
                    if (resp_pkt->cxl_pkt.retry_req) {
                        stats.retry_req_count++;
                    }
                    if (resp_pkt->cxl_pkt.retry_resp) {
                        stats.retry_resp_count++;
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

        // See control_flit_respfunc(): strict mode treats each queued
        // packet's tick as a not-before deadline even if an obsolete shared
        // event fires first.
        if (validationStrictCftDeadline &&
            retry_transmitList_req_size != 0 &&
            retry_transmitList_req.front().valid &&
            retry_transmitList_req.front().pkt != nullptr &&
            retry_transmitList_req.front().tick > curTick()) {
            syncStrictCftReqEvent();
            return;
        }


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
                
                PacketPtr pending_pkt = retry_transmitList_req.front().pkt;
                ProtocolValidationEvent expired;
                expired.event = "CFT_EXPIRE";
                expired.component = name();
                expired.layer = "CXL_PROTOCOL";
                expired.direction = "H2D";
                expired.packetSeq = pending_pkt->cxl_pkt.seqNum;
                expired.ackSeq = pending_pkt->cxl_pkt.seqNum;
                expired.timerId = name() + "-req-" +
                    std::to_string(pending_pkt->cxl_pkt.seqNum);
                expired.expiryTick = retry_transmitList_req.front().tick;
                expired.reason = pending_pkt->cxl_pkt.crc_check ?
                    "ACK" : "NAK";
                ProtocolValidationLogger::recordPacket(expired, pending_pkt);

                PacketPtr req_pkt = make_control_flit_req(pending_pkt);

                ProtocolValidationEvent generated = expired;
                generated.event = "CTRL_FLIT_GEN";
                generated.reason = pending_pkt->cxl_pkt.crc_check ?
                    "CFT_EXPIRE_ACK" : "CFT_EXPIRE_NAK";
                ProtocolValidationLogger::recordPacket(generated, pending_pkt);

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
                        auto replay_size = retry_buffer_req.size();
                        if (replay_size > replayBufferHighWatermark) {
                            replayBufferHighWatermark = replay_size;
                        }
                        DPRINTF(CXL_ctrl,
                                "%s, push packet to retry_buffer_req, "
                                "seq = %d\n",
                                __func__, req_pkt->cxl_pkt.seqNum);
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
                    ++stats.cft_expiry_count;
                    ++stats.control_flit_tx_count;
                    // A successful retry keeps retry_resp set, but this
                    // control flit acknowledges recovery; it is not a NAK.
                    const bool is_nak = !req_pkt->cxl_pkt.crc_check;
                    if (is_nak)
                        ++stats.nak_tx_count;
                    else
                        ++stats.standalone_ack_tx_count;
                    ProtocolValidationEvent control_tx;
                    control_tx.event = is_nak ? "NAK_TX" : "ACK_TX";
                    control_tx.component = name();
                    control_tx.layer = "CXL_PROTOCOL";
                    control_tx.direction = "H2D";
                    control_tx.packetSeq = req_pkt->cxl_pkt.seqNum;
                    if (is_nak)
                        control_tx.nakSeq = req_pkt->cxl_pkt.seqNum;
                    else
                        control_tx.ackSeq = req_pkt->cxl_pkt.seqNum;
                    control_tx.reason = is_nak ?
                        "CRC_NAK" : "CONTROL_ACK";
                    ProtocolValidationLogger::recordPacket(
                        control_tx, retry_transmitList_req.front().pkt);

                    //esj 2025-02-24
                    if(retry_transmitList_req.front().pkt->isWrite()){
                        stats.control_flit_resp_write++;
                    }
                    else{
                        stats.control_flit_resp_read++;
                    }
                    if (req_pkt->cxl_pkt.retry_ack) {
                        stats.ack_control_flit_count++;
                        stats.retry_ack_count++;
                    }
                    if (req_pkt->cxl_pkt.retry_req ||
                        req_pkt->cxl_pkt.retry_resp ||
                        !req_pkt->cxl_pkt.crc_check) {
                        stats.nak_control_flit_count++;
                    }
                    if (req_pkt->cxl_pkt.retry_req) {
                        stats.retry_req_count++;
                    }
                    if (req_pkt->cxl_pkt.retry_resp) {
                        stats.retry_resp_count++;
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
        int error_req_transmitList_size = error_req_transmitList.size();
        DPRINTF(CXL_ctrl, "[%s] error_req_transmitList size = %d\n", __func__,error_req_transmitList_size);
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
                    auto replay_size = retry_buffer_resp.size();
                    if (replay_size > replayBufferHighWatermark) {
                        replayBufferHighWatermark = replay_size;
                    }
                    DPRINTF(CXL_ctrl,
                            "%s, push packet to retry_buffer_resp, "
                            "seq = %d\n",
                            __func__, pkt->cxl_pkt.seqNum);
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
                    auto replay_size = retry_buffer_req.size();
                    if (replay_size > replayBufferHighWatermark) {
                        replayBufferHighWatermark = replay_size;
                    }
                    DPRINTF(CXL_ctrl,
                            "%s, push packet to retry_buffer_req, "
                            "seq = %d\n",
                            __func__, pkt->cxl_pkt.seqNum);
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
    CXL_ctrl::syncStrictCftReqEvent()
    {
        if (!validationStrictCftDeadline)
            return;

        if (control_flit_reqEvent.scheduled())
            deschedule(control_flit_reqEvent);

        if (!retry_transmitList_req.empty()) {
            schedule(control_flit_reqEvent,
                     std::max(retry_transmitList_req.front().tick,
                              curTick()));
        }
    }

    void
    CXL_ctrl::syncStrictCftRespEvent()
    {
        if (!validationStrictCftDeadline)
            return;

        if (control_flit_respEvent.scheduled())
            deschedule(control_flit_respEvent);

        if (!retry_transmitList_resp.empty()) {
            schedule(control_flit_respEvent,
                     std::max(retry_transmitList_resp.front().tick,
                              curTick()));
        }
    }

    void
    CXL_ctrl::retry_transmit_req(PacketPtr pkt, Tick when){
        // if (retry_transmitList_req.empty()) {
        //         DPRINTF(CXL_ctrl, "[%s] scheduling control_flit_reqEvent, when = %d, transmitList size %d\n",__func__,when, retry_transmitList_req.size());
        //         schedule(control_flit_reqEvent, when);
        //     }
        DPRINTF(CXL_ctrl, "[%s]trySend request addr 0x%x, transmitList size %d\n",__func__,pkt->getAddr(), retry_transmitList_req.size());
        stats.retransmit_req_count++;
        stats.cft_arm_count++;

        //esj 2024-12-23
        // assert(retry_transmitList_req.size() != maxQueueSize);

        //esj 2025-01-02
        //esj 2025-01-17
        // check_seqnum_req(pkt->cxl_pkt.seqNum);									  								  

        if (validationStrictCftDeadline) {
            DeferredPacket dp(pkt, when);
            auto position = retry_transmitList_req.begin();
            while (position != retry_transmitList_req.end() &&
                   position->tick <= when) {
                ++position;
            }
            retry_transmitList_req.insert(position, dp);
        }
        else if(pkt->cxl_pkt.retry_resp){
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

        ProtocolValidationEvent armed;
        armed.event = "CFT_ARM";
        armed.component = name();
        armed.layer = "CXL_PROTOCOL";
        armed.direction = "H2D";
        armed.packetSeq = pkt->cxl_pkt.seqNum;
        armed.ackSeq = pkt->cxl_pkt.seqNum;
        armed.timerId =
            name() + "-req-" + std::to_string(pkt->cxl_pkt.seqNum);
        armed.expiryTick = when;
        armed.reason = pkt->cxl_pkt.crc_check ? "ACK" : "NAK";
        ProtocolValidationLogger::recordPacket(armed, pkt);

        
        if (validationStrictCftDeadline) {
            syncStrictCftReqEvent();
        }
        else if(!this->control_flit_reqEvent.scheduled()){
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
        stats.retransmit_resp_count++;
        stats.cft_arm_count++;

        //esj 2024-12-23
        // assert(retry_transmitList_resp.size() != maxQueueSize);

        //esj 2025-01-02
        //esj 2025-01-17
        // check_seqnum_resp(pkt->cxl_pkt.seqNum);

        if (validationStrictCftDeadline) {
            DeferredPacket dp(pkt, when);
            auto position = retry_transmitList_resp.begin();
            while (position != retry_transmitList_resp.end() &&
                   position->tick <= when) {
                ++position;
            }
            retry_transmitList_resp.insert(position, dp);
        }
        else if(pkt->cxl_pkt.retry_req){
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

        ProtocolValidationEvent armed;
        armed.event = "CFT_ARM";
        armed.component = name();
        armed.layer = "CXL_PROTOCOL";
        armed.direction = "D2H";
        armed.packetSeq = pkt->cxl_pkt.seqNum;
        armed.ackSeq = pkt->cxl_pkt.seqNum;
        armed.timerId =
            name() + "-resp-" + std::to_string(pkt->cxl_pkt.seqNum);
        armed.expiryTick = when;
        armed.reason = pkt->cxl_pkt.crc_check ? "ACK" : "NAK";
        ProtocolValidationLogger::recordPacket(armed, pkt);
        

        // DPRINTF(CXL_ctrl, "[%s] retry_num(Device --> Host) = %d\n",__func__,retry_transmitList_resp.size());

        // DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_resp is not scheduled, scheduling retry_transmit_resp, when = %d\n",__func__,when);
        if (validationStrictCftDeadline) {
            syncStrictCftRespEvent();
        }
        else if(!this->control_flit_respEvent.scheduled()){
            //esj 2024-12-17
            DPRINTF(CXL_ctrl, "[%s] retransmit, retry_transmit_resp is not scheduled, scheduling retry_transmit_resp, when = %d\n",__func__,when);
            this->schedule(control_flit_respEvent, when);
        }
    }


    // Create and schedule sequence event
    void
    CXL_ctrl::createSeqEvent(uint64_t seqNum) {
        DPRINTF(CXL_ctrl, "[%s] Creating event for seqNum=%d\n", __func__, seqNum);
        
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
            return;
        }

        //esj 2024-12-25
        if(temp->isResponse()){
            DPRINTF(CXL_ctrl, "[%s] skip timeout: packet is already responsed\n",__func__);
            cancelSeqEvent(seqNum);
            const int rb_size_before = cxl_buffer_host.size();
            const bool removed = cxl_buffer_host.popByseqnum(seqNum);
            if (removed) {
                ProtocolValidationEvent released;
                released.event = "RB_RELEASE";
                released.component = name();
                released.layer = "CXL_PROTOCOL";
                released.direction = "D2H";
                released.packetSeq = seqNum;
                released.rbId = "host_req";
                released.rbOccupancy = rb_size_before - 1;
                released.rbCapacity = maxQueueSize;
                released.reason = "TIMEOUT_RESPONSE_CLEANUP";
                ProtocolValidationLogger::recordPacket(released, temp);
                endHostRbStallIfReady(temp);
            }
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
        stats.seq_timeout_req_count++;
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
        
        // Create new event
        auto event = new EventFunctionWrapper(
            [this, seqNum]{ handleSeqTimeout(seqNum); }, 
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
        stats.seq_timeout_resp_count++;
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
        // DPRINTF(CXL_ctrl,"%s\n",__func__);
        static unsigned atomic_debug_prints = 0;
        const bool print_atomic_debug = atomic_debug_prints < 128;
        if (print_atomic_debug) {
            warn("esj CXL_ctrl::recvAtomic enter port=%s addr=%#llx "
                 "size=%u cxl_flag=%d cxl_io=%d cxl_mem=%d host=%d\n",
                 name().c_str(), (unsigned long long)pkt->getAddr(),
                 pkt->getSize(), pkt->cxl_flag, pkt->is_cxl_io,
                 pkt->is_cxl_mem, ctrl->is_host);
            atomic_debug_prints++;
        }
        Tick delay = 0;
        std::string portname = name();
        if (pkt->cxl_flag && pkt->is_cxl_io &&
            portname.find("upstreamResponse2") != std::string::npos &&
            !ctrl->is_host) {
            if (print_atomic_debug)
                warn("esj CXL_ctrl::recvAtomic route %s -> "
                     "upstreamRequest2 direct CXL.io addr=%#llx\n",
                     name().c_str(),
                     (unsigned long long)pkt->getAddr());
            delay = ctrl->upstreamRequest2.sendAtomic(pkt);
        }
        else if (pkt->cxl_flag & pkt->is_cxl_mem) {
            if (portname.find("upstreamResponse") != std::string::npos) {
                if (print_atomic_debug)
                    warn("esj CXL_ctrl::recvAtomic route %s -> "
                         "downstreamRequest addr=%#llx\n",
                         name().c_str(),
                         (unsigned long long)pkt->getAddr());
                delay = ctrl->downstreamRequest.sendAtomic(pkt);
            }
            else if (portname.find("downstreamResponse") != std::string::npos){
                if (print_atomic_debug)
                    warn("esj CXL_ctrl::recvAtomic route %s -> "
                         "upstreamRequest addr=%#llx\n",
                         name().c_str(),
                         (unsigned long long)pkt->getAddr());
                delay = ctrl->upstreamRequest.sendAtomic(pkt);
            }
        }
        else {
            if (portname.find("upstreamResponse") != std::string::npos) {
                if (print_atomic_debug)
                    warn("esj CXL_ctrl::recvAtomic route %s -> "
                         "downstreamRequest addr=%#llx non_cxl_or_io\n",
                         name().c_str(),
                         (unsigned long long)pkt->getAddr());
                delay = ctrl->downstreamRequest.sendAtomic(pkt);
            }
            else if (portname.find("downstreamResponse") != std::string::npos){
                if (print_atomic_debug)
                    warn("esj CXL_ctrl::recvAtomic route %s -> "
                         "upstreamRequest addr=%#llx non_cxl_or_io\n",
                         name().c_str(),
                         (unsigned long long)pkt->getAddr());
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
