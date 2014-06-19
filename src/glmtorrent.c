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
#include "fileop.h"

struct globalArgs_t globalArgs;
const char *optstring = "p:i:vh?";

struct globalInfo_t globalInfo;

int listenfd;

void sigpipe_handle(int signalnum){
    // ignore
}

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

void *show_speed(void *arg){
    int old_download = globalInfo.g_downloaded;
    char bar[] = "=================================================>";
    for(;;){
        sleep(2);
        int current_download = globalInfo.g_downloaded;
        double speed = (double)(current_download - old_download)/3.0;
        double proportion = (double)current_download/(double)globalInfo.g_torrentmeta->length;
        int index = (proportion >= 1)?0:(49 - (int)(proportion * 50));
        printf("speed:%5.1fKB/s [%-50s]\r", speed / 1024, &bar[index]);
        old_download = current_download;
        fflush(stdout);
    }
}

void *daemon_listen(void *arg){
    int sockfd = (int)arg;

    printf("daemon is running\n");
    for(;;){
        struct sockaddr_in cliaddr;
        socklen_t cliaddr_len;
        cliaddr_len = sizeof(struct sockaddr_in);

        pthread_testcancel();

        int connfd = accept(sockfd, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if (connfd < 0){
            int tmp = errno;
            printf("Error when accept socket:%s\n", strerror(tmp));
            continue;
        }
        printf("receive a connect from %s\n", inet_ntoa(cliaddr.sin_addr));
        pthread_t tid;
        p2p_thread_param *param = (p2p_thread_param *)malloc(sizeof(p2p_thread_param));
        param->connfd = connfd;
        param->is_connecter = 0;
        strcpy(param->ip, inet_ntoa(cliaddr.sin_addr));
        if (pthread_create(&tid, NULL, process_p2p_conn, param) != 0){
            printf("Error when create thread accept request\n");
        } else {
            printf("Success create thread to accept request\n");
        }
    }

    return NULL;
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
    int i;
    //init ListHead
    list_init(&P2PCB_head);
    list_init(&downloading_piece_head);

    //init mutex
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_settype(&mutex_attr,PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&P2P_mutex,&mutex_attr);
    pthread_mutex_init(&download_mutex,&mutex_attr);
    pthread_mutex_init(&firstReq_mutex,&mutex_attr);
    pthread_mutex_init(&pieceCounter_mutex,&mutex_attr);

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
    for (i = 0; i < globalInfo.g_torrentmeta->filenum; i++){
        globalInfo.g_torrentmeta->flist[i].fp = createfile(globalInfo.g_torrentmeta->flist[i].filename, globalInfo.g_torrentmeta->flist[i].size);
    }
    globalInfo.bitfield = gen_bitfield(globalInfo.g_torrentmeta->pieces, globalInfo.g_torrentmeta->piece_len, globalInfo.g_torrentmeta->num_pieces);

#ifdef DEBUG
    printf("bitfield:");
    for (i = 0; i <= globalInfo.g_torrentmeta->num_pieces / 8; i++)
        printf("%X ", globalInfo.bitfield[i]);
    printf("\n");
#endif 

    //set piece_counter for "least first"
    piece_counter = (int *)malloc(sizeof(int)*globalInfo.g_torrentmeta->num_pieces);
    memset(piece_counter,0,sizeof(int)*globalInfo.g_torrentmeta->num_pieces);

    // <-- end -->
    
    // <-- create socket listen to port -->
    listenfd = make_listen_port(globalInfo.g_peer_port);
    if (listenfd == 0){
        printf("Error when create socket for binding:%s\n", strerror(errno));
        exit(-1);
    }
    pthread_t p_daemon;
    if (pthread_create(&p_daemon, NULL, daemon_listen, (void *)listenfd) != 0){
        int tmp = errno;
        printf("Error when create daemon thread: %s\n", strerror(tmp));
        return -1;
    }
    pthread_t p_speed;
    if (pthread_create(&p_speed, NULL, show_speed, NULL) != 0){
        int tmp = errno;
        printf("Error when create show_speed thread: %s\n", strerror(tmp));
        return -1;
    }

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
    signal(SIGPIPE, SIG_IGN);
    
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
        for (i = 0; i < globalInfo.g_tracker_response->numpeers; i++){
            printf("Peer ip: %s:%d\n", globalInfo.g_tracker_response->peers[i].ip, globalInfo.g_tracker_response->peers[i].port);
        }
#endif
        for (i = 0; i < globalInfo.g_tracker_response->numpeers; i++){
            if (!exist_ip(globalInfo.g_tracker_response->peers[i].ip)){
                pthread_t tid;
                p2p_thread_param *param = (p2p_thread_param *)malloc(sizeof(p2p_thread_param));
                param->is_connecter = 1;
                param->port = globalInfo.g_tracker_response->peers[i].port;
                strcpy(param->ip, globalInfo.g_tracker_response->peers[i].ip);
                if (pthread_create(&tid, NULL, process_p2p_conn, param) != 0){
                    printf("Error when create thread to connect peer\n");
                } else {
                    printf("Success create thread to connect peer %s\n", globalInfo.g_tracker_response->peers[i].ip);
                }
            }
        }

        printf("sleep %d seconds\n", globalInfo.g_tracker_response->interval);
        sleep(globalInfo.g_tracker_response->interval);
    }

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */
