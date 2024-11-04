#ifndef __ETH_SERVER_H
#define __ETH_SERVER_H


struct pc_net_info{
    char net_name[30];
    char net_ip[30];
    int status;
};

int eth_capture_server_init(struct  pc_net_info *info, int port);
int eth_log_server_init(struct  pc_net_info *info, int port);
int eth_playback_server_init(struct  pc_net_info *info, int port);
int playback_server_reconnection(void);
int create_playback_server_data(int socket);
int status_refresh_thread_start(void);
int capture_server_main_loop(void);
int playback_server_wait_connect(void);
void ScanNetworkInterfaces(struct  pc_net_info *net_info);
const char * Interfaces_Status_To_Str(int status);
int playback_server_Send(char *buf, int len);
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))




#endif
