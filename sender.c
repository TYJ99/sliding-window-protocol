#include "sender.h"
#include <stdbool.h>
#include <assert.h>

void init_sender(Sender* sender, int id) {
    pthread_cond_init(&sender->buffer_cv, NULL);
    pthread_mutex_init(&sender->buffer_mutex, NULL);
    sender->send_id = id;
    sender->input_cmdlist_head = NULL;
    sender->input_framelist_head = NULL;
    sender->active = 1;
    sender->awaiting_msg_ack = 0;
    // TODO: You should fill in this function as necessary    
    //gettimeofday(&sender->sendTime, NULL);
    memset(sender->curr_seq_num, 0, sizeof(sender->curr_seq_num));
    sender->msglist_head = NULL; 
    //sender->curr_sending_charbuf = NULL;
    gettimeofday(&sender->next_expiring_timeval, NULL);  

    int i;
    for(i = 0; i < SWS; ++i) {
            sender->sendQ[i].msg = NULL;
            gettimeofday(&sender->sendQ[i].sending_time, NULL);
            // sender->sendQ[i].hasResent = 0;
    }

    memset(sender->is_connected, 0, sizeof(sender->is_connected));
    memset(sender->LARs, 255, sizeof(sender->LARs)); // 255 = -1
    memset(sender->LFSs, 255, sizeof(sender->LFSs)); // 255 = -1

}

struct timeval* sender_get_next_expiring_timeval(Sender* sender) {
    uint8_t i, hasNextTime;
    hasNextTime = 0;
    // struct timeval minTime;
    for(i = 0; i < SWS; ++i) {        
        if(0 == hasNextTime) {
            if(sender->sendQ[i].msg != NULL) {
                sender->next_expiring_timeval.tv_usec = sender->sendQ[i].sending_time.tv_usec;
                sender->next_expiring_timeval.tv_sec = sender->sendQ[i].sending_time.tv_sec;
                hasNextTime = 1;
            }
        }else {
            if(sender->sendQ[i].msg != NULL) {
                long diff = timeval_usecdiff(&sender->next_expiring_timeval, &sender->sendQ[i].sending_time);
                if(diff < 0) {
                    sender->next_expiring_timeval.tv_usec = sender->sendQ[i].sending_time.tv_usec;
                    sender->next_expiring_timeval.tv_sec = sender->sendQ[i].sending_time.tv_sec;
                }
            }
        }
    }
    if(0 == hasNextTime) {
        return NULL;
    }
    sender->next_expiring_timeval.tv_usec += 90000;
    if (sender->next_expiring_timeval.tv_usec >= 1000000) {
        sender->next_expiring_timeval.tv_sec++;
        sender->next_expiring_timeval.tv_usec -= 1000000;
    }
    return &sender->next_expiring_timeval;
}

void handle_incoming_acks(Sender* sender, LLnode** outgoing_frames_head_ptr) {
    // TODO: Suggested steps for handling incoming ACKs
    //    1) Dequeue the ACK from the sender->input_framelist_head
    //    2) Convert the incoming frame from char* to Frame* data type
    //    3) Implement logic as per stop and wait ARQ to track ACK for what frame is expected,
    //       and what to do when ACK for expected frame is received

    //- the incoming ack frame's destination id matches the sender's id
    //- the incoming ack frame's crc8 returns 0
    //- the incoming ack frame's sequence number is the same as 
    // a global sender sequence number that was updated when the sender sent the original frame
    // if any one of the above is not met, the sender has to resend the original frame
    int incoming_ack_length = ll_get_length(sender->input_framelist_head);
    while(incoming_ack_length > 0) {
        // printf("in sender, incoming_ack_length: %d\n", incoming_ack_length);
        LLnode* ll_input_frame_node = ll_pop_node(&sender->input_framelist_head);
        // Cast to Frame type and free up the memory for the node
        incoming_ack_length = ll_get_length(sender->input_framelist_head);
        char* raw_char_buf = ll_input_frame_node->value;
        //check if ack is corrupted(check CRC)
        bool checkCRC = compute_crc8(raw_char_buf) == 0;
        Frame* incoming_ack = convert_char_to_frame(raw_char_buf);
        // printf("CRC in sender[%d]: %d\n", incoming_ack->seqNum, compute_crc8(raw_char_buf));
        // Free raw_char_buf
        free(raw_char_buf);

        uint8_t recv_id = incoming_ack->src_id;

        if(incoming_ack->dst_id != sender->send_id || !checkCRC) {
            free(incoming_ack);
            free(ll_input_frame_node);
            return;
        }

        if(sender->is_connected[recv_id] == 0 && strcmp(incoming_ack->data, "SYN-ACK") == 0) {
            //printf("receive syn-ack, seqNum = %d\n", incoming_ack->seqNum);
            sender->is_connected[recv_id] = 1;
        }
        uint8_t ack_seq = incoming_ack->seqNum;
        Frame* next_sendQ_msg = sender->sendQ[(sender->LARs[recv_id] + 1) % SWS].msg;
        
        if(next_sendQ_msg != NULL) {
            uint8_t expected_seq_num = next_sendQ_msg->seqNum;
            //printf("sender->sendQ[%d].msg: %s\n", (sender->LAR + 1),sender->sendQ[(sender->LAR + 1) % SWS].msg->data);
            // printf("[ack_seq, expected_seq_num] = [%d, %d]\n", ack_seq, expected_seq_num);
            if(((ack_seq >= expected_seq_num &&  ack_seq - expected_seq_num <= SWS)||
                    (ack_seq < expected_seq_num && expected_seq_num - ack_seq > SWS))) {

                // uint8_t i;
                // uint8_t num_of_reset = incoming_ack->seqNum - sender->LAR;
                ++sender->LARs[recv_id];
                while(sender->LARs[recv_id] != ack_seq) {
                    sender->is_connected[recv_id] = 1;
                    //printf("#free sendQ[%d].msg: %d\n", (sender->LAR) % SWS, sender->sendQ[(sender->LAR) % SWS].msg->seqNum);
                    free(sender->sendQ[(sender->LARs[recv_id]) % SWS].msg);
                    sender->sendQ[(sender->LARs[recv_id]) % SWS].msg = NULL;
                    // sender->sendQ[(sender->LARs[recv_id]) % SWS].hasResent = 0;
                    ++sender->LARs[recv_id];
                }
                // for(sender->LAR; sender->LAR != ack_seq; ++sender->LAR) {
                //     sender->is_connected[recv_id] = 1;
                //     free(sender->sendQ[(sender->LAR) % SWS].msg);
                //     sender->sendQ[(sender->LAR) % SWS].msg = NULL;
                // }
                // printf("##free sendQ[%d].msg: %d\n", (sender->LARs[recv_id]) % SWS, sender->sendQ[(sender->LARs[recv_id]) % SWS].msg->seqNum);
                free(sender->sendQ[(sender->LARs[recv_id]) % SWS].msg);
                sender->sendQ[(sender->LARs[recv_id]) % SWS].msg = NULL;
                // sender->sendQ[(sender->LARs[recv_id]) % SWS].hasResent = 0;

                // send data
                int msglist_length = ll_get_length(sender->msglist_head);
                uint8_t diff_LFS_LAR = sender->LFSs[recv_id] - sender->LARs[recv_id];
                while(msglist_length > 0 && diff_LFS_LAR < SWS) {
                    // printf("in handle_incoming_acks, msglist_length > 0\n");
                    
                    // no need to check if connection is established
                    LLnode* ll_outframe_node = ll_pop_node(&sender->msglist_head);
                    msglist_length = ll_get_length(sender->msglist_head);
                    char* outgoing_charbuf = (char*) ll_outframe_node->value;
                    Frame* pop_msg_frame = convert_char_to_frame(outgoing_charbuf);
                    ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);
                    sender->LFSs[recv_id] = pop_msg_frame->seqNum;
                    sender->sendQ[pop_msg_frame->seqNum % SWS].msg = pop_msg_frame;
                    // sender->sendQ[pop_msg_frame->seqNum % SWS].hasResent = 0;
                    free(ll_outframe_node);
                    //free(pop_msg_frame);
                    struct timespec ts; // part of #include <“time.h”>
                    ts.tv_sec = 0;
                    ts.tv_nsec = 10000000; // 10 milliseconds
                    nanosleep(&ts, NULL); // part of #include <“time.h”>
                    diff_LFS_LAR = sender->LFSs[recv_id] - sender->LARs[recv_id];
                    gettimeofday(&sender->sendQ[pop_msg_frame->seqNum % SWS].sending_time, NULL);
                }

            }
        }

        int msglist_length = ll_get_length(sender->msglist_head);
        // printf("sender->LAR: %d\n", sender->LARs[recv_id]);
        // printf("incoming_ack->seqNum: %d\n", incoming_ack->seqNum);
        // printf("sender->LFS: %d\n", sender->LFSs[recv_id]);
        // printf("msglist_length: %d\n", msglist_length);
        // printf("equal: %d\n", incoming_ack->seqNum == sender->LFSs[recv_id]);
        if(incoming_ack->seqNum == sender->LFSs[recv_id] && msglist_length == 0) {
            sender->awaiting_msg_ack = 0;
            printf("sender->awaiting_msg_ack: %d\n", sender->awaiting_msg_ack);
            // printf("\n");
            // printf("Finish sending Message!!, sender->LFSs[%d]:%d\n", recv_id, sender->LFSs[recv_id]);
            int i;
            for(i = 0; i < SWS; ++i) {
                if(sender->sendQ[i].msg != NULL) {
                    // printf("###free sender->sendQ[%d].msg: %d\n", i, sender->sendQ[i].msg->seqNum);
                    free(sender->sendQ[i].msg);
                }
                sender->sendQ[i].msg = NULL;
                // sender->sendQ[i].hasResent = 0;
            }
        }   

        free(incoming_ack);
        free(ll_input_frame_node);
    }

}

void handle_input_cmds(Sender* sender, LLnode** outgoing_frames_head_ptr) {
    // TODO: Suggested steps for handling input cmd
    //    1) Dequeue the Cmd from sender->input_cmdlist_head
    //    2) Convert to Frame
    //    3) Set up the frame according to the protocol
    // 
    // sender->awaiting_msg_ack = 1;
    if(sender->awaiting_msg_ack == 1) {
        // printf("sender->awaiting_msg_ack: %d\n", sender->awaiting_msg_ack);
        return;
    }
    int input_cmd_length = ll_get_length(sender->input_cmdlist_head);

    // Recheck the command queue length to see if stdin_thread dumped a command
    // on us
    input_cmd_length = ll_get_length(sender->input_cmdlist_head);

    if(input_cmd_length <= 0) {
        return;
    }
    // printf("Enter handle_input_cmds, cmd_length: %d\n", input_cmd_length);

    //int is_send = 0;


    if (input_cmd_length > 0) {
        // printf("cmd_length: %d\n", input_cmd_length);
        sender->awaiting_msg_ack = 1;
        int msg_offset = 0;
        // Pop a node off and update the input_cmd_length
        LLnode* ll_input_cmd_node = ll_pop_node(&sender->input_cmdlist_head);
        input_cmd_length = ll_get_length(sender->input_cmdlist_head);

        // Cast to Cmd type and free up the memory for the node
        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        int msg_length = strlen(outgoing_cmd->message) + 1;
        uint8_t recv_id = outgoing_cmd->dst_id;
        // printf("\n");
        // printf("Start sending new message!! from %d to %d\n", outgoing_cmd->src_id, recv_id);
        // printf("LAR[%d]: %d, LFS[%d}: %d\n", recv_id, sender->LARs[recv_id], recv_id, sender->LFSs[recv_id]);
        
        // send SYN to establish connection(handshake)
        // printf("sender->is_connected[%d]: %d\n", recv_id, sender->is_connected[recv_id]);
        if(sender->is_connected[recv_id] == 0) {
            const char* syn = "SYN";
            Frame* msg_frame = calloc(1, sizeof(Frame));
            assert(msg_frame);
            memset(msg_frame->data, '\0', sizeof(msg_frame->data));
            strncpy(msg_frame->data, syn, strlen(syn));
            //msg_frame->data[strlen(msg_frame->data)] = '\0';
            msg_frame->remaining_msg_bytes = 0;
            msg_frame->src_id = outgoing_cmd->src_id;
            msg_frame->dst_id = outgoing_cmd->dst_id;  
            msg_frame->crcField = 0;
            msg_frame->seqNum = sender->curr_seq_num[recv_id];
            char* compute_crc8_charbuf = convert_frame_to_char(msg_frame);
            msg_frame->crcField = compute_crc8(compute_crc8_charbuf);
            free(compute_crc8_charbuf);
            // Convert the message to the msglist_charbuf
            char* msglist_charbuf = convert_frame_to_char(msg_frame);
            ll_append_node(&sender->msglist_head,  msglist_charbuf);
            free(msg_frame);
        }

        //uint8_t curr_seq_num = sender->is_connected[recv_id] + 1;
        // handle messages if msg_length > 0
        while (msg_length > 0 && msg_offset < strlen(outgoing_cmd->message)) {
            // printf("msg_length: %d\n", msg_length);
            // printf("msg_offset: %d\n", msg_offset);
            
            Frame* msglist_frame = calloc(1, sizeof(Frame));
            assert(msglist_frame);
            memset(msglist_frame->data, '\0', sizeof(msglist_frame->data));
            strncpy(msglist_frame->data, outgoing_cmd->message + msg_offset, FRAME_PAYLOAD_SIZE - 1);
            //msglist_frame->data[FRAME_PAYLOAD_SIZE - 1] = '\0';
            //printf("data Length: %ld\n", strlen(msglist_frame->data));
            
            msg_length = msg_length - FRAME_PAYLOAD_SIZE + 1;
            msglist_frame->remaining_msg_bytes = msg_length < 0? 0 : msg_length;
            msglist_frame->src_id = outgoing_cmd->src_id;
            msglist_frame->dst_id = outgoing_cmd->dst_id;  
            msglist_frame->crcField = 0;
            msglist_frame->seqNum = ++sender->curr_seq_num[recv_id];

            char* compute_crc8_charbuf = convert_frame_to_char(msglist_frame);
            msglist_frame->crcField = compute_crc8(compute_crc8_charbuf);
            free(compute_crc8_charbuf);
            
            // Convert the message to the msglist_charbuf
            char*  msglist_charbuf = convert_frame_to_char(msglist_frame);
            // ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);
            ll_append_node(&sender->msglist_head,  msglist_charbuf);
            free(msglist_frame);

            msg_offset += (FRAME_PAYLOAD_SIZE - 1);

        } 

        // At this point, we don't need the outgoing_cmd
        free(outgoing_cmd->message);
        free(outgoing_cmd);

        int msglist_length = ll_get_length(sender->msglist_head);
        // printf("msglist_length: %d\n", msglist_length);
        uint8_t diff_LFS_LAR = sender->LFSs[recv_id] - sender->LARs[recv_id];
        while(msglist_length > 0 && diff_LFS_LAR < SWS) {
            // printf("sender cmd msg_length: %d\n", msglist_length);
            LLnode* ll_pop_msg_node = ll_pop_node(&sender->msglist_head);
            msglist_length = ll_get_length(sender->msglist_head);
            char* pop_msg_char_buf = ll_pop_msg_node->value;
            Frame* pop_msg_frame = convert_char_to_frame(pop_msg_char_buf);
            ll_append_node(outgoing_frames_head_ptr, pop_msg_char_buf);
            sender->LFSs[recv_id] = pop_msg_frame->seqNum;
            sender->sendQ[pop_msg_frame->seqNum % SWS].msg = pop_msg_frame;
            // sender->sendQ[pop_msg_frame->seqNum % SWS].hasResent = 0;
            free(ll_pop_msg_node);
            //free(pop_msg_frame);
            struct timespec ts; // part of #include <“time.h”>
            ts.tv_sec = 0;
            ts.tv_nsec = 10000000; // 10 milliseconds
            nanosleep(&ts, NULL); // part of #include <“time.h”>
            diff_LFS_LAR = sender->LFSs[recv_id] - sender->LARs[recv_id];
            gettimeofday(&sender->sendQ[pop_msg_frame->seqNum % SWS].sending_time, NULL);
            //printf("cmd send[%d][%d]: %s\n", pop_msg_frame->seqNum, diff_LFS_LAR,sender->sendQ[pop_msg_frame->seqNum % SWS].msg->data);
            // printf("cmd send[%d][%d]: %s\n", pop_msg_frame->seqNum, diff_LFS_LAR,sender->sendQ[pop_msg_frame->seqNum % SWS].msg->data);
        }

    }

    // if(sender->is_connected[recv_id] == 0) {
    //     return;
    // }

}

void handle_timedout_frames(Sender* sender, LLnode** outgoing_frames_head_ptr) {
    // TODO: Handle frames that have timed out
    struct timeval curr_timeval;
    struct timeval expiring_timeval;
    long diff_timeout_usec;
    int i;
    //uint8_t start = sender->LARs[recv_id] + 1;
    for(i = 0; i < SWS; ++i) {
        //uint8_t idx = (start + i) % SWS;
        uint8_t idx = i;
        if(sender->sendQ[idx].msg == NULL) { continue; }
        // printf("handle timeout[%d]: %d\n", idx, sender->sendQ[idx].msg->seqNum);
        expiring_timeval = sender->sendQ[idx].sending_time;
        // printf("\n");
        // printf("expiring_timeval.tv_sec[%d] %lds\n", idx, expiring_timeval.tv_sec);
        // printf("expiring_timeval.tv_usec[%d] %ldus\n", idx, expiring_timeval.tv_usec);
        expiring_timeval.tv_usec += 90000;
        
        // printf("expiring_timeval.tv_sec[%d] %lds\n", idx, expiring_timeval.tv_sec);
        // printf("expiring_timeval.tv_usec[%d] %ldus\n", idx, expiring_timeval.tv_usec);
        //if(expiring_timeval == NULL || sender->curr_sending_charbuf == NULL) { return; }
        // Get the current time
        gettimeofday(&curr_timeval, NULL);
        // printf("curr_timeval.tv_sec[%d] %lds\n", idx, curr_timeval.tv_sec);
        // printf("curr_timeval.tv_usec[%d] %ldus\n", idx, curr_timeval.tv_usec);
        diff_timeout_usec = timeval_usecdiff(&curr_timeval, &expiring_timeval);
        // printf("diff_timeout_usec: %ld\n", diff_timeout_usec);
        // printf("\n");
        // int ll_outgoing_frame_length = ll_get_length(*outgoing_frames_head_ptr);
        // printf("len = %d\n", ll_outgoing_frame_length);
        if(diff_timeout_usec < 0) {
            // printf("it's < 0, Enter handle_timedout, diff_timeout_usec: %ld\n", diff_timeout_usec);
            // sender->sendQ[i].msg is NOT NULL
            // Convert the message to the msglist_charbuf
            // printf("resend from timeout[%d]: %d\n", idx, sender->sendQ[idx].msg->seqNum);
            char*  msg_charbuf = convert_frame_to_char(sender->sendQ[idx].msg);
            ll_append_node(outgoing_frames_head_ptr, msg_charbuf);
            gettimeofday(&sender->sendQ[idx].sending_time, NULL);
            // sender->sendQ[idx].hasResent = 1;
        }
    }
    
}

void* run_sender(void* input_sender) {
    struct timespec time_spec;    
    struct timeval curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Sender* sender = (Sender*) input_sender;
    LLnode* outgoing_frames_head;
    struct timeval* expiring_timeval;
    long sleep_usec_time, sleep_sec_time;

    // This incomplete sender thread, at a high level, loops as follows:
    // 1. Determine the next time the thread should wake up
    // 2. Grab the mutex protecting the input_cmd/inframe queues
    // 3. Dequeues commands and frames from the input_cmdlist and input_framelist 
    //    respectively. Adds frames to outgoing_frames list as needed.
    // 4. Releases the lock
    // 5. Sends out the frames

    while (1) {
        outgoing_frames_head = NULL;

        // Get the current time
        gettimeofday(&curr_timeval, NULL);

        // time_spec is a data structure used to specify when the thread should wake up.
        time_spec.tv_sec = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;

        // Check for the next event we should handle
        expiring_timeval = sender_get_next_expiring_timeval(sender);

        if (expiring_timeval == NULL) {
            //printf("in run_sender, expiring_timeval is NULL\n");
            time_spec.tv_sec += WAIT_SEC_TIME;
            time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        } else {
            // Take the difference between the next event and the current time
            sleep_usec_time = timeval_usecdiff(&curr_timeval, expiring_timeval);
            //printf("in run_sender, expiring_timeval is NOT NULL, sleep_usec_time: %ld\n", sleep_usec_time);
            // Sleep if the difference is positive
            if (sleep_usec_time > 0) {
                sleep_sec_time = sleep_usec_time / 1000000;
                sleep_usec_time = sleep_usec_time % 1000000;
                time_spec.tv_sec += sleep_sec_time;
                time_spec.tv_nsec += sleep_usec_time * 1000;
            }
        }

        // Check to make sure we didn't "overflow" the nanosecond field
        if (time_spec.tv_nsec >= 1000000000) {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        //*****************************************************************************************
        // NOTE: Anything that involves dequeing from the input_framelist or input_cmdlist 
        // should go between the mutex lock and unlock, because other threads
        //      CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&sender->buffer_mutex);

        // Check whether anything has arrived
        int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(sender->input_framelist_head);

        // Nothing (cmd nor incoming frame) has arrived, so do a timed wait on
        // the sender's condition variable (releases lock) A signal on the
        // condition variable will wakeup the thread and reaquire the lock
        if (input_cmd_length == 0 && inframe_queue_length == 0) {
            pthread_cond_timedwait(&sender->buffer_cv, &sender->buffer_mutex,
                                   &time_spec);
        }
        // Implement this
        // printf("handle_incoming_acks\n");
        handle_incoming_acks(sender, &outgoing_frames_head);

        // Implement this
        // printf("handle_input_cmds\n");
        handle_input_cmds(sender, &outgoing_frames_head);
        sender->active = (ll_get_length(outgoing_frames_head) > 0 || sender->awaiting_msg_ack) ? 1:0;

        // Implement this
        // printf("handle_timedout_frames\n");
        handle_timedout_frames(sender, &outgoing_frames_head);

        pthread_mutex_unlock(&sender->buffer_mutex);

        // DO NOT CHANGE BELOW CODE
        // Send out all the frames
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);

        while (ll_outgoing_frame_length > 0) {
            LLnode* ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char* char_buf = (char*) ll_outframe_node->value;

            // The following function will free the memory for the char_buf object.
            // The function will convert char_buf to frame and deliver it to 
            // the receiver having receiver->recv_id = frame->dst_id.
            send_msg_to_receiver(char_buf);
            // Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);
    return 0;
}