/*
 * =====================================================================================
 *
 *       Filename:  fileop.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年06月04日 17时06分49秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fileop.h"
#include "sha1.h"
#include "global.h"

static char set_bit[8] = {1,2,4,8,16,32,64,128};

static inline void set_bit_at_index(char *info, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        info[index/8] = info[index/8] | set_bit[offset];
    else
        info[index/8] = info[index/8] & (~set_bit[offset]);
}

// return size of file
int filesize(FILE *fp){
    int cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return size;
}

// check whether file exist, also work for dir, but make sure dir path is ended by '/'
bool exists(char *filepath){
    if (access(filepath, F_OK) != -1)
        return true;
    return false;
}

// open or create file and set file's size
FILE *createfile(char *filepath, int size){
    if (!exists(filepath)){
        printf("createfile %s\n", filepath);
        // find the directory 
        int i = strlen(filepath) - 1;
        for (; i >= 0; i--){
            if (filepath[i] == '/')
                break;
        }
        if (i != -1){
            // god bless that the dir path isn't too long
            char dir[80];
            memset(dir, 0, 80);
            memcpy(dir, filepath, i + 1);
            if (!exists(dir)){
                printf("createdir %s\n", dir);
                mkdir(dir, 0777);
            }
        }
        int fd = creat(filepath, 0644);
        if (fd == -1){
            int tmp = errno;
            printf("%s\n", strerror(tmp));
            return NULL;
        }
        close(fd);
    }
    FILE *fp = fopen(filepath, "r+");
    if (fp == NULL){
        int tmp = errno;
        printf("%s\n", strerror(tmp));
        return NULL;
    }
    int cursize = filesize(fp);
    if (cursize < size){
        fseek(fp, size - 1, SEEK_SET);
        fputc('a', fp);
    }
    fseek(fp, 0, SEEK_SET);
    return fp;
}

// read special piece from file
// return actual read bytes
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize <= piece_num * piece_size){
        printf("request unexist piece\n");
        return -1;
    }
    if (cursize < (piece_num + 1) * piece_size){
        fseek(fp, piece_num * piece_size, SEEK_SET);
        return fread(buf, sizeof(char), cursize - (piece_num * piece_size), fp);
    }
    fseek(fp, piece_num * piece_size, SEEK_SET);
    return fread(buf, sizeof(char), piece_size, fp);
}

// write special piece to file
// return actual write bytes
int store_piece(FILE *fp, char *buf, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize <= piece_num * piece_size){
        printf("write unexist piece\n");
        return -1;
    }
    if (cursize < (piece_num + 1) * piece_size){
        fseek(fp, piece_num * piece_size, SEEK_SET);
        return fwrite(buf, sizeof(char), cursize - (piece_num * piece_size), fp);
    }
    fseek(fp, piece_num * piece_size, SEEK_SET);
    return fwrite(buf, sizeof(char), piece_size, fp);
}

// read sub piece from file
int get_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize < piece_num * piece_size + len){
        printf("read sub piece beyond file size\n");
        return -1;
    }
    fseek(fp, piece_num * piece_size + begin, SEEK_SET);
    return fread(buf, sizeof(char), len, fp);
}

// write sub piece to file
int store_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize < piece_num * piece_size + len){
        printf("write sub piece beyond file size\n");
        return -1;
    }
    fseek(fp, piece_num * piece_size + begin, SEEK_SET);
    return fwrite(buf, sizeof(char), len, fp);
}

int list_set_piece(struct fileinfo_t *fileinfo, int filenum, char *buf, int len, int begin){
    if (fileinfo[filenum - 1].begin_index + fileinfo[filenum - 1].size < begin + len){
        printf("write beyond file list\n");
        return -1;
    }
    int i;
    for (i = 0; i < filenum; i++){
        if (fileinfo[i].begin_index <= begin && fileinfo[i].begin_index + fileinfo[i].size > begin){
            // write to a file
            if (fileinfo[i].begin_index + fileinfo[i].size - begin - len >= 0){
                fseek(fileinfo[i].fp, begin - fileinfo[i].begin_index, SEEK_SET);
                return fwrite(buf, sizeof(char), len, fileinfo[i].fp);
            } else {
            // write to multi file
                int writesize = fileinfo[i].begin_index + fileinfo[i].size - begin;
                fseek(fileinfo[i].fp, begin - fileinfo[i].begin_index, SEEK_SET);
                int partsize = fwrite(buf, sizeof(char), writesize, fileinfo[i].fp);
                return partsize + list_set_piece(fileinfo, filenum, buf + writesize, len - writesize, begin + writesize);
            }
        }
    }
    assert(false && "if argu pass the first if stmt, it shouldn't access here");
    return -1;
}

int list_get_piece(struct fileinfo_t *fileinfo, int filenum, char *buf, int len, int begin){
    if (fileinfo[filenum - 1].begin_index + fileinfo[filenum - 1].size < begin + len){
        printf("write beyond file list\n");
        return -1;
    }
    int i;
    for (i = 0; i < filenum; i++){
        if (fileinfo[i].begin_index <= begin && fileinfo[i].begin_index + fileinfo[i].size > begin){
            // write to a file
            if (fileinfo[i].begin_index + fileinfo[i].size - begin - len >= 0){
                fseek(fileinfo[i].fp, begin - fileinfo[i].begin_index, SEEK_SET);
                return fread(buf, sizeof(char), len, fileinfo[i].fp);
            } else {
            // write to multi file
                int writesize = fileinfo[i].begin_index + fileinfo[i].size - begin;
                fseek(fileinfo[i].fp, begin - fileinfo[i].begin_index, SEEK_SET);
                int partsize = fread(buf, sizeof(char), writesize, fileinfo[i].fp);
                return partsize + list_get_piece(fileinfo, filenum, buf + writesize, len - writesize, begin + writesize);
            }
        }
    }
    assert(false && "if argu pass the first if stmt, it shouldn't access here");
    return -1;
}



void get_block(int index, int begin, int length, char *block){
    list_get_piece(globalInfo.g_torrentmeta->flist, globalInfo.g_torrentmeta->filenum, block, length, index * globalInfo.g_torrentmeta->piece_len + begin);
}
void set_block(int index, int begin, int length, char *block){
    list_set_piece(globalInfo.g_torrentmeta->flist, globalInfo.g_torrentmeta->filenum, block, length, index * globalInfo.g_torrentmeta->piece_len + begin);
}

// generate bitfield 
char *gen_bitfield(char *piece_hash, int piece_len, int piece_num){
    struct fileinfo_t *filelist = globalInfo.g_torrentmeta->flist;
    int filenum = globalInfo.g_torrentmeta->filenum;
    int cursize = filelist[filenum - 1].begin_index + filelist[filenum - 1].size;
    char *bitfield = (char *)malloc(piece_num / 8 + 1);
    memset(bitfield, 0, piece_num / 8 + 1);
    char *hashbuf = (char *)malloc(piece_len);
    typedef struct {int hash[5];} *hashptr_t;
    hashptr_t ptr = (hashptr_t) piece_hash;
    int i;
    for (i = 0; i < piece_num; i++){
        int blocksize = (i != piece_num - 1)?piece_len:(cursize - (piece_num - 1) * piece_len);
        list_get_piece(filelist, filenum, hashbuf, blocksize, piece_len * i);
        SHA1Context sha;
        SHA1Reset(&sha);
        SHA1Input(&sha, (const unsigned char*)hashbuf, blocksize);
        if(!SHA1Result(&sha))
        {
            printf("FAILURE in count sha when generate bitfield\n");
            assert(false);
        }
        int j;
        for (j = 0; j < 5; j++){
            sha.Message_Digest[j] = htonl(sha.Message_Digest[j]);
        }
#ifdef DEBUG
        for (j = 0; j < 5; j++){
            printf("%08X ", sha.Message_Digest[j]);
        }
        printf("\nvs\n");
        for (j = 0; j < 5; j++){
            printf("%08X ", ptr->hash[j]);
        }
        printf("\n\n");
#endif
        if (memcmp(sha.Message_Digest, ptr->hash, 20) == 0){
            printf("write 1\n");
            globalInfo.g_downloaded += globalInfo.g_torrentmeta->piece_len;
            set_bit_at_index(bitfield, i, 1);
        } else {
            printf("write 0\n");
            set_bit_at_index(bitfield, i, 0);
        }
        ptr++;
    }

    free(hashbuf);
    return bitfield;
}
/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  main
 *  Description:  
 * =====================================================================================
 */
int
testfile ( int argc, char *argv[] )
{
    char buf[20];
    FILE *fp = createfile("newfile", 13);
    strcpy(buf, "world");
    store_piece(fp, buf, 1, 5);

    fclose(fp);
    return EXIT_SUCCESS;
}
