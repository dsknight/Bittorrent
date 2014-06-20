#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "btdata.h"
#include "util.h"

/* readn - read exactly n bytes */
int readn(int fd, char *bp, size_t len)
{
	int cnt;
	int rc;

	cnt = len;
	while ( cnt > 0 )
	{
		rc = recv( fd, bp, cnt, 0 );
		if ( rc < 0 )				/* read error? */
		{
			if ( errno == EINTR )	/* interrupted? */
				continue;			/* restart the read */
            printf("%s\n", strerror(errno));
			return -1;				/* return error */
		}
		if ( rc == 0 )				/* EOF? */
			return len - cnt;		/* return short count */
		bp += rc;
		cnt -= rc;
	}
	return len;
}

// 读取并处理来自Tracker的HTTP响应, 确认它格式正确, 然后从中提取数据. 
// 一个Tracker的HTTP响应格式如下所示:
// (I've annotated it)
// HTTP/1.0 200 OK       (17个字节,包括最后的\r\n)
// Content-Type: text/plain (26个字节)
// Content-Length: X     (到第一个空格为16个字节) 注意: X是一个数字
// \r\n  (空行, 表示数据的开始)
// data                  注意: 从这开始是数据, 但并没有一个data标签
tracker_response* preprocess_tracker_response(int sockfd)
{ 
    char recvline[MAXLINE];
    char head[MAXLINE];
    int len = -1;
    int offset = 0;
    memset(recvline, 0, MAXLINE);
    printf("Reading tracker response...\n");
    
    // Read HTTP header
    // keep reading until read double \r\n
    while(1 == readn(sockfd, &recvline[offset], 1)){
        if (recvline[offset] == '\r'){
            readn(sockfd, &recvline[offset + 1], 3);
            if (strncmp(&recvline[offset], "\r\n\r\n", 4) == 0){
                offset += 4;
                break;
            }
            offset += 4;
            continue;
        }
        offset += 1;
    }

    // parsering HTTP header
    memset(head, 0, MAXLINE);
    memcpy(head, recvline, offset);
    char *begin, *end;
    begin = head;
    while(begin < &head[offset] && (end = strstr(begin, "\r\n"), end != NULL)){
        if (begin == end){
            break;
        } else {
            char buf[80];
            memset(buf, 0, 80);
            memcpy(buf, begin, end - begin);
            if (begin == head){
                if(memcmp(buf, "HTTP/1.1 200 OK", sizeof("HTTP/1.1 200 OK") - 1) != 0 && memcmp(buf, "HTTP/1.0 200 OK", sizeof("HTTP/1.0 200 OK") - 1) != 0){
                    printf("Wrong tracker response: %s\n", buf);
                    return NULL;
                }
            } else if (memcmp(buf, "Content-Type", sizeof("Content-Type") - 1) == 0){
                if (memcmp(buf + sizeof("Content-Type") + 1, "text/plain", sizeof("text/plain") - 1) != 0){
                    printf("Wrong tracker response type: %s\n", buf);
                    return NULL;
                }
            } else if (memcmp(buf, "Content-Length", sizeof("Content-Length") - 1) == 0){
                char *plen = begin + sizeof("Content-Length") + 1;
                char lenfield[20];
                memset(lenfield, 0, 20);
                memcpy(lenfield, plen, end - plen);
                len = atoi(lenfield);
            }
            begin = end + 2;
        }
    }

    // read HTTP content
    //assert(len != -1 && "HTTP header should have the Content-Length item");
    memset(recvline, 0, MAXLINE);
    if (len != -1){
        if (readn(sockfd, recvline, len) < 0){
            printf("Error when read HTTP content\n");
            return NULL;
        }
    } else {
        if ((len = read(sockfd, recvline, MAXLINE)) < 0){
            printf("Error when read HTTP content\n");
            return NULL;
        }
    }

    tracker_response* ret;
    ret = (tracker_response*)malloc(sizeof(tracker_response));
    if(ret == NULL)
    {
        printf("Error allocating tracker_response ptr\n");
        return 0;
    }
    ret->size = len;
    ret->data = (char *)malloc(len + 1);
    memcpy(ret->data, recvline, len);
    ret->data[len] = '\0';

    return ret;
}

// 解码B编码的数据, 将解码后的数据放入tracker_data结构
tracker_data* get_tracker_data(char* data, int len)
{
    tracker_data* ret;
    be_node* ben_res;
    ben_res = be_decoden(data,len);
    if(ben_res->type != BE_DICT)
    {
        perror("Data not of type dict");
        exit(-12);
    }

    ret = (tracker_data*)malloc(sizeof(tracker_data));
    if(ret == NULL)
    {
        perror("Could not allcoate tracker_data");
        exit(-12);
    }

    // 遍历键并测试它们
    int i;
    for (i=0; ben_res->val.d[i].val != NULL; i++)
    { 
        //printf("%s\n",ben_res->val.d[i].key);
        // 检查是否有失败键
        if(!strncmp(ben_res->val.d[i].key,"failure reason",strlen("failure reason")))
        {
            printf("Error: %s",ben_res->val.d[i].val->val.s);
            exit(-12);
        }
        // interval键
        if(!strncmp(ben_res->val.d[i].key,"interval",strlen("interval")))
        {
            ret->interval = (int)ben_res->val.d[i].val->val.i;
        }
        // peers键
        if(!strncmp(ben_res->val.d[i].key,"peers",strlen("peers")))
        { 
            be_node* peer_list = ben_res->val.d[i].val;
            get_peers(ret,peer_list);
        }
    }

    be_free(ben_res);

    return ret;
}
// 处理来自Tracker的字典模式的peer列表
void get_peers(tracker_data* td, be_node* peer_list)
{
    int i;
    int numpeers = 0;

    if (peer_list->type == BE_DICT){ 
        // 计算列表中的peer数
        for (i=0; peer_list->val.l[i] != NULL; i++)
        {
            // 确认元素是一个字典
            if(peer_list->val.l[i]->type != BE_DICT)
            {
                perror("Expecting dict, got something else");
                exit(-12);
            }

            // 找到一个peer, 增加numpeers
            numpeers++;
        }

        printf("Num peers: %d\n",numpeers);

        // 为peer分配空间
        td->numpeers = numpeers;
        td->peers = (peerdata*)malloc(numpeers*sizeof(peerdata));
        if(td->peers == NULL)
        {
            perror("Couldn't allocate peers");
            exit(-12);
        }

        // 获取每个peer的数据
        for (i=0; peer_list->val.l[i] != NULL; i++)
        {
            get_peer_data(&(td->peers[i]),peer_list->val.l[i]);
        }
    } else if (peer_list->type == BE_STR){
        long long peerlen = be_str_len(peer_list);
        int peersnum = peerlen / 6;
        td->numpeers = peersnum;
        td->peers = (peerdata *)malloc(peersnum * sizeof(peerdata));
        int i;
        for (i = 0; i < peersnum; i++){
            unsigned char *begin = (unsigned char *)peer_list->val.s + (i * 6);
            sprintf(td->peers[i].ip, "%d.%d.%d.%d", begin[0], begin[1], begin[2], begin[3]);
            td->peers[i].port = (begin[4] << 8) + begin[5];
            memset(td->peers[i].id, 0, sizeof(td->peers[i].id));
        }
    } else {
        assert(false && "peers should be dict or string! something wrong");
    }

    return;

}

// 给出一个peerdata的指针和一个peer的字典数据, 填充peerdata结构
void get_peer_data(peerdata* peer, be_node* ben_res)
{
    int i;

    if(ben_res->type != BE_DICT)
    {
        perror("Don't have a dict for this peer");
        exit(-12);
    }

    // 遍历键并填充peerdata结构
    for (i=0; ben_res->val.d[i].val != NULL; i++)
    { 
        //printf("%s\n",ben_res->val.d[i].key);

        // peer id键
        if(!strncmp(ben_res->val.d[i].key,"peer id",strlen("peer id")))
        {
            //printf("Peer id: %s\n", ben_res->val.d[i].val->val.s);
            memcpy(peer->id,ben_res->val.d[i].val->val.s,20);
            peer->id[20] = '\0';
            /*
               int idl;
               printf("Peer id: ");
               for(idl=0; idl<len; idl++)
               printf("%02X ",(unsigned char)peer->id[idl]);
               printf("\n");
               */
        }
        // ip键
        if(!strncmp(ben_res->val.d[i].key,"ip",strlen("ip")))
        {
            int len;
            //printf("Peer ip: %s\n",ben_res->val.d[i].val->val.s);
            len = strlen(ben_res->val.d[i].val->val.s);
            strcpy(peer->ip,ben_res->val.d[i].val->val.s);
        }
        // port键
        if(!strncmp(ben_res->val.d[i].key,"port",strlen("port")))
        {
            //printf("Peer port: %d\n",ben_res->val.d[i].val->val.i);
            peer->port = ben_res->val.d[i].val->val.i;
        }
    }
}
