#include <assert.h>
#include "bencode.h"
#include "util.h"
#include "sha1.h"

char *mystrstr(const char *haystack, const char *needle, int len){
    int needle_len = strlen(needle);
    int i;
    for (i = 0; i <= len - needle_len; i++){
        if(memcmp(haystack + i, needle, needle_len) == 0)
            return (char *)haystack + i;
    }
    return NULL;
}

// 注意: 这个函数只能处理单文件模式torrent
torrentmetadata_t* parsetorrentfile(char* filename)
{
    int i;
    be_node* ben_res;
    FILE* f;
    int flen;
    char* data;
    torrentmetadata_t* ret;
    char endstr[20];
    memset(endstr, 0, 20);

    // 打开文件, 获取数据并解码字符串
    f = fopen(filename,"r");
    flen = file_len(f);
    data = (char*)malloc(flen*sizeof(char));
    fread((void*)data,sizeof(char),flen,f);
    fclose(f);
    ben_res = be_decoden(data,flen);

    // 遍历节点, 检查文件结构并填充相应的字段.
    if(ben_res->type != BE_DICT)
    {
        perror("Torrent file isn't a dictionary");
        exit(-13);
    }

    ret = (torrentmetadata_t*)malloc(sizeof(torrentmetadata_t));
    if(ret == NULL)
    {
        perror("Could not allocate torrent meta data");
        exit(-13);
    }
    // 检查键并提取对应的信息
    int filled=0;
    for(i=0; ben_res->val.d[i].val != NULL; i++)
    {
        int j;
        if(!strcmp(ben_res->val.d[i].key,"announce"))
        {
            ret->announce = (char*)malloc(be_str_len(ben_res->val.d[i].val) + 1);
            memset(ret->announce, 0, be_str_len(ben_res->val.d[i].val) + 1);
            memcpy(ret->announce,ben_res->val.d[i].val->val.s,be_str_len(ben_res->val.d[i].val));
            filled++;
        }
        // info是一个字典, 它还有一些其他我们关心的键
        if(!strcmp(ben_res->val.d[i].key,"info"))
        {
            if (ben_res->val.d[i + 1].val != NULL)
                strcpy(endstr, ben_res->val.d[i + 1].key);
            be_dict* idict;
            if(ben_res->val.d[i].val->type != BE_DICT)
            {
                perror("Expected dict, got something else");
                exit(-3);
            }
            idict = ben_res->val.d[i].val->val.d;
            bool multifile = false;
            for (j = 0; idict[j].key != NULL; j++){
                if (!strcmp(idict[j].key, "files")){
                    multifile = true;
                    filled++;
                }
            }
            // 检查这个字典的键
            for(j=0; idict[j].key != NULL; j++)
            { 
                if(!strcmp(idict[j].key,"length"))
                {
                    ret->length = idict[j].val->val.i;
                    ret->flist[0].size = ret->length;
                    filled++;
                }
                if(!strcmp(idict[j].key,"name"))
                {
                    memset(ret->name, 0, be_str_len(idict[j].val) + 1);
                    memcpy(ret->name,idict[j].val->val.s,be_str_len(idict[j].val));
                    if (multifile == false){
                        ret->filenum = 1;
                        ret->flist[0].begin_index = 0;
                        strcpy(ret->flist[0].filename, ret->name);
                    }
                    filled++;
                }
                if(!strcmp(idict[j].key,"piece length"))
                {
                    ret->piece_len = idict[j].val->val.i;
                    filled++;
                }
                if(!strcmp(idict[j].key,"pieces"))
                {
                    ret->pieces = (char*)malloc(be_str_len(idict[j].val) + 1);
                    memcpy(ret->pieces,idict[j].val->val.s, be_str_len(idict[j].val));
                    filled++;
                }
            } // for循环结束
            for (j = 0; idict[j].key != NULL; j++){
                if (!strcmp(idict[j].key, "files")){
                    assert(idict[j].val->type == BE_LIST);
                    int k;
                    for(k = 0; idict[j].val->val.l[k] != NULL; k++){
                        struct be_dict *filedict = idict[j].val->val.l[k]->val.d;
                        ret->filenum = k + 1;
                        int x;
                        for(x = 0; filedict[x].key != NULL; x++){
                            if (!strcmp(filedict[x].key, "length")){
                                ret->flist[k].size = filedict[x].val->val.i;
                                ret->flist[k].begin_index = (k == 0)?0:ret->flist[k - 1].begin_index + ret->flist[k - 1].size;
                                ret->length = ret->flist[k].size + ret->flist[k].begin_index;
                            }
                            if (!strcmp(filedict[x].key, "path")){
                                strcpy(ret->flist[k].filename, ret->name);
                                struct be_node **pathlist = filedict[x].val->val.l;
                                while(*pathlist != NULL){
                                    strcat(ret->flist[k].filename, "/");
                                    strcat(ret->flist[k].filename, (*pathlist)->val.s);
                                    pathlist++;
                                }
                            }
                        }
                    }
                }
            }
        } // info键处理结束
    }
    int num_pieces = ret->length/ret->piece_len;
    if(ret->length % ret->piece_len != 0)
        num_pieces++;
    ret->num_pieces = num_pieces;


    // 计算这个torrent的info_hash值
    // 注意: SHA1函数返回的哈希值存储在一个整数数组中, 对于小端字节序主机来说,
    // 在与tracker或其他peer返回的哈希值进行比较时, 需要对本地存储的哈希值
    // 进行字节序转换. 当你生成发送给tracker的请求时, 也需要对字节序进行转换.
    char* info_loc, *info_end;
    info_loc = mystrstr(data, "infod", flen);  // 查找info键, 它的值是一个字典
    info_loc += 4; // 将指针指向值开始的地方
    printf("%s\n", data);
    if (strcmp(endstr, "") != 0){
        info_end = mystrstr(data, endstr, flen);
        info_end--;
        while(*info_end != 'e')
            info_end--;
        assert(info_end > info_loc);
    } else {
        info_end = data+flen-1;
        // 去掉结尾的e
        if(*info_end == 'e')
        {
            --info_end;
        }
    }

    int len = info_end - info_loc + 1;

    // 计算上面字符串的SHA1哈希值以获取info_hash
    SHA1Context sha;
    SHA1Reset(&sha);
    SHA1Input(&sha,(const unsigned char*)info_loc,len);
    if(!SHA1Result(&sha))
    {
        printf("FAILURE\n");
    }

    memcpy(ret->info_hash,sha.Message_Digest,20);
    printf("SHA1:\n");
    for(i=0; i<5; i++)
    {
        printf("%08X ",ret->info_hash[i]);
    }
    printf("\n");

    // 确认已填充了必要的字段

    be_free(ben_res);  

    if(filled < 5)
    {
        printf("Did not fill necessary field\n");
        return NULL;
    }
    else
        return ret;
}
