/*
 * this file includes the implements of PWP
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "PWP.h"
#include "list.h"
#include "global.h"

static char set_bit[8] = {1,2,4,8,16,32,64,128};


static int readn( int fd, void *bp, size_t len)
{
    int cnt;
    int rc;

    cnt = len;
    while ( cnt > 0 )
    {
        rc = recv( fd, bp, cnt, 0 );
        if ( rc < 0 )               /* read error? */
        {
            if ( errno == EINTR )   /* interrupted? */
                continue;           /* restart the read */
            return -1;              /* return error */
        }
        if ( rc == 0 )              /* EOF? */
            return len - cnt;       /* return short count */
        bp += rc;
        cnt -= rc;
    }
    return len;
}

// get/set the bit value at the index
static inline int get_bit_at_index(char *info, int index){
    unsigned char ch = info[index/8];
    int offset = 7 - index%8;
    return (ch >> offset) & 1;
}
static inline void set_bit_at_index(char *info, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        info[index/8] = info[index/8] | set_bit[offset];
    else
        info[index/8] = info[index/8] & (~set_bit[offset]);
}

//when error ,drop connection
static inline void drop_conn(P2PCB *currP2P){
    list_del(&currP2P->list);
    free(currP2P->oppsite_piece_info);
    close(currP2P->connfd);
    free(currP2P);
}

// init p2p control block
void init_p2p_block(P2PCB *node){
    node->am_choking = 1;
    node->am_interested = 0;
    node->peer_choking = 1;
    node->peer_interested = 0;
    list_init(&node->list);
}

//listening PEER_PORT for handshake,this function returns the listening sockfd 
int generate_listenfd(){ 
	int listenfd,connfd;
	struct sockaddr_in servaddr;

	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
		perror("Problem in creating the listening socket");
		exit(1);		
	}
    
    memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PEER_PORT);

	bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
	listen(listenfd,8);

	return listenfd;
}

void* process_p2p_conn(void *fd){
    int connfd = (int)fd;
    P2PCB *currP2P = (P2PCB *)malloc(sizeof(P2PCB));
    memset(currP2P,0,sizeof(P2PCB));
    currP2P->connfd = connfd;
    init_p2p_block(currP2P);
    int piece_info_len = globalInfo.g_torrentmeta->num_pieces/8 + (globalInfo.g_torrentmeta->num_pieces%8 != 0);
    currP2P->oppsite_piece_info = (char *)malloc(piece_info_len);
    memset(currP2P->oppsite_piece_info,0,piece_info_len);
    list_add_before(&P2PCB_head,&currP2P->list);

    char prefix[5];
    //char *payload;
    handshake_msg msg;

    //readn handshake msg
    if(readn(connfd,&msg,sizeof(handshake_msg)) > 0){
        printf("handshake recieved\n");
        if(strncmp(msg.info_hash, (char *)globalInfo.g_torrentmeta->info_hash,20) != 0){//wrong hash_info 
            printf("wrong handshake msg\n");
            drop_conn(currP2P);
            return NULL;
        }
        else{
            memcpy(currP2P->oppsite_peer_id,msg.peer_id,20);
            send(connfd,&msg,sizeof(handshake_msg),0);//send back with local peer_id
        }
    }

    //send bitfield msg
    if(strcmp(currTorrent.piece_info,"") != 0){
        char bitfield_msg[5+piece_info_len];
        *(int*)bitfield_msg = htonl(1+piece_info_len);
        bitfield_msg[4] = 5;
        strncpy(bitfield_msg+5,currTorrent.piece_info,piece_info_len);
        send(connfd,bitfield_msg,5+piece_info_len,0);
    }

    //process msgs
    while(readn(connfd, prefix, 5) > 0){
        int len = ntohl(*(int *)prefix);
        char type = prefix[4];
        if(len == 0)//keep-alive
            continue;
        switch(type){
            case 0 : currP2P->peer_choking = 1;break;//choke
            case 1 : currP2P->peer_choking = 0;break;//unchoke
            case 2 : currP2P->am_interested = 1;break;//interested
            case 3 : currP2P->am_interested = 0;break;//not interested
            case 4 : {//have  
                         int index;
                         readn(connfd,&index,4);
                         index = ntohl(index);
                         set_bit_at_index(currP2P->oppsite_piece_info,index,1);
                         if(get_bit_at_index(currTorrent.piece_info,index) == 0){//send interest
                            *(int *)prefix = htonl(1);
                            prefix[4] = 2;
                            send(connfd, prefix, 5, 0);
                         }
                         break;
                     }
            case 5 : {//bitfield
                         char bitfield[len-1];
                         readn(connfd,bitfield,len-1);
                         if(len - 1 != piece_info_len){
                            //wrong length of bitfield
                            printf("wrong length of bitfield\n");
                            drop_conn(currP2P);
                            return NULL;
                         }
                         //check idle bits
                         unsigned char ch = bitfield[len-2];
                         int offset = 8 - globalInfo.g_torrentmeta->num_pieces%8;
                         while(offset >= 1){
                             if(((ch >> (offset-1)) & 1) == 1){//wrong idle bits 
                                 printf("wrong idle bits\n");
                                 drop_conn(currP2P);
                                 return NULL;
                             }
                             offset--;
                         }
                         //set oppsite piece info
                         memcpy(currP2P->oppsite_piece_info,bitfield,len-1);
                         break;
                     }
            case 6 : {//request
                        int payload[3];//<index><begin><length>
                        readn(connfd,payload,12);
                        int index = ntohl(payload[0]);
                        int begin = ntohl(payload[1]);
                        int length = ntohl(payload[2]);
                        if(length > 131072){
                            printf("the length in request is larger than 2^17\n");
                            drop_conn(currP2P);
                            return NULL;
                        }

                        //send the block
                        /*char *block = get_block(index,begin,length);
                        char piece_msg[13 + length];
                        *(int *)piece_msg = 9 + length;
                        piece_msg[4] = 7;
                        *(int *)(piece_msg+5) = index;
                        *(int *)(piece_msg+9) = begin;
                        memcpy(piece_msg+13,block,length);
                        send(connfd,piece_msg,13+length,0);
                        free(block);*/
                        break;
                     }
            case 7 : {//piece
                        char payload[len-1];
                        readn(connfd,payload,len-1);
                        int index = ntohl(*(int*)payload);
                        int begin = ntohl(*(int*)(payload+4));
                        int length = len - 9;
                        //set_block(index,begin,length,payload+8);
                        break;
                     }
            case 8 : {//cancel
                        break;
                     }
        }
    }

    printf("exit the p2p msg process\n");
    return NULL;
}



