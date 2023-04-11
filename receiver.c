#include "receiver.h"
#include <stdbool.h>
#include<string.h>

void init_receiver(Receiver* receiver, int id) {
    pthread_cond_init(&receiver->buffer_cv, NULL);
    pthread_mutex_init(&receiver->buffer_mutex, NULL);
    receiver->recv_id = id;
    receiver->input_framelist_head = NULL;
    receiver->active = 1;
    //memset(receiver->seq_num_arr, 0, sizeof(receiver->seq_num_arr));
    memset(receiver->sender_msg, '\0', sizeof(receiver->sender_msg));
    
    int i, j;
    for(i = 0; i < 256; ++i) {
        for(j = 0; j < RWS; ++j) {
            receiver->recvQs[i][j].msg = NULL;
        }
    }

    memset(receiver->is_connected, 0, sizeof(receiver->is_connected));
    memset(receiver->LFRs, 255, sizeof(receiver->LFRs)); // 255 = -1
    memset(receiver->LAFs, 7, sizeof(receiver->LAFs)); 
    memset(receiver->is_first, 0, sizeof(receiver->is_first));

}

void handle_incoming_frames(Receiver* receiver,
                          LLnode** outgoing_frames_head_ptr) {
    // TODO: Suggested steps for handling incoming frames
    //    1) Dequeue the Frame from the sender->input_framelist_head
    //    2) Compute CRC of incoming frame to know whether it is corrupted
    //    3) If frame is corrupted, drop it and move on.
    //    4) If frame is not corrupted, convert incoming frame from char* to Frame* data type
    //    5) Implement logic to check if the expected frame has come
    //    6) Implement logic to combine payload received from all frames belonging to a message
    //       and print the final message when all frames belonging to a message have been received.
    //    7) ACK the received frame
    int incoming_frames_length = ll_get_length(receiver->input_framelist_head);
    while (incoming_frames_length > 0) {
        // printf("before, incoming_frames_length: %d\n", incoming_frames_length);
        // Pop a node off the front of the link list and update the count
        LLnode* ll_inmsg_node = ll_pop_node(&receiver->input_framelist_head);
        incoming_frames_length = ll_get_length(receiver->input_framelist_head);
        // printf("after, incoming_frames_length: %d\n", incoming_frames_length);
        char* raw_char_buf = ll_inmsg_node->value;
        Frame* inframe = convert_char_to_frame(raw_char_buf);
        //check if ack is corrupted(check CRC)
        bool checkCRC = compute_crc8(raw_char_buf) == 0;

        // printf("inframe->seqNum[%d]: %d\n", inframe->src_id,inframe->seqNum);

        // printf("CRC in receiver[%d]: %d\n", inframe->seqNum,compute_crc8(raw_char_buf));

        if(!checkCRC || inframe->dst_id != receiver->recv_id) { 
            free(raw_char_buf);
            free(inframe);
            free(ll_inmsg_node);
            continue; 
        }

        int sender_id = inframe->src_id;

        if(receiver->is_connected[sender_id] == 0 && strcmp(inframe->data, "SYN") == 0) {
            // printf("receive syn\n");
            receiver->is_connected[sender_id] = 1;
            ++receiver->LFRs[sender_id];
            receiver->LAFs[sender_id] = receiver->LFRs[sender_id] + RWS;
            receiver->recvQs[sender_id][inframe->seqNum % RWS].msg = inframe;
            memset(inframe->data, 0, sizeof(inframe->data));
            const char* syn_ack = "SYN-ACK";
            strncpy(inframe->data, syn_ack, strlen(syn_ack));
            inframe->src_id = receiver->recv_id;
            inframe->dst_id = sender_id;
            inframe->crcField = 0x0;
            char* compute_crc8_charbuf = convert_frame_to_char(inframe);
            inframe->crcField = compute_crc8(compute_crc8_charbuf);
            free(compute_crc8_charbuf);
            // Convert the message to the outgoing_charbuf
            char* outgoing_charbuf = convert_frame_to_char(inframe);
            ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);        

            // Free raw_char_buf
            free(raw_char_buf);

            //free(outgoing_ack);
            free(ll_inmsg_node);

            // printf("inframe->data: %s\n", inframe->data);

            continue;
        }

        // check if the connection is established        
        if(receiver->is_connected[sender_id] == 0) {
            free(raw_char_buf);
            free(inframe);
            free(ll_inmsg_node);
            return;
        }
        // printf("\n");
        // printf("receiver inframe data[%d]: %s\n",inframe->seqNum, inframe->data);
        // if(receiver->recvQs[sender_id][inframe->seqNum % RWS].msg != NULL) {
        //     free(raw_char_buf);
        //     free(inframe);
        //     free(ll_inmsg_node);
        //     continue;
        // }

        // printf("inframe->seqNum[%d]: %d\n", inframe->src_id,inframe->seqNum);
        // printf("receiver[%d]->LFRs[%d]: %d\n", receiver->recv_id,sender_id, receiver->LFRs[sender_id]);
        // printf("receiver[%d]->LAFs[%d]: %d\n", receiver->recv_id,sender_id, receiver->LAFs[sender_id]);
        // printf("receiver->seq_num_arr[%d]: %d\n",sender_id ,receiver->seq_num_arr[sender_id]);
        // printf("diff_inframe_seq_and_LFR: %d\n",inframe->seqNum - receiver->LFRs[sender_id]);
        // 0-255 = 1
        uint8_t diff_inframe_seq_and_LFR = inframe->seqNum - receiver->LFRs[sender_id];
        if(diff_inframe_seq_and_LFR == 1) {
            // put inframe to proper position based on its seqNum(next to current LFR) in recvQ
            // free the frame at current LFR position in rendQ
            // update new LFR and new LAF
            receiver->recvQs[sender_id][inframe->seqNum % RWS].msg = inframe;
            if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL) {
                free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
            }
            receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;
            receiver->LFRs[sender_id] = inframe->seqNum;
            receiver->LAFs[sender_id] = receiver->LFRs[sender_id] + RWS;

            char* original_data = receiver->sender_msg[sender_id]; 
            long original_data_len = receiver->sender_msg[sender_id] == NULL? 0 : strlen(original_data);
            char* inframe_data = inframe->data;
            long inframe_data_len = inframe_data == NULL? 0 : strlen(inframe_data);
            // printf("inframe_data_len: %ld\n", inframe_data_len);
            char* new_data = (char *)calloc(original_data_len + inframe_data_len + 1, sizeof(char));
            //memset(newDest, '\0', strlen(strMap[0]) + strlen(newSrc) + 1);
            memcpy(new_data, original_data, original_data_len);
            memcpy(new_data + original_data_len, inframe_data, inframe_data_len);
            if(receiver->is_first[sender_id] > 0) {
                free(original_data);
            }
            receiver->is_first[sender_id] = 1; 
            receiver->sender_msg[sender_id] = new_data;
            
            //uint8_t LFR = receiver->LFRs[sender_id];    
            int i;
            for(i = 0; i < RWS; ++i) {
                if(receiver->recvQs[sender_id][(receiver->LFRs[sender_id] + 1) % RWS].msg == NULL) {
                    break;
                }

                free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
                receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;
                ++receiver->LFRs[sender_id];
                receiver->LAFs[sender_id] = receiver->LFRs[sender_id] + RWS;

                char* original_data = receiver->sender_msg[sender_id]; 
                long original_data_len = receiver->sender_msg[sender_id] == NULL? 0 : strlen(original_data);
                char* inframe_data = receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data;
                long inframe_data_len = inframe_data == NULL? 0 : strlen(inframe_data);
                char* new_data = (char *)calloc(original_data_len + inframe_data_len + 1, sizeof(char));
                //memset(newDest, '\0', strlen(strMap[0]) + strlen(newSrc) + 1);
                memcpy(new_data, original_data, original_data_len);
                memcpy(new_data + original_data_len, inframe_data, inframe_data_len);
                if(receiver->is_first[sender_id] > 0) {
                    free(original_data);
                }
                receiver->is_first[sender_id] = 1;
                receiver->sender_msg[sender_id] = new_data;
            }

            // printf("inframe->remaining_msg_bytes: %d\n", inframe->remaining_msg_bytes);
            // if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL && 
            //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes <= 0) {
            //     printf("<RECV_%d>:[%s]\n", receiver->recv_id, receiver->sender_msg[sender_id]);
            //     receiver->sender_msg[sender_id] = NULL;
            //     free(new_data);
            // }
            if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL && 
                 receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes <= 0) {
            
                // if(receiver->sender_msg[sender_id] == NULL) {
                //     printf("NULL!! <SEND_%d>:[%s]\n", sender_id, receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data);
                // }
                printf("<RECV_%d>:[%s]\n", receiver->recv_id, receiver->sender_msg[sender_id]);
                free(receiver->sender_msg[sender_id]);
                receiver->sender_msg[sender_id] = NULL;
                free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
                receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;

                // free(new_data);
            }
        }
        else if(diff_inframe_seq_and_LFR > 1 && diff_inframe_seq_and_LFR <= RWS && 
                    receiver->recvQs[sender_id][inframe->seqNum % RWS].msg == NULL) {

            // printf("receiver->recvQs[%d][%d].msg is NULL!!\n", sender_id,inframe->seqNum % RWS);                        
            receiver->recvQs[sender_id][inframe->seqNum % RWS].msg = inframe;
        }
        else {
            // printf("free inframe\n");
            free(inframe);
        }

        // send ack back
        Frame* outgoing_ack = calloc(1, sizeof(Frame));
        // printf("data Length: %ld\n", strlen(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data));
        char outgoing_msg[FRAME_PAYLOAD_SIZE];
        memset(outgoing_msg, '\0', sizeof(outgoing_msg));
        memset(outgoing_ack->data, '\0', sizeof(outgoing_ack->data));
        // if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL) {
        //     strncpy(outgoing_ack->data, receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data, 
        //                             strlen(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data));
        // }
        // else {
        //     strncpy(outgoing_ack->data, outgoing_msg, sizeof(outgoing_msg));
        // }
        
        // inframe->remaining_msg_bytes = 0;             
        outgoing_ack->src_id = receiver->recv_id;
        outgoing_ack->dst_id = sender_id;
        outgoing_ack->crcField = 0x0;
        outgoing_ack->seqNum = receiver->LFRs[sender_id];
        char* compute_crc8_charbuf = convert_frame_to_char(outgoing_ack);
        outgoing_ack->crcField = compute_crc8(compute_crc8_charbuf);
        free(compute_crc8_charbuf);

        // Convert the message to the outgoing_charbuf
        char* outgoing_charbuf = convert_frame_to_char(outgoing_ack);
        ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);        

        // Free raw_char_buf
        free(raw_char_buf);

        free(outgoing_ack);
        free(ll_inmsg_node);

        // if(diff_inframe_seq_and_LFR == 0 || diff_inframe_seq_and_LFR > RWS) {
        //     free(inframe);
        // }
        // if it is last message frame
        
        // if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL &&
        //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes <= 0) {
            
        //     printf("sender_id: %d\n", sender_id);
        //     printf("window idx: %d\n", receiver->LFRs[sender_id] % RWS);
        //     if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL) {
        //         printf("have msg at [%d][%d]\n", sender_id, receiver->LFRs[sender_id] % RWS);
        //         printf("remaining msg: %d\n", receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes);
        //     }


        //     free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
        //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;
        // }

        // if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL && 
        //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes <= 0) {
            
        //     if(receiver->sender_msg[sender_id] == NULL) {
        //         printf("NULL!! <SEND_%d>:[%s]\n", sender_id, receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->data);
        //     }
        //     printf("<RECV_%d>:[%s]\n", receiver->recv_id, receiver->sender_msg[sender_id]);
        //     free(receiver->sender_msg[sender_id]);
        //     receiver->sender_msg[sender_id] = NULL;
        //     free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
        //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;
        // }
        // if(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg != NULL && 
        //          receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg->remaining_msg_bytes <= 0) {
        
        //     free(receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg);
        //     receiver->recvQs[sender_id][receiver->LFRs[sender_id] % RWS].msg = NULL;
        //     int i;
        //     for(i = 0; i < RWS; ++i) {
        //         if(receiver->recvQs[sender_id][i].msg != NULL) {
        //             free(receiver->recvQs[sender_id][i].msg);
        //             receiver->recvQs[sender_id][i].msg = NULL;
        //         }
        //     }
        // }

    }
}

void* run_receiver(void* input_receiver) {
    struct timespec time_spec;
    struct timeval curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Receiver* receiver = (Receiver*) input_receiver;
    LLnode* outgoing_frames_head;

    // This incomplete receiver thread. At a high level, it loops as follows:
    // 1. Determine the next time the thread should wake up if there is nothing
    // in the input_framelist
    // 2. Grab the mutex protecting the input_framelist 
    // 3. Dequeues frames from the input_framelist and prints them
    // 4. Releases the lock
    // 5. Sends out any outgoing frames

    while (1) {
        // NOTE: Add outgoing frames to the outgoing_frames_head pointer
        outgoing_frames_head = NULL;
        gettimeofday(&curr_timeval, NULL);

        // Either timeout or get woken up because you've received a frame
        // NOTE: You don't really need to do anything here, but it might be
        // useful for debugging purposes to have the receivers periodically
        // wakeup and print info
        time_spec.tv_sec = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;
        time_spec.tv_sec += WAIT_SEC_TIME;
        time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        if (time_spec.tv_nsec >= 1000000000) {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        //*****************************************************************************************
        // NOTE: Anything that involves dequeing from the input frames should go
        //      between the mutex lock and unlock, because other threads
        //      CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&receiver->buffer_mutex);

        // Check whether anything arrived
        int incoming_frames_length =
            ll_get_length(receiver->input_framelist_head);
        if (incoming_frames_length == 0) {
            // Nothing has arrived, do a timed wait on the condition variable
            // (which releases the mutex). Again, you don't really need to do
            // the timed wait. A signal on the condition variable will wake up
            // the thread and reacquire the lock
            pthread_cond_timedwait(&receiver->buffer_cv,
                                   &receiver->buffer_mutex, &time_spec);
        }
        // printf("handle_incoming_frames\n");
        handle_incoming_frames(receiver, &outgoing_frames_head);
        receiver->active = ll_get_length(outgoing_frames_head) > 0 ? 1:0;
        pthread_mutex_unlock(&receiver->buffer_mutex);

        // DO NOT CHANGE BELOW CODE
        // Send out all the frames user has appended to the outgoing_frames list
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        while (ll_outgoing_frame_length > 0) {
            LLnode* ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char* char_buf = (char*) ll_outframe_node->value;

            // The following function will free the memory for the char_buf object.
            // The function will convert char_buf to frame and deliver it to 
            // the sender having sender->send_id = frame->dst_id.
            send_msg_to_sender(char_buf);
            // Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);
}