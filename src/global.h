/*
 * =====================================================================================
 *
 *       Filename:  global.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年05月31日 16时09分25秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include "btdata.h"
#include <stdbool.h>

#ifndef __GLOBAL_H_
#define __GLOBAL_H_

struct globalArgs_t{
    char*   torrentpath;
    int     isseed;
    int     port;
    bool    verbose;
};
extern struct globalArgs_t globalArgs;

struct globalInfo_t{
    char    g_my_ip[16];
    char    g_tracker_ip[16];
    char    g_my_id[20];
    int     g_peer_port;
    int     g_tracker_port;
    int     g_done;
    int     g_uploaded;
    int     g_downloaded;
    int     g_left;
    char    *bitfield;
    torrentmetadata_t   *g_torrentmeta;
    tracker_data        *g_tracker_response;
};

extern struct globalInfo_t globalInfo;
extern int listenfd;
#endif
