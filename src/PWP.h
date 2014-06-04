#ifndef __PWP_H__
#define __PWP_H_


#define PEER_PORT 6666

#include "list.h"


extern ListHead P2PCB_head;
 
typedef struct p2p_ctrl_block{
    int connfd;
    //`int state;//0:before handshake; 1:after handshake bebore bitfield; 3 after bitfield
    int am_choking;
    int am_interested;
    int peer_choking;
    int peer_interested;
    char oppsite_peer_id[20];
    char *oppsite_piece_info;
    ListHead list;
}P2PCB;

typedef struct handshake_msg{
    char pstrlen;
    char pstr[19];
    char reserved[8];
    char info_hash[20];
    char peer_id[20];
}handshake_msg;

typedef struct torrent_info{
    char info_hash[20];
    char peer_id[20];
    int piece_num;
    char *piece_info;
}torrent_info;
extern torrent_info currTorrent;

typedef struct p2p_thread_param{
    int connfd;
    int is_connecter;//1:this peer connect to another; 0:oppsite
}p2p_thread_param;


//functions
void* process_p2p_conn(void *);
int generate_listenfd();











#endif
