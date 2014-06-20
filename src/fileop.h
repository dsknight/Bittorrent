/*
 * =====================================================================================
 *
 *       Filename:  fileop.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年06月04日 17时07分58秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef __FILE_OP_H_
#define __FILE_OP_H_

#include <stdbool.h>
#include <stdio.h>

struct fileinfo_t {
    FILE *fp;
    char filename[80];// with path
    int begin_index;
    int size;
};


int filesize(FILE *fp);
bool exists(char *filepath);
FILE *createfile(char *filepath, int size);
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int store_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int get_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size);
int store_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size);
void get_block(int index, int begin, int length, char *block);
void set_block(int index, int begin, int length, char *block);
char *gen_bitfield(char *piece_hash, int piece_len, int piece_num);

#endif
