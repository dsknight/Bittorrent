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
#include <stdbool.h>
#include <pthread.h>
#include "PWP.h"
#include "list.h"
#include "global.h"
#include "fileop.h"

#define my_free(ptr) if(NULL != (ptr))\
                        free(ptr);\
                        (ptr) = NULL

//define global varibles and mutex
ListHead P2PCB_head;
ListHead downloading_piece_head;
int first_request = 1;
int *piece_counter;

pthread_mutex_t P2P_mutex;
pthread_mutex_t download_mutex;
pthread_mutex_t firstReq_mutex;
pthread_mutex_t pieceCounter_mutex;

static char set_bit[8] = {1,2,4,8,16,32,64,128};

static int readn( int fd, void *bp, size_t len)
{
    printf("in readn: read %d char\n", len);
    int cnt;
    int rc;
    cnt = len;
    while ( cnt > 0 ) {
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
static inline int get_bit_at_index(char *bitfield, int index){
    unsigned char ch = bitfield[index/8];
    int offset = 7 - index%8;
    return (ch >> offset) & 1;
}

static inline void set_bit_at_index(char *bitfield, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        bitfield[index/8] = bitfield[index/8] | set_bit[offset];
    else
        bitfield[index/8] = bitfield[index/9] & (~set_bit[offset]);
}

//when error ,drop connection
static inline void drop_conn(P2PCB *currP2P){
    pthread_mutex_lock(&P2P_mutex);
    list_del(&currP2P->list);
    pthread_mutex_unlock(&P2P_mutex);
    my_free(currP2P->oppsite_bitfield);
    close(currP2P->connfd);
    my_free(currP2P);
}


//whether a bitfield is empty or complete
static inline int is_bitfield_empty(char *bitfield,int len){
    int flag = 1;
    for(int i = 0; i < len; i++){
        if(bitfield[i] != 0){
            flag = 0;
            break;
        }
    }
    return flag;
}

static inline int is_bitfield_complete(char *bitfield){
    for(int i = 0; i < globalInfo.g_torrentmeta->num_pieces; i++){
        if(get_bit_at_index(bitfield,i) != 1)
            return 0;
    }
    return 1;
}

bool exist_ip(char *ip){
    ListHead *ptr;
    pthread_mutex_lock(&P2P_mutex);
    list_foreach(ptr, &P2PCB_head){
        P2PCB *tmpP2P = list_entry(ptr,P2PCB,list);
        if (strcmp(tmpP2P->oppsite_peer_ip, ip) == 0){
            pthread_mutex_unlock(&P2P_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&P2P_mutex);
    return false;
}

//retuen the real len of piece of NO index
static inline int real_piece_len(int index){
    int piece_num = globalInfo.g_torrentmeta->num_pieces;
    int piece_len = globalInfo.g_torrentmeta->piece_len;
    int total_len = globalInfo.g_torrentmeta->length;
    int real_piece_len = (index == piece_num-1)?(total_len - index * piece_len):(piece_len);
    return real_piece_len;
}

//select next piece/sub-piece
int select_next_piece_(){//by order
    int piece_num = globalInfo.g_torrentmeta->num_pieces;
    ListHead *ptr;
    pthread_mutex_lock(&P2P_mutex);
    list_foreach(ptr,&P2PCB_head){
        P2PCB *tmpP2P = list_entry(ptr,P2PCB,list);
        printf("*** %d ***\n",(int)&tmpP2P->list);
        if(tmpP2P->peer_interested == 1 && tmpP2P->am_choking == 0){
            char *bitfield1 = globalInfo.bitfield;
            char *bitfield2 = tmpP2P->oppsite_bitfield;
            for(int i = 0; i < piece_num; i++){
                if(get_bit_at_index(bitfield1,i) == 0 && get_bit_at_index(bitfield2,i) == 1){
                    printf("next piece is: %d \n",i);
                    pthread_mutex_unlock(&P2P_mutex);
                    return i;
                }
            }
        }
    }
    pthread_mutex_unlock(&P2P_mutex);
    printf("Error: can not find next piece\n");
    return -1;
}

int select_next_piece(){//least first
    int min_index = -1, min_val = 100000;
    for(int i = 0; i < globalInfo.g_torrentmeta->num_pieces; i++){
        printf("*%d:%d* ",i,piece_counter[i]);
        if(get_bit_at_index(globalInfo.bitfield,i) == 0 
            && piece_counter[i] != 0 && piece_counter[i] < min_val){
            min_val = piece_counter[i];
            min_index = i;
        }
    }
    printf("\nnext piece is %d (least first)\n",min_index);
    return min_index;
}
    
int select_next_subpiece(int index,int *begin,int *length){
    ListHead *ptr;
    pthread_mutex_lock(&download_mutex);
    list_foreach(ptr,&downloading_piece_head){
        downloading_piece *d_piece = list_entry(ptr,downloading_piece,list);
        int rest = real_piece_len(index) % d_piece->sub_piece_size;
        if(d_piece->index == index){
            for(int i = 0; i < d_piece->sub_piece_num; i++){
                if(d_piece->sub_piece_state[i] == 0){
                    *begin = i * d_piece->sub_piece_size;
                    if(i == d_piece->sub_piece_num -1 && rest != 0){
                        *length = rest;
                        printf("next subpiece is index:%d begin:%d length:%d\n",index,*begin,*length);
                        pthread_mutex_unlock(&download_mutex);
                        return 1;
                    }
                    else{
                        *length = d_piece->sub_piece_size;
                        printf("next subpiece is index:%d begin:%d length:%d\n",index,*begin,*length);
                        pthread_mutex_unlock(&download_mutex);
                        return 1;
                    }
                }
            }
            for(int i = 0; i < d_piece->sub_piece_num; i++){
                if(d_piece->sub_piece_state[i] == 1){
                    *begin = i * d_piece->sub_piece_size;
                    int rest = globalInfo.g_torrentmeta->piece_len % d_piece->sub_piece_size;
                    if(i == d_piece->sub_piece_num -1 && rest != 0){
                        *length = rest;
                        printf("next subpiece is index:%d begin:%d length:%d\n",index,*begin,*length);
                        pthread_mutex_unlock(&download_mutex);
                        return 1;
                    }
                    else{
                        *length = d_piece->sub_piece_size;
                        printf("next subpiece is index:%d begin:%d length:%d\n",index,*begin,*length);
                        pthread_mutex_unlock(&download_mutex);
                        return 1;
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&download_mutex);
    return 0;
}

//find the downloading piece corresponding to index
downloading_piece *find_downloading_piece(int index){
    ListHead *ptr;
    pthread_mutex_lock(&download_mutex);
    list_foreach(ptr,&downloading_piece_head){
        downloading_piece *tmp = list_entry(ptr,downloading_piece,list);
        //printf("downloading piece %d\n",tmp->index);
        if(tmp->index == index){
            pthread_mutex_unlock(&download_mutex);
            return tmp;
        }
    }
    pthread_mutex_unlock(&download_mutex);
    return NULL;
}

//init structure downloading_piece
downloading_piece *init_downloading_piece(int index){
    if(find_downloading_piece(index) != NULL)
        return NULL;
    downloading_piece *d_piece = (downloading_piece *)malloc(sizeof(downloading_piece));
    d_piece->index = index;
    d_piece->sub_piece_size = SUB_PIECE_SIZE;
    int real_len = real_piece_len(index);
    int tmp1 = real_len/d_piece->sub_piece_size;
    int tmp2 = (real_len%d_piece->sub_piece_size != 0);
    d_piece->sub_piece_num = tmp1 + tmp2;
    d_piece->downloading_num = 0;
    d_piece->sub_piece_state = (int *)malloc(4*d_piece->sub_piece_num);
    memset(d_piece->sub_piece_state,0,4*d_piece->sub_piece_num);
    pthread_mutex_lock(&download_mutex);
    list_add_before(&downloading_piece_head,&d_piece->list);
    pthread_mutex_unlock(&download_mutex);
    return d_piece;
}


// init p2p control block
void init_p2p_block(P2PCB *node){
    node->am_choking = 1;
    node->am_interested = 0;
    node->peer_choking = 1;
    node->peer_interested = 0;
    list_init(&node->list);
}

//whether a bitfield is interested about another
int is_interested_bitfield(char *bitfield1, char *bitfield2, int len){
    char interest_bitfield[len];
    for(int i = 0; i < len; i++){
        interest_bitfield[i] = (~bitfield1[i]) & bitfield2[i]; 
    }
    if(is_bitfield_empty(interest_bitfield,len))
        return 0;
    else
        return 1;
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

void* process_p2p_conn(void *param){
    //process param 
    p2p_thread_param *currParam = (p2p_thread_param *)param;
    int connfd = currParam->connfd;
    int is_connecter = currParam->is_connecter;

    //init P2PCB
    P2PCB *currP2P = (P2PCB *)malloc(sizeof(P2PCB));
    memset(currP2P,0,sizeof(P2PCB));
    currP2P->connfd = connfd;
    strcpy(currP2P->oppsite_peer_ip, currParam->ip);
    init_p2p_block(currP2P); 
    my_free(currParam);
    
    int bitfield_len = globalInfo.g_torrentmeta->num_pieces/8 + (globalInfo.g_torrentmeta->num_pieces%8 != 0);
    currP2P->oppsite_bitfield = (char *)malloc(bitfield_len);
    memset(currP2P->oppsite_bitfield,0,bitfield_len);
    pthread_mutex_lock(&P2P_mutex);
    list_add_before(&P2PCB_head,&currP2P->list);
    pthread_mutex_unlock(&P2P_mutex);

    //if is_connecter,send handshake msg
    if(is_connecter){
        printf("send a handshake\n");
        send_handshake_msg(connfd);
    }

    //readn handshake msg
    char pstrlen;
    if(readn(connfd,&pstrlen,1) > 0){
        printf("handshake recieved %d\n", pstrlen);
        char pstr[pstrlen];
        char reserved[8];
        int info_hash[5];
        char peer_id[20];
        readn(connfd,pstr,pstrlen);
        readn(connfd,reserved,8);
        readn(connfd,info_hash,20);
        int i;
        for (i = 0; i < 5; i++){
            info_hash[i] = ntohl(info_hash[i]);
        }
        readn(connfd,peer_id,20);
#ifdef DEBUG
        int j;
        for (j = 0; j < 5; j++){
            printf("%08X ", info_hash[j]);
        }
        printf("\nvs\n");
        for (j = 0; j < 5; j++){
            printf("%08X ", globalInfo.g_torrentmeta->info_hash[j]);
        }
        printf("\n");
#endif
        if(memcmp(info_hash, (char *)globalInfo.g_torrentmeta->info_hash,20) != 0){//wrong hash_info 
            printf("wrong handshake msg\n");
            drop_conn(currP2P);
            return NULL;
        }
        else{
            pthread_mutex_lock(&P2P_mutex);
            memcpy(currP2P->oppsite_peer_id,peer_id,20);
            pthread_mutex_unlock(&P2P_mutex);
            if(!is_connecter)
                send_handshake_msg(connfd);//send back with local peer_id
        }
    }

    //send bitfield msg
    if(!is_bitfield_empty(globalInfo.bitfield,bitfield_len)){
        char bitfield_msg[5+bitfield_len];
        *(int*)bitfield_msg = htonl(1+bitfield_len);
        bitfield_msg[4] = 5;
        memcpy(bitfield_msg+5,globalInfo.bitfield,bitfield_len);
        printf("bitfield to send:%02X %02X\n", bitfield_msg[5], globalInfo.bitfield[0]);
        send(connfd,bitfield_msg,5+bitfield_len,0);
    }

    //process msgs
    char prefix[5];
    while(readn(connfd, prefix, 4) > 0){
    ListHead *ptr;
    pthread_mutex_lock(&download_mutex);
    list_foreach(ptr,&downloading_piece_head){
        downloading_piece *d_piece = list_entry(ptr,downloading_piece,list);
        printf("**** %d ****\n",d_piece->index);
    }
    pthread_mutex_unlock(&download_mutex);
        int len = ntohl(*(int *)prefix);
        if(len == 0){//keep-alive
            continue;
        }
        else{
            readn(connfd,prefix+4,1);
        }
        switch(prefix[4]){
            case 0 : {//choke
                         pthread_mutex_lock(&P2P_mutex);
                         currP2P->am_choking = 1;
                         pthread_mutex_unlock(&P2P_mutex);
                         break;
                     }
            case 1 : {//unchoke
                         pthread_mutex_lock(&P2P_mutex);
                         currP2P->am_choking = 0;
                         pthread_mutex_unlock(&P2P_mutex);
                         break;
                     }
            case 2 : {//interested
                         pthread_mutex_lock(&P2P_mutex);
                         currP2P->am_interested = 1;
                         currP2P->peer_choking = 0;
                         pthread_mutex_unlock(&P2P_mutex);
                         send_unchoke_msg(connfd);
                         break;
                     }
            case 3 : {//not interested
                         pthread_mutex_lock(&P2P_mutex);
                         currP2P->am_interested = 0;
                         pthread_mutex_unlock(&P2P_mutex);
                         break;
                     }
            case 4 : {//have  
                         int index;
                         readn(connfd,&index,4);
                         index = ntohl(index);
                         set_bit_at_index(currP2P->oppsite_bitfield,index,1);
                         pthread_mutex_lock(&pieceCounter_mutex);
                         piece_counter[index] ++;//update piece_counter
                         pthread_mutex_unlock(&pieceCounter_mutex);
                         if(get_bit_at_index(globalInfo.bitfield,index) == 0){//send interest
                             send_interest_msg(connfd);
                             pthread_mutex_lock(&P2P_mutex);
                             currP2P->peer_interested = 1;
                             pthread_mutex_unlock(&P2P_mutex);
                             pthread_mutex_lock(&firstReq_mutex);
                             if(first_request == 1){//first request
                                 pthread_mutex_unlock(&firstReq_mutex);
                                 pthread_mutex_lock(&P2P_mutex);
                                 if(currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                     int begin;
                                     int length;
                                     select_next_subpiece(index,&begin,&length);
                                     send_request_msg(connfd,index,begin,length);
                                     first_request = 0;
                                     //set the corresponding downloading piece
                                     downloading_piece *d_piece = init_downloading_piece(index);
                                     pthread_mutex_lock(&download_mutex);
                                     d_piece->downloading_num++;
                                     pthread_mutex_unlock(&download_mutex);
                                     int subpiece_index = begin/d_piece->sub_piece_size;
                                     d_piece->sub_piece_state[subpiece_index] = 1;
                                 }
                                 pthread_mutex_unlock(&P2P_mutex);
                             }
                             else{
                                 pthread_mutex_unlock(&firstReq_mutex);
                                 downloading_piece *d_piece = find_downloading_piece(index);
                                 pthread_mutex_lock(&P2P_mutex);
                                 pthread_mutex_lock(&download_mutex);
                                 if(d_piece->downloading_num < MAX_REQUEST_NUM && 
                                         d_piece->downloading_num != 0 && 
                                         currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                     pthread_mutex_unlock(&P2P_mutex);
                                     int begin;
                                     int length;
                                     select_next_subpiece(index,&begin,&length);
                                     send_request_msg(connfd,index,begin,length);
                                     d_piece->downloading_num++;
                                     int subpiece_index = begin/d_piece->sub_piece_size;
                                     d_piece->sub_piece_state[subpiece_index] = 1;
                                 }
                                 pthread_mutex_unlock(&P2P_mutex);
                                 pthread_mutex_unlock(&download_mutex);
                             }
                         }
                         break;
                     }
            case 5 : {//bitfield
                         char bitfield[len-1];
                         readn(connfd,bitfield,len-1);
                         if(len - 1 != bitfield_len){
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
                         pthread_mutex_lock(&P2P_mutex);
                         memcpy(currP2P->oppsite_bitfield,bitfield,len-1);
                         pthread_mutex_unlock(&P2P_mutex);

                         //update piece_counter
                         pthread_mutex_lock(&pieceCounter_mutex);
                         for(int i = 0; i < globalInfo.g_torrentmeta->num_pieces; i++){
                             if(get_bit_at_index(bitfield,i) == 1)
                                 piece_counter[i] ++;
                         }
                         pthread_mutex_unlock(&pieceCounter_mutex);
                         
                         //to see whether interedted about oppsite peer
                         pthread_mutex_lock(&P2P_mutex);
                         char *bitfield1 = globalInfo.bitfield;
                         char *bitfield2 = currP2P->oppsite_bitfield;
                         if(is_interested_bitfield(bitfield1,bitfield2,bitfield_len)){
                             send_interest_msg(connfd);
                             currP2P->peer_interested = 1;
                             currP2P->am_choking = 0;
                             pthread_mutex_lock(&firstReq_mutex);
                             if(first_request == 1){//find the first interested piece then request
                                 pthread_mutex_unlock(&firstReq_mutex);
                                 for(int index = 0; index < globalInfo.g_torrentmeta->num_pieces; index++){
                                     if(get_bit_at_index(bitfield1,index) == 0 && 
                                        get_bit_at_index(bitfield2,index) == 1){
                                         if(currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                             //set the corresponding downloading piece
                                             downloading_piece *d_piece = init_downloading_piece(index);
                                             pthread_mutex_lock(&download_mutex);
                                             d_piece->downloading_num++;
                                             pthread_mutex_unlock(&download_mutex);
                                             //send the request
                                             int begin;
                                             int length;
                                             select_next_subpiece(index,&begin,&length);
                                             send_request_msg(connfd,index,begin,length);
                                             first_request = 0;
                                             //change state
                                             int subpiece_index = begin/d_piece->sub_piece_size;
                                             d_piece->sub_piece_state[subpiece_index] = 1;
                                             pthread_mutex_unlock(&P2P_mutex);
                                             break;
                                         }
                                     }
                                 }
                             }
                             else{ 
                                 pthread_mutex_unlock(&firstReq_mutex);
                                 for(int index = 0; index < globalInfo.g_torrentmeta->num_pieces; index++){
                                     if(get_bit_at_index(bitfield1,index) == 0 && 
                                        get_bit_at_index(bitfield2,index) == 1){
                                         downloading_piece *d_piece = find_downloading_piece(index);
                                         if(d_piece == NULL)
                                             continue;
                                         pthread_mutex_lock(&download_mutex);
                                         if(d_piece->downloading_num < MAX_REQUEST_NUM && 
                                                 d_piece->downloading_num != 0 && 
                                                 currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                             int begin;
                                             int length;
                                             if(select_next_subpiece(index,&begin,&length)){
                                                 send_request_msg(connfd,index,begin,length);
                                                 int subpiece_index = begin/d_piece->sub_piece_size;
                                                 d_piece->sub_piece_state[subpiece_index] = 1;
                                             }
                                         }
                                         pthread_mutex_unlock(&download_mutex);
                                     }
                                 }
                             }
                         }
                         pthread_mutex_unlock(&P2P_mutex);
                         break;
                     }
            case 6 : {//request
                         int payload[3];//<index><begin><length>
                         readn(connfd,payload,12);
                         int index = ntohl(payload[0]);
                         int begin = ntohl(payload[1]);
                         int length = ntohl(payload[2]);
                         if(length > (1 << 17)){
                             printf("the length in request is larger than 2^17\n");
                             drop_conn(currP2P);
                             return NULL ;
                         }
                         pthread_mutex_lock(&P2P_mutex);
                         if(currP2P->am_interested == 1 && currP2P->peer_choking == 0){
                             //send the block
                             send_piece_msg(connfd,index,begin,length);
                         }
                         else{
                             printf("the request is invalid,interested = %d,peer_choking = %d\n",
                                     currP2P->am_interested,currP2P->peer_choking);
                         }
                         pthread_mutex_unlock(&P2P_mutex);
                         break;
                     }
            case 7 : {//piece
                        char payload[len-1];
                        readn(connfd,payload,len-1);
                        int index = ntohl(*(int*)payload);
                        int begin = ntohl(*(int*)(payload+4));
                        int length = len - 9;
                        downloading_piece *d_piece = find_downloading_piece(index);
                        pthread_mutex_lock(&download_mutex);
                        if(d_piece == NULL){
                            pthread_mutex_unlock(&download_mutex);
                            continue;
                        }
                        set_block(index,begin,length,payload+8);
                        int subpiece_index = begin/d_piece->sub_piece_size;
                        d_piece->sub_piece_state[subpiece_index] = 2;
                        if(!select_next_subpiece(index,&begin,&length)){//a piece is downloaded completely
                            printf("piece %d has been dowmloaded successfully\n",index);
                            set_bit_at_index(globalInfo.bitfield,index,1);//update local bitfield
                            list_del(&d_piece->list);
                            my_free(d_piece->sub_piece_state);
                            my_free(d_piece);
                            pthread_mutex_unlock(&download_mutex);
                            
                            //send have msg
                            ListHead *ptr;
                            pthread_mutex_lock(&P2P_mutex);
                            list_foreach(ptr,&P2PCB_head){
                                P2PCB *tmpP2P = list_entry(ptr,P2PCB,list);
                                send_have_msg(tmpP2P->connfd,index);
                            }
                            pthread_mutex_unlock(&P2P_mutex);

                            if(is_bitfield_complete(globalInfo.bitfield)){
                                //the file has been dowmloaded completely
                                printf("File has been downloaded completely\n");
                                ListHead *ptr_;
                                pthread_mutex_lock(&P2P_mutex);
                                list_foreach(ptr_,&P2PCB_head){
                                    P2PCB *tmpP2P = list_entry(ptr_,P2PCB,list);
                                    if(tmpP2P->peer_interested == 1){
                                        tmpP2P->peer_interested = 0;
                                        send_not_interest_msg(tmpP2P->connfd);
                                    }
                                }
                                pthread_mutex_unlock(&P2P_mutex);
                                continue;
                            }

                            int next_index = select_next_piece();
                            printf("*** tag-select piece %d ***\n", next_index);
                            downloading_piece *next_d_piece;
                            if(next_index != -1)
                                next_d_piece = init_downloading_piece(next_index);
                            //update peer_interested,send not interested msg,request for next piece
                            pthread_mutex_lock(&P2P_mutex);
                            list_foreach(ptr,&P2PCB_head){
                                P2PCB *tmpP2P = list_entry(ptr,P2PCB,list);
                                char *bitfield1 = globalInfo.bitfield;
                                char *bitfield2 = tmpP2P->oppsite_bitfield;
                                if(!is_interested_bitfield(bitfield1,bitfield2,bitfield_len) && 
                                        tmpP2P->peer_interested == 1){//change to be not interested
                                    send_not_interest_msg(tmpP2P->connfd);
                                    tmpP2P->peer_interested = 0;
                                }
                                if(next_index == -1){
                                    pthread_mutex_unlock(&P2P_mutex);
                                    continue;
                                }
                                //request for next piece
                                static int no_more_sub_piece = 0;
                                if(no_more_sub_piece){
                                    pthread_mutex_unlock(&P2P_mutex);
                                    continue;
                                }
                                pthread_mutex_lock(&download_mutex);
                                if(next_d_piece->downloading_num < MAX_REQUEST_NUM && 
                                        get_bit_at_index(bitfield2,next_index) == 1 &&
                                        tmpP2P->am_choking == 0 && tmpP2P->peer_interested == 1){
                                    int begin_;
                                    int length_;
                                    if(!select_next_subpiece(next_index,&begin_,&length_))
                                        no_more_sub_piece = 1;//no more sub piece
                                    send_request_msg(tmpP2P->connfd,next_index,begin_,length_);
                                    next_d_piece->downloading_num++;
                                    int subpiece_index = begin_/next_d_piece->sub_piece_size;
                                    next_d_piece->sub_piece_state[subpiece_index] = 1;
                                }   
                                pthread_mutex_unlock(&download_mutex);
                            }
                            pthread_mutex_unlock(&P2P_mutex);
                        }
                        else{// request for next sub piece
                            pthread_mutex_lock(&P2P_mutex);
                            if(currP2P->peer_interested == 1 && currP2P->am_choking == 0){
                                send_request_msg(connfd,index,begin,length);
                                int subpiece_index = begin/d_piece->sub_piece_size;
                                d_piece->sub_piece_state[subpiece_index] = 1;
                            }
                            pthread_mutex_unlock(&P2P_mutex);
                            pthread_mutex_unlock(&download_mutex);
                        }
                        break;
                     }
            case 8 : {//cancel
                        break;
                     }
        }
    }

    //update piece_counter 
    pthread_mutex_lock(&pieceCounter_mutex);
    for(int i = 0; i < globalInfo.g_torrentmeta->num_pieces; i++){
        if(get_bit_at_index(currP2P->oppsite_bitfield,i) == 1)
            piece_counter[i] --;
    }
    pthread_mutex_unlock(&pieceCounter_mutex);

    printf("exit the p2p msg process\n");
    pthread_mutex_lock(&P2P_mutex);
    list_del(&currP2P->list);
    pthread_mutex_unlock(&P2P_mutex);
    my_free(currP2P->oppsite_bitfield);
    my_free(currP2P);


    return NULL;
}

void send_have_msg(int connfd,int index){
    printf("send have\n");
    char have_msg[9];
    *(int*)have_msg = htonl(5);
    have_msg[4] = 4;
    *(int*)(have_msg+5) = htonl(index);
    send(connfd,have_msg,9,0);
}

void send_request_msg(int connfd,int index,int begin,int length){
    printf("send request,index:%d,begin:%d,length:%d\n",index,begin,length);
    char request_msg[17];
    *(int*)request_msg = htonl(13);
    request_msg[4] = 6;
    *(int*)(request_msg+5) = htonl(index);
    *(int*)(request_msg+9) = htonl(begin);
    *(int*)(request_msg+13) = htonl(length);
    send(connfd,request_msg,17,0);
}

void send_interest_msg(int connfd){
    printf("send interest\n");
    char interest_msg[5];
    *(int*)interest_msg = htonl(1);
    interest_msg[4] = 2;
    send(connfd,interest_msg,5,0);
}

void send_choke_msg(int connfd){
    printf("send chock\n");
    char choke_msg[5];
    *(int*)choke_msg = htonl(1);
    choke_msg[4] = 0;
    send(connfd,choke_msg,5,0);
}

void send_not_interest_msg(int connfd){
    printf("send not interest\n");
    char not_interest_msg[5];
    *(int*)not_interest_msg = htonl(1);
    not_interest_msg[4] = 3;
    send(connfd,not_interest_msg,5,0);
}

void send_unchoke_msg(int connfd){
    printf("send unchoke\n");
    char unchoke_msg[5];
    *(int*)unchoke_msg = htonl(1);
    unchoke_msg[4] = 1;
    send(connfd,unchoke_msg,5,0);
}   

void send_piece_msg(int connfd, int index, int begin, int length){
    printf("send piece: %d\n", length);
    char block[length];
    get_block(index,begin,length,block);
    char piece_msg[13 + length];
    *(int *)piece_msg = htonl(9 + length);
    piece_msg[4] = 7;
    *(int *)(piece_msg+5) = htonl(index);
    *(int *)(piece_msg+9) = htonl(begin);
    memcpy(piece_msg+13,block,length);
    send(connfd,piece_msg,13+length,0);
}

void send_handshake_msg(int connfd){
    char *pstr = "BitTorrent protocol";
    char pstrlen = strlen(pstr);
    char handshake_msg[49+pstrlen];
    memset(handshake_msg,0,49+pstrlen);
    handshake_msg[0] = pstrlen;
    memcpy(handshake_msg+1,pstr,pstrlen);
#ifdef DEBUG
    int j;
    printf("send info_hash:");
    for (j = 0; j < 5; j++){
        printf("%08X ", globalInfo.g_torrentmeta->info_hash[j]);
    }
    printf("\n");
#endif
    int tmphash[5];
    int i;
    for (i = 0; i < 5; i++){
        tmphash[i] = htonl(globalInfo.g_torrentmeta->info_hash[i]);
    }
    memcpy(handshake_msg+9+pstrlen,tmphash,20);
    memcpy(handshake_msg+29+pstrlen,globalInfo.g_my_id,20);
#ifdef DEBUG
    for (j = 0; j < 49 + pstrlen; j++)
        printf("%02X ", handshake_msg[j]);
    printf("\n");
#endif
    send(connfd,handshake_msg,49+pstrlen,0);
}


