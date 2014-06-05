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

int filesize(FILE *fp);
bool exists(char *filepath);
FILE *createfile(char *filepath, int size);
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int store_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int get_sub_piece(FILE *fp, char *buf, int len, int piece_num, int piece_size);
int store_sub_piece(FILE *fp, char *buf, int len, int piece_num, int piece_size);
char *gen_bitfield(FILE *fp, char *piece_hash, int piece_len, int piece_num);

#endif
