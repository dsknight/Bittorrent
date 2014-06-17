#ifndef __PWP_H__
#define __PWP_H_


#define PEER_PORT 6666
#define SUB_PIECE_SIZE 16384
#define MAX_REQUEST_NUM 5
#define MAX_PEERS 1000000

#include <stdbool.h>
#include <pthread.h>
#include "list.h"


extern ListHead P2PCB_head;
extern ListHead downloading_piece_head;
extern int first_request;

extern pthread_mutex_t P2P_mutex;
extern pthread_mutex_t download_mutex;
extern pthread_mutex_t firstReq_mutex;

typedef struct p2p_ctrl_block{
    ListHead list;
    int connfd;
    int am_choking;
    int am_interested;
    int peer_choking;
    int peer_interested;
    char oppsite_peer_id[20];
    char oppsite_peer_ip[20];
    char *oppsite_piece_info;
}P2PCB;


typedef struct torrent_info{
    char info_hash[20];
    char peer_id[20];
    int piece_num;
    char *piece_info;
}torrent_info;

typedef struct p2p_thread_param{
    int connfd;
    int is_connecter;//1:this peer connect to another; 0:oppsite
    char ip[20];
}p2p_thread_param;

typedef struct downloading_piece{
    int index;
    int sub_piece_size;
    int sub_piece_num;
    int *sub_piece_state;//sub_piece_state[i] == 0/1/2  : NO.i subpiece not-download/downloading/finish
    int downloading_num;//current downloading request number
    ListHead list;
}downloading_piece;


//functions
void* process_p2p_conn(void *);
int generate_listenfd();
void init_p2p_block(P2PCB *node);
//send msgs funcs
void send_keep_alive_msg(int);
void send_choke_msg(int);
void send_unchoke_msg(int);
void send_interest_msg(int);
void send_not_interest_msg(int);
void send_have_msg(int,int);
void send_bitfield_msg(int);
void send_request_msg(int,int,int,int);
void send_piece_msg(int,int,int,int);
void send_cancel_msg(int,int,int,int);

// check whether exist a thread for an ip
bool exist_ip(char *ip);
void send_handshake_msg(int);

#endif
