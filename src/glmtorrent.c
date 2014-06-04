/*
 * =====================================================================================
 *
 *       Filename:  glmtorrent.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年05月31日 15时57分19秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <errno.h>
#include "util.h"
#include "global.h"
#include "PWP.h"

struct globalArgs_t globalArgs;
const char *optstring = "p:i:vh?";

struct globalInfo_t globalInfo;

ListHead P2PCB_head;
torrent_info currTorrent;

void useage(){
    printf("Useage:\n\t./simpletorrent [-i isseed] [-p port] [-v] [-h] torrentpath\n\n");
    printf("[-i isseed]\n\tisseed is optional, 1 indicates this is a seed and won't contact other clients.");
    printf("0 indicates a downloading client and will connect to other clients and receive connections. 0 is also default value\n");
    printf("[-p port]\n\tport is optional, default is 6881\n");
    printf("[-v]\n\toutput more information");
    printf("[-h]\n\t-h will print useage\n");
    printf("torrentpath\n\ttorrentpath is necessary\n");
}

// set peer id to a random value 
void init_peer_id(char id[20]){
    int i;
    srand(time(NULL));
    for (i = 0; i < 20; i++){
        id[i] = rand() % 0x100;
    }
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  main
 *  Description:  
 * =====================================================================================
 */
    
int
main ( int argc, char *argv[] )
{
    // <-- deal with argument -->
    globalArgs.port = 6881;
    globalArgs.isseed = 0;
    globalArgs.torrentpath = NULL;
    int opt;
    while(opt = getopt(argc, argv, optstring), opt != -1){
        switch(opt){
            case 'p':
                {
                    globalArgs.port = atoi(optarg);
                    break;
                }
            case 'i':
                {
                    globalArgs.isseed = atoi(optarg);
                    break;
                }
            case 'v':
                {
                    globalArgs.verbose = true;
                    break;
                }
            case 'h':
                {
                    useage();
                    exit(0);
                    break;
                }
            case '?':
                {
                    useage();
                    exit(0);
                    break;
                }
            default:
                {
                    assert(false && "some amazing thing happen when parser argument");
                    break;
                }
        }
    }
    globalArgs.torrentpath = argv[argc - 1];
    // <-- end -->
#ifdef DEBUG
    printf("isseed:%d\n", globalArgs.isseed);
    printf("port:%d\n", globalArgs.port);
    printf("torrentpath:%s\n", globalArgs.torrentpath);
    printf("verbose:%s\n", (globalArgs.verbose)?"true":"false");
#endif
    
    // <-- set value of globalInfo -->
    init_peer_id(globalInfo.g_my_id); 
    globalInfo.g_peer_port = globalArgs.port;
    globalInfo.g_torrentmeta = parsetorrentfile(globalArgs.torrentpath); 
    if (globalInfo.g_torrentmeta == NULL){
        printf("Error when parsing torrent file\n");
        return -1;
    }
    // <-- end -->
    
    announce_url_t *announce_info = parse_announce_url(globalInfo.g_torrentmeta->announce);
    if (globalArgs.verbose == true){
        printf("host: %s:%d\n", announce_info->hostname, announce_info->port);
    }
    struct hostent *tracker_hostent = gethostbyname(announce_info->hostname);
    if (tracker_hostent == NULL){
        printf("Error when get tracker host\n");
        return -1;
    }
    strcpy(globalInfo.g_tracker_ip, inet_ntoa(*((struct in_addr *)tracker_hostent->h_addr_list[0])));
    globalInfo.g_tracker_port = announce_info->port;
    free(announce_info);
    announce_info = NULL;

    // set the action after recv ctrl-c
    signal(SIGINT, client_shutdown);

    int event_value = BT_STARTED;
    while(true){
        int sockfd = connect_to_host(globalInfo.g_tracker_ip, globalInfo.g_tracker_port);
#ifdef DEBUG
        printf("Sending request to tracker\n");
#endif
        int request_len = 0;
        char *request = make_tracker_request(&globalInfo, event_value, &request_len);
        event_value = -1;
#ifdef DEBUG
        printf("%s", request);
#endif
        if (send(sockfd, request, request_len, 0) == -1){
            int tmp = errno;
            printf("%s\n", strerror(tmp));
            // error when connect to tracker, wait some time and try again
            sleep(5);
            close(sockfd);
            free(request);
            continue;
        }

        tracker_response *tr = preprocess_tracker_response(sockfd);
        close(sockfd);

        globalInfo.g_tracker_response = get_tracker_data(tr->data, tr->size);
        free(tr->data);
        free(tr);

#ifdef DEBUG
        printf("Num Peers: %d\n", globalInfo.g_tracker_response->numpeers);
        int i;
        for (i = 0; i < globalInfo.g_tracker_response->numpeers; i++){
            printf("Peer ip: %s:%d\n", globalInfo.g_tracker_response->peers[i].ip, globalInfo.g_tracker_response->peers[i].port);
        }
#endif
        printf("sleep %d seconds\n", globalInfo.g_tracker_response->interval);
        sleep(globalInfo.g_tracker_response->interval);
    }

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */
