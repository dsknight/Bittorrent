
#include <pthread.h>
#include "bencode.h"
#include "fileop.h"

#ifndef BTDATA_H
#define BTDATA_H

/**************************************
 * 一些常量定义
 **************************************/
/*
#define HANDSHAKE_LEN 68  // peer握手消息的长度, 以字节为单位
#define BT_PROTOCOL "BitTorrent protocol"
#define INFOHASH_LEN 20
#define PEER_ID_LEN 20
#define MAXPEERS 100
#define KEEP_ALIVE_INTERVAL 3
*/

#define BT_STARTED 0
#define BT_STOPPED 1
#define BT_COMPLETED 2

/**************************************
 * 数据结构
 **************************************/
// Tracker HTTP响应的数据部分
typedef struct _tracker_response {
    int size;       // B编码字符串的字节数
    char* data;     // B编码的字符串
} tracker_response;

// 元信息文件中包含的数据
typedef struct _torrentmetadata {
    int info_hash[5]; // torrent的info_hash值(info键对应值的SHA1哈希值)
    char* announce; // tracker的URL
    int length;     // 文件长度, 以字节为单位
    char name[80];     // 文件名
    struct fileinfo_t flist[20];
    int filenum;
    int piece_len;  // 每一个分片的字节数
    int num_pieces; // 分片数量(为方便起见)
    char* pieces;   // 针对所有分片的20字节长的SHA1哈希值连接而成的字符串
} torrentmetadata_t;

// 包含在announce url中的数据(例如, 主机名和端口号)
typedef struct _announce_url_t {
    char* hostname;
    int port;
} announce_url_t;

// 由tracker返回的响应中peers键的内容
typedef struct _peerdata {
    char id[21]; // 20用于null终止符
    int port;
    char ip[16]; // Null终止
} peerdata;

// 包含在tracker响应中的数据
typedef struct _tracker_data {
    int interval;
    int numpeers;
    peerdata* peers;
} tracker_data;

typedef struct _tracker_request {
    int info_hash[5];
    char peer_id[20];
    int port;
    int uploaded;
    int downloaded;
    int left;
    char ip[16]; // 自己的IP地址, 格式为XXX.XXX.XXX.XXX, 最后以'\0'结尾
} tracker_request;

// 针对到一个peer的已建立连接, 维护相关数据
typedef struct _peer_t {
    int sockfd;
    int choking;        // 作为上传者, 阻塞远端peer
    int interested;     // 远端peer对我们的分片有兴趣
    int choked;         // 作为下载者, 我们被远端peer阻塞
    int have_interest;  // 作为下载者, 对远端peer的分片有兴趣
    char name[20]; 
} peer_t;

#endif
