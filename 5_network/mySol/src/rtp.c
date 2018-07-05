#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "queue.h"
#include "network.h"
#include "rtp.h"

typedef struct message {
    char *buffer;
    int length;
} message_t;

static int checksum(char *buffer, int length) {

    int i;
    int sum = 0;
    for (i = 0; i < length; i++) sum += *(buffer+i);
    return sum;
}

static packet_t *packetize(char *buffer, int length, int *num) {
    if (length <= 0)
        return NULL;
    int i;
    int j;
    *num = length / MAX_PAYLOAD_LENGTH;
    if (length%MAX_PAYLOAD_LENGTH != 0)
        *num += 1;
    packet_t *pkt = (packet_t *)malloc((size_t)((*num) * (int)sizeof(packet_t)));
    for (i = 0; i < *num; i++){
        int len = 0;
        for(j = 0; j < MAX_PAYLOAD_LENGTH; len++,j++)
            if(i * MAX_PAYLOAD_LENGTH + j < length)
                (pkt+i)->payload[j] = *(buffer + i*MAX_PAYLOAD_LENGTH + j);
        (pkt+i)->payload_length = len;
        (pkt+i)->type = DATA;
    }
    (pkt - 1 + (*num))->type = LAST_DATA;
    return pkt;
}

/* ================================================================ */
/*                      R T P       T H R E A D S                   */
/* ================================================================ */

static void *rtp_recv_thread(void *void_ptr) {
    
    rtp_connection_t *connection = (rtp_connection_t *) void_ptr;
    
    do {
        message_t *message;
        int buffer_length = 0;
        char *buffer = NULL;
        packet_t packet;

        /* Put messages in buffer until the last packet is received  */
        do {

            if (net_recv_packet(connection->net_connection_handle, &packet) <= 0 || packet.type == TERM) {
                /* remote side has disconnected */
                connection->alive = 0;
                pthread_cond_signal(&connection->recv_cond);
                pthread_cond_signal(&connection->send_cond);
                break;
            }


            if (packet.type == DATA || packet.type == LAST_DATA){
                packet_t pkt;
                if (packet.checksum != checksum((char*)packet.payload, packet.payload_length)){
                    pkt.type = NACK;
                    if (packet.type == LAST_DATA) packet.type = DATA;

                }else{
                    int tmp = buffer_length;
                    buffer_length += packet.payload_length;
                    buffer = (char *) realloc(buffer, (size_t)buffer_length);
                    memcpy(tmp + buffer, packet.payload, (size_t)packet.payload_length);
                    pkt.type = ACK;
                }
                net_send_packet(connection->net_connection_handle, &pkt);
            }else{

                pthread_mutex_lock(&connection -> ack_mutex);
                if(packet.type == ACK)
                    connection->ack=False;
                else
                    connection->ack=True;

                pthread_cond_signal(&connection->ack_cond);
                pthread_mutex_unlock(&connection -> ack_mutex);

            }
        } while (packet.type != LAST_DATA);
        
        if (connection->alive == 1) {
            message = (message_t*) malloc(sizeof(message_t));
            message -> length = buffer_length;
            message -> buffer = buffer;

            pthread_mutex_lock(&connection->recv_mutex);
            queue_add(&(connection->recv_queue), message);
            pthread_mutex_unlock(&connection->recv_mutex);
            
            pthread_cond_signal(&connection->recv_cond);
            
        } else free(buffer);
        
    } while (connection->alive == 1);
    
    return NULL;
    
}

static void *rtp_send_thread(void *void_ptr) {
    
    rtp_connection_t *connection = (rtp_connection_t *) void_ptr;
    message_t *message;
    int array_length = 0;
    int i;
    packet_t *packet_array;
    
    do {
        /* Extract the next message from the send queue */
        pthread_mutex_lock(&connection->send_mutex);
        while (queue_size(&connection->send_queue) == 0 && connection->alive == 1) {
            pthread_cond_wait(&connection->send_cond, &connection->send_mutex);
        }
        
        if (connection->alive == 0) break;
        
        message = queue_extract(&connection->send_queue);
        
        pthread_mutex_unlock(&connection->send_mutex);
        
        /* Packetize the message and send it */
        packet_array = packetize(message->buffer, message->length, &array_length);
        
        for (i = 0; i < array_length; i++) {
            
            /* Start sending the packetized messages */        
            packet_array[i].checksum = checksum(packet_array[i].payload, packet_array[i].payload_length);

            if (net_send_packet(connection->net_connection_handle, &packet_array[i]) <= 0) {
                /* remote side has disconnected */
                connection->alive = 0;
                break;
            }

            pthread_mutex_lock(&connection->ack_mutex);
            while(connection->ack==Null)
                pthread_cond_wait(&connection->ack_cond, &connection->ack_mutex);
            if(connection->ack==True) i-=1;
            connection->ack=Null;
            pthread_mutex_unlock(&connection->ack_mutex);
        }
        
        free(packet_array);
        free(message->buffer);
        free(message);
    } while (connection->alive == 1);
    return NULL;
    
    
}

static rtp_connection_t *rtp_init_connection(int net_connection_handle) {
    rtp_connection_t *rtp_connection = malloc(sizeof(rtp_connection_t));
    
    if (rtp_connection == NULL) {
        fprintf(stderr, "Out of memory!\n");
        exit(EXIT_FAILURE);
    }
    
    rtp_connection->net_connection_handle = net_connection_handle;
    
    queue_init(&rtp_connection->recv_queue);
    queue_init(&rtp_connection->send_queue);
    
    pthread_mutex_init(&rtp_connection->ack_mutex, NULL);
    pthread_mutex_init(&rtp_connection->recv_mutex, NULL);
    pthread_mutex_init(&rtp_connection->send_mutex, NULL);
    pthread_cond_init(&rtp_connection->ack_cond, NULL);
    pthread_cond_init(&rtp_connection->recv_cond, NULL);
    pthread_cond_init(&rtp_connection->send_cond, NULL);
    
    rtp_connection->alive = 1;
    rtp_connection->ack=Null;
    pthread_create(&rtp_connection->recv_thread, NULL, rtp_recv_thread,
                   (void *) rtp_connection);
    pthread_create(&rtp_connection->send_thread, NULL, rtp_send_thread,
                   (void *) rtp_connection);
    
    return rtp_connection;
}

/* ================================================================ */
/*                           R T P    A P I                         */
/* ================================================================ */

rtp_connection_t *rtp_connect(char *host, int port) {
    
    int net_connection_handle;
    
    if ((net_connection_handle = net_connect(host, port)) < 1)
        return NULL;
    
    return (rtp_init_connection(net_connection_handle));
}

int rtp_disconnect(rtp_connection_t *connection) {
    
    message_t *message;
    packet_t term;
    
    term.type = TERM;
    term.payload_length = term.checksum = 0;
    net_send_packet(connection->net_connection_handle, &term);
    connection->alive = 0;
    
    net_disconnect(connection->net_connection_handle);
    pthread_cond_signal(&connection->send_cond);
    pthread_cond_signal(&connection->recv_cond);
    pthread_join(connection->send_thread, NULL);
    pthread_join(connection->recv_thread, NULL);
    net_release(connection->net_connection_handle);
    
    /* emtpy recv queue and free allocated memory */
    while ((message = queue_extract(&connection->recv_queue)) != NULL) {
        free(message->buffer);
        free(message);
    }
    queue_release(&connection->recv_queue);
    
    /* emtpy send queue and free allocated memory */
    while ((message = queue_extract(&connection->send_queue)) != NULL) {
        free(message);
    }
    queue_release(&connection->send_queue);
    
    free(connection);
    
    return 1;
    
}

int rtp_recv_message(rtp_connection_t *connection, char **buffer, int *length) {
    
    message_t *message;
    
    if (connection->alive == 0)
        return -1;
    /* lock */
    pthread_mutex_lock(&connection->recv_mutex);
    while (queue_size(&connection->recv_queue) == 0 && connection->alive == 1) {
        pthread_cond_wait(&connection->recv_cond, &connection->recv_mutex);
    }
    
    if (connection->alive == 0) {
        pthread_mutex_unlock(&connection->recv_mutex);
        return -1;
    }
    
    /* extract */
    message = queue_extract(&connection->recv_queue);
    *buffer = message->buffer;
    *length = message->length;
    free(message);
    
    /* unlock */
    pthread_mutex_unlock(&connection->recv_mutex);
    
    return *length;
}

int rtp_send_message(rtp_connection_t *connection, char *buffer, int length) {
    
    message_t *message;
    
    if (connection->alive == 0)
        return -1;
    
    message = malloc(sizeof(message_t));
    if (message == NULL) {
        return -1;
    }
    message->buffer = malloc((size_t) length);
    message->length = length;
    
    if (message->buffer == NULL) {
        free(message);
        return -1;
    }
    
    memcpy(message->buffer, buffer, (size_t) length);
    
    /* lock */
    pthread_mutex_lock(&connection->send_mutex);
    
    /* add */
    queue_add(&(connection->send_queue), message);
    
    /* unlock */
    pthread_mutex_unlock(&connection->send_mutex);
    pthread_cond_signal(&connection->send_cond);
    return 1;
    
}
