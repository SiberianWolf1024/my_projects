#include <stdio.h>
#include <string.h>
#include "errno.h"
 
/*
* 参数：
* section:  配置文件中的节sectin
* key:      配置项的标识
* filename: ini配置文件路径
*
* 返回值：    找到需要的值返回结果0，否则返回-1
*/
int GetIniKeyString(char *section, char *key, char *filename, char *buf)
{
    FILE *fp;
 
    // 用来标记是否找到section
    int flag = 0;
    char sSection[64], *wTmp;
    char sLine[1024];
 
    // 节section字符串
    sprintf(sSection, "[%s]", section);
 
    if (NULL == (fp = fopen(filename, "r")))
    {
        printf("open %s failed.\n", filename);
        return -1;
    }
 
    // 读取ini中的每一行
    while (NULL != fgets(sLine, 1024, fp))
    {
        // 处理ini文件中的注释行
        if ('#' == sLine[0])
            continue;
 
        if (';' == sLine[0])
            continue;
 
        // 定位=的位置
        wTmp = strchr(sLine, '=');
        if ((NULL != wTmp) && (1 == flag))
        {
            if (0 == strncmp(key, sLine, strlen(key)))
            {
                sLine[strlen(sLine) - 1] = '\0';
 
                while (*(wTmp + 1) == ' ')
                {
                    wTmp++;
                }
 
                // 获取key对应的value
                strcpy(buf, wTmp + 1);
 
                fclose(fp);
                return 0;
            }
        }
        else
        {
            if (0 == strncmp(sSection, sLine, strlen(sSection)))
            {
                // 不存在键值对的情况下，标记flag
                flag = 1;
            }
        }
    }
 
    fclose(fp);
    return -1;
}
 
 
/*
* 参数：
* section:  配置文件中的节sectin
* key:      配置项的标识
* val:      配置项标识对应的值
* filename: ini配置文件路径
*
* 返回值：    成功返回结果0，否则返回-1
*/
int PutIniKeyString(char *section, char *key, char *val, char *filename)
{
    FILE *fpr, *fpw;
    int flag = 0;
    int ret;
    char sLine[1024], sSection[32], *wTmp;
 
    sprintf(sSection, "[%s]", section);
 
    if (NULL == (fpr = fopen(filename, "r")))
        return -1;
 
    // 临时文件名
    sprintf(sLine, "%s.tmp", filename);
 
    fpw = fopen(sLine, "w");
    if (NULL == fpw)
        return -1;
 
    while (NULL != fgets(sLine, 1024, fpr))
    {
        if (2 != flag)
        {
            wTmp = strchr(sLine, '=');
            if ((NULL != wTmp) && (1 == flag))
            {
                if (0 == strncmp(key, sLine, strlen(key)))
                {
                    // 找到对应的key
                    flag = 2;
                    sprintf(wTmp + 1, " %s\n", val);
                }
            }
            else
            {
                if (0 == strncmp(sSection, sLine, strlen(sSection)))
                {
                    // 找到section的位置
                    flag = 1;
                }
            }
        }
 
        // 写入临时文件
        fputs(sLine, fpw);
    }
 
    fclose(fpr);
    fclose(fpw);
 
    sprintf(sLine, "%s.tmp", filename);
 
    // rename函数在windows上和linux上表现有差异，看文章中的备注
    ret = rename(sLine, filename);
    if (ret != 0)
    {
        if (errno == EEXIST)
        {
            // 如果目标文件已经存在，需要先删除，再重命名
            if (remove(filename) == 0)
            {
                if (rename(sLine, filename) == 0)
                {
                    // printf("File %s has been renamed to %s\n", sLine, filename);
                    return 0;
                }
            }
        }
    }
 
    return ret;
}