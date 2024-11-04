#ifndef DEMO_INI_PARSER_H
#define DEMO_INI_PARSER_H
 
#include <stdio.h>
#include <string.h>
 
// 获取key对应的值
int GetIniKeyString(char *section,char *key,char *filename,char *buf);
 
// 修改key对应的值
int PutIniKeyString(char *section,char *key,char *val,char *filename);
 
#endif //DEMO_INI_PARSER_H