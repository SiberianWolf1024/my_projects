//gcc -o eth_server eth_server.c -lws2_32 -liphlpapi -std=c11

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include "eth_server.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/time.h>
#include "my_list.h"

#pragma comment(lib, "iphlpapi.lib")

#define ETH_RECV_BUF_LEN  4096

struct capture_server_data {
    SOCKET Socket; //套接字
    SOCKET clientSocket; //套接字
    int client_cnt;//客户端数量
    int is_connect; //客户端接入状态
    char dir_name[30];
    char log_file_name[30];
    int log_size;
};

static struct capture_server_data *server_data;

/**
 * @brief 处理接入的日志客户端
 * 
 * @param lpParam 
 * @return DWORD 
 */
DWORD WINAPI LogHandleClient(LPVOID lpParam) 
{
	char *buffer;
    SOCKET clientSocket;
    int recv_len;
    FILE* sSaveFile;
    char real_file_name[60] = {0};
    struct capture_server_data *ser_data;
    ser_data = (struct capture_server_data *)lpParam;

    /**
     * @brief create data file
     * 
     */
    sprintf(real_file_name, "%s/%s", ser_data->dir_name, ser_data->log_file_name);
    sSaveFile = fopen(real_file_name, "wb");
    if (sSaveFile  == NULL) {
        perror("Error opening the file.");
        return EXIT_FAILURE;
    }

    buffer = malloc(ETH_RECV_BUF_LEN);
    if(buffer == NULL) {
        perror("Buff alloc faild.");
        return E_ABORT;
    }

	ser_data->log_size = 0;
	clientSocket = ser_data->clientSocket;

	while(1) {
		recv_len = recv(clientSocket, buffer, ETH_RECV_BUF_LEN, 0);
        if(recv_len > 0){
            fwrite(buffer, sizeof(char),  recv_len, sSaveFile);
            fflush(sSaveFile);
            ser_data->log_size += recv_len;
        }
        else if (recv_len == 0) { /* 连接已断开 */
            //更新客户端套接字，因为客户端可能重新连接
            Sleep(20);
            clientSocket = ser_data->clientSocket;
        }else {
            //更新客户端套接字，因为客户端可能重新连接
            Sleep(20);
            clientSocket = ser_data->clientSocket;
        }
	}

    //下面程序基本上不执行
    fclose(sSaveFile);
    // 关闭客户端套接字
    closesocket(clientSocket);
    free(buffer);

}

const char * Interfaces_Status_To_Str(int status)
{
    if(status == IfOperStatusUp)
        return "正常";
    else 
        return "异常"; 
}

void ScanNetworkInterfaces(struct  pc_net_info *net_info) 
{
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    ULONG outBufLen = 0;
    ULONG retVal = 0;
    PIP_ADAPTER_ADDRESSES pCurrAddresses;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast;
    char *ip_addr;

    // 调用 GetAdaptersAddresses 函数来获取适配器信息
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW) {
        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
        if (pAddresses == NULL) {
            perror("Memory allocation failed");
            exit(1);
        }
    }

    if ((retVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen)) == NO_ERROR) { 

        for (pCurrAddresses = pAddresses; pCurrAddresses != NULL; pCurrAddresses = pCurrAddresses->Next) {
            // 获取适配器名称的长度
            int wideNameLen = wcslen(pCurrAddresses->FriendlyName);

            // 分配用于存储多字节字符串的缓冲区
            int multiByteBufferSize = WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->FriendlyName, wideNameLen, NULL, 0, NULL, NULL);
            char* multiByteFriendlyName = (char*)malloc(multiByteBufferSize + 1);

            // 将宽字符字符串转换为多字节字符串
            WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->FriendlyName, wideNameLen, multiByteFriendlyName, multiByteBufferSize, NULL, NULL);
            multiByteFriendlyName[multiByteBufferSize] = '\0';  // 添加终止符
            
            if(!strcmp(multiByteFriendlyName, "WLAN") || !strcmp(multiByteFriendlyName, "以太网")) {
                
                for (pUnicast = pCurrAddresses->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
                    // 获取IPv4地址
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                        struct sockaddr_in* pAddr = (struct sockaddr_in*)pUnicast->Address.lpSockaddr;
                        ip_addr = inet_ntoa(pAddr->sin_addr);
                      //  printf("  IP Address: %s\n", inet_ntoa(pAddr->sin_addr));
                    }
                }
            }
            if(!strcmp(multiByteFriendlyName, "WLAN")) {
                strcpy(net_info[0].net_name, multiByteFriendlyName);
                strcpy(net_info[0].net_ip, ip_addr);
                net_info[0].status = pCurrAddresses->OperStatus;
            }
            else if(!strcmp(multiByteFriendlyName, "以太网")) {
                strcpy(net_info[1].net_name, multiByteFriendlyName);
                strcpy(net_info[1].net_ip, ip_addr);
                net_info[1].status = pCurrAddresses->OperStatus;
            }

            free(multiByteFriendlyName);
        }
    } else {
        perror("GetAdaptersAddresses failed");
    }

    if (pAddresses != NULL) {
        free(pAddresses);
    }
}

/**
 * @brief 创建日志文件名称
 * 
 * @param file_name 
 */
void systime_to_logfilename(char *file_name)
{
    time_t current_time;
    struct tm* time_info;
    char time_string[80];

    // 获取当前时间
    time(&current_time);

    // 将当前时间转换为本地时间
    time_info = localtime(&current_time);

    memset(time_string, 0, sizeof(time_string));
    // 格式化时间为字符串
    strftime(time_string, sizeof(time_string), "%Y-%m-%d_%H_%M_%S.txt", time_info);

    strcpy(file_name, time_string);
}


int create_playback_server_data(int socket)
{
    /**
     * @brief Create server data blocks
     * 
     */
    server_data = malloc(sizeof(*server_data));
    if(server_data == NULL) {
        printf("Failed to allocate memory server_data \n");
        return -1;
    }
    server_data->Socket = socket;
    server_data->client_cnt = 0;
    server_data->is_connect = 0;

	return 0;
}

void dump_current_time(char *buf)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* 当前时间戳，精确到微秒 */
    printf("%s current millisecond:%ld\n",buf, tv.tv_sec*1000 + tv.tv_usec/1000);  //毫秒
}

void ipaddr_to_dir_name(SOCKET clientSocket, char *dir_name)
{
    int i = 0, len;
	char buf[256];
    int ret;

    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

    strcpy(buf, inet_ntoa(clientAddr.sin_addr));
   
    len = strlen(buf);

    for(i = 0; i < len; i++) {
        if(buf[i] == '.')
            buf[i] = '_';
    }

    strcpy(dir_name, buf);

}

/**
 * @brief 创建文件夹
 * 
 * @param folderName 
 */
void data_folder_creation(const char* folderName) 
{
    // 将char*转换为wchar_t*
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, folderName, -1, NULL, 0);
    wchar_t* wideFolderName = (wchar_t*)malloc(size_needed * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, folderName, -1, wideFolderName, size_needed);

    // 判断文件夹是否存在
    if (GetFileAttributesW(wideFolderName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) {
        // 文件夹不存在，创建文件夹
        if (CreateDirectoryW(wideFolderName, NULL)) {
          //  wprintf(L"Folder '%s' created successfully.\n", wideFolderName);
        } else {
            wprintf(L"Failed to create folder '%s'. Error code: %d\n", wideFolderName, GetLastError());
        }
    } else {
        // 文件夹已存在
       // printf("文件夹 %s 已存在 \n", folderName);
    }

    // 释放内存
    free(wideFolderName);
}

/*
 * 为日志服务器创建套接字
 */
int eth_playback_server_init(struct  pc_net_info *info, int port)
{
    WSADATA wsaData;
    SOCKET listenSocket;
    struct sockaddr_in serverAddr;
	unsigned long serverIPAddress;

    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("WSAStartup failed");
        return -1;
    }

    // 创建套接字
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        perror("socket failed");
        WSACleanup();
        return -1;
    }

    // 配置服务器地址
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverIPAddress = inet_addr(info->net_ip);//指定网卡IP
	serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = serverIPAddress;
    serverAddr.sin_port = htons(port);//指定端口号

    // 绑定套接字
    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("网卡绑定出错，可能原因: \n");
		printf("1. 同时打开多次当前程序\n");
        printf("2. 端口号%d被占用\n", port);
        printf("3. 有线网络网线被拔出\n");
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }

	printf("-----------回放数据服务器-----------\n");
    printf("IP地址: %s\t端口号: %d\n", 
    	inet_ntoa(serverAddr.sin_addr), ntohs(serverAddr.sin_port));    
    printf("------------------------------------\n");

    // 监听连接
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        perror("listen failed");
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }

	return listenSocket;
}



DWORD WINAPI playback_server_main_loop(LPVOID lpParam)
{


}

/**
 * @brief 等待客户端接入
 * 
 * @return int 
 */
int playback_server_wait_connect(void)
{
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int clientAddrLen;
    char dir_name[40] = {0};

    listenSocket = server_data->Socket;
    clientAddrLen = sizeof(clientAddr);

    // 接受客户连接
    clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        perror("accept failed");
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }
    ipaddr_to_dir_name(clientSocket, server_data->dir_name);//获取目录名称
    data_folder_creation(server_data->dir_name);//创建目录
    systime_to_logfilename(server_data->log_file_name); //日志文件名
    server_data->clientSocket = clientSocket;
    printf("算法日志存储于 %s/%s\n",server_data->dir_name, server_data->log_file_name);
    // 创建子线程处理客户端日志
    HANDLE threadHandle = CreateThread(NULL, 0, LogHandleClient, (LPVOID)server_data, 0, NULL);
    if (threadHandle == NULL) {
        perror("CreateThread failed");
        closesocket(clientSocket);
    } else {
        CloseHandle(threadHandle);
    }

    return 0;
}


int playback_server_Send(char *buf, int len)
{
    int ret;
    SOCKET clientSocket;

    clientSocket = server_data->clientSocket;
    ret = send(clientSocket, buf, len, 0);

    return ret;
}

/**
 * @brief 等待客户端PBOX重新连接到服务器
 * 
 * @param buf 
 * @param len 
 * @return int 
 */
int playback_server_reconnection(void)
{
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int clientAddrLen;
    char client_ip_str[40] = {0};

    listenSocket = server_data->Socket;//服务器套接字
    clientAddrLen = sizeof(clientAddr);

    // 接受客户连接
    clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        perror("接受连接失败");
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }
    
    //检查重连的IP地址是否和之前一样,不一样则打印
    ipaddr_to_dir_name(clientSocket, client_ip_str);
    if(strcmp(client_ip_str, server_data->dir_name) != 0) {
        printf("client_ip_str = %s server_data->dir_name = %s \n",client_ip_str, server_data->dir_name);
    }
    /**
     * @brief 更新客户端的套接字
     */
    server_data->clientSocket = clientSocket;

    return 0;
}
