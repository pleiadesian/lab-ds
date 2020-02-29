/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 * NOTE: In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-1 bit->|<-    7 bit   ->|<-  4 byte  ->|<-             the rest            ->|
 *       |<- EOM ->|<-payload size->|<-  seq no. ->|<-             payload             ->|
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdt_struct.h"
#include "rdt_sender.h"

#define WINDOW_SIZE     10
#define MSG_BUFFER_SIZE 15000
#define INFO_OFFSET     0
#define INFO_SIZE       1
#define SEQNUM_OFFSET   INFO_OFFSET + INFO_SIZE
#define SEQNUM_SIZE     sizeof(int)
#define PAYLOAD_OFFSET  SEQNUM_OFFSET + SEQNUM_SIZE
#define PAYLOAD_SIZE    RDT_PKTSIZE - SEQNUM_SIZE - INFO_SIZE
#define TIMEOUT         0.3
#define CHECK_INTERVAL  0.1

#define END_OF_MESSAGE  0x80

struct window {
    double send_time;
    struct packet pkt;
};

static message *msg_buffer;             // buffers for the waiting message
static window *buffer;                  // buffers for the outbound stream
static int ack_expected = 0;            // next ack expected inbound
static int next_packet_to_send = 0;     // next packet going out
static int seq_no = 0;                  // next sequence number to use
static int nbuffer = 0;                 // number of output buffers currently in use
static int next_message_to_send = 0;    // next message going out
static int message_cursor = 0;          // next packet in the message going out
static int msg_no = 0;                  // next message number to use
static int nmsg = 0;                    // number of message buffers currently in use
static bool sending = false;            // if sending has started


/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
    msg_buffer = (struct message *)malloc(MSG_BUFFER_SIZE * sizeof(struct message));
    buffer = (struct window *)malloc(WINDOW_SIZE * sizeof(struct window));
    ASSERT(msg_buffer != NULL);
    ASSERT(buffer != NULL);
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some 
   memory you allocated in Sender_init(). */
void Sender_Final()
{
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
    Sender_StopTimer();
    for (int i = 0; i < MSG_BUFFER_SIZE; i++) {
        if (msg_buffer[i].size > 0) 
            free(msg_buffer[i].data);
    }
    free(msg_buffer);
    free(buffer);
}

/* fragmentation and move packets into window */
void Sender_ExpandWindow()
{
    ASSERT(next_message_to_send < msg_no);
    struct message *msg = &msg_buffer[next_message_to_send % MSG_BUFFER_SIZE];
    int packet_num = msg->size / PAYLOAD_SIZE;
    int last_packet_size = msg->size % PAYLOAD_SIZE;
    for (int i = message_cursor; i < packet_num; i++) {
        packet *p = &buffer[seq_no % WINDOW_SIZE].pkt;
        char header = ~END_OF_MESSAGE & PAYLOAD_SIZE;
        memcpy(p->data + INFO_OFFSET, &header, INFO_SIZE);
        memcpy(p->data + SEQNUM_OFFSET, &seq_no, SEQNUM_SIZE);
        memcpy(p->data + PAYLOAD_OFFSET, msg->data + PAYLOAD_SIZE * i, PAYLOAD_SIZE);
        seq_no++;
        nbuffer++;
        message_cursor++;
        /* stop moving packets into window when window is full*/
        if (nbuffer >= WINDOW_SIZE) 
            return;
    }

    /* fill last packet */
    if (last_packet_size > 0) {
        ASSERT(last_packet_size < 128);
        packet *p = &buffer[seq_no % WINDOW_SIZE].pkt;
        char header = END_OF_MESSAGE | last_packet_size;
        memcpy(p->data + INFO_OFFSET, &header, INFO_SIZE);
        memcpy(p->data + SEQNUM_OFFSET, &seq_no, SEQNUM_SIZE);
        memcpy(p->data + PAYLOAD_OFFSET, msg->data + PAYLOAD_SIZE * packet_num, last_packet_size);
        seq_no++;
        nbuffer++;
    } else {
        /* mark end of message on last packet */
        int last_seq_no = seq_no - 1;
        packet *p = &buffer[last_seq_no % WINDOW_SIZE].pkt;
        char header = END_OF_MESSAGE | PAYLOAD_SIZE;
        memcpy(p->data + INFO_OFFSET, &header, INFO_SIZE);
    }
    nmsg--;
    next_message_to_send++;
}

/* send all packets */
void Sender_SendPacket()
{
    while (next_packet_to_send < seq_no) {
        buffer[next_packet_to_send % WINDOW_SIZE].send_time = GetSimulationTime();
        Sender_ToLowerLayer(&buffer[next_packet_to_send % WINDOW_SIZE].pkt);
        next_packet_to_send++;
    }
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer(struct message *msg)
{
    ASSERT(msg->size > 0);
    ASSERT(nmsg < MSG_BUFFER_SIZE);
    msg_buffer[msg_no % MSG_BUFFER_SIZE].size = msg->size;
    msg_buffer[msg_no % MSG_BUFFER_SIZE].data = (char *)malloc(msg->size);
    ASSERT(msg_buffer[msg_no % MSG_BUFFER_SIZE].data != NULL);
    memcpy(&msg_buffer[msg_no].data, msg->data, msg->size);
    msg_no++;
    nmsg++;

    Sender_ExpandWindow();
    Sender_SendPacket();

    if (!sending) 
        Sender_StartTimer(CHECK_INTERVAL);
}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
    int ack_no;
    memcpy(&ack_no, pkt->data + SEQNUM_OFFSET, SEQNUM_SIZE);
    ASSERT(ack_no < seq_no);
    /* ignore old acknowledge number and increase expected acknowledge number */
    if (ack_no >= ack_expected) {
        nbuffer -= ack_no - ack_expected + 1;
        ack_expected = ack_no + 1;
        /* message buffer should be drained when slots in the send window become available */
        Sender_ExpandWindow();
        Sender_SendPacket();
    }
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
    /* check if the first packet waiting for ack is timout */
    if (GetSimulationTime() - buffer[ack_expected % WINDOW_SIZE].send_time >= TIMEOUT) {
        next_packet_to_send = ack_expected;
        Sender_ExpandWindow();
        Sender_SendPacket();
    }
    Sender_StartTimer(CHECK_INTERVAL);
}
