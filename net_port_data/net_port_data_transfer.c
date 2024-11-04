#include <stdio.h>
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>
#include <pthread.h>
#include "ini_parser.h"
#include "eth_server.h"

#define SoftVersion  "V1.1"
#define SoftCommit   "卫惯算法数据回放软件-电脑版本(以太网)" \
					 "\n增加PBOX断开时的重连机制"

#define IMU_FLAG_INDEX  1
#define GNSS_FLAG_INDEX  2
#define SSR_FLAG_INDEX  3

#define ERR_NONE            (0x0)        //无错误
#define ERR_HEAD_FIELDS		(0x1 << 1)   //标志位非法
#define ERR_FLAG_FIELDS		(0x1 << 2)   //标志位非法
#define ERR_LEN_FIELDS		(0x1 << 3)  //长度非法
#define ERR_INDEX_FIELDS	(0x1 << 4)  //索引非法
#define ERR_FILE_END		(0x1 << 5)  //文件结束

typedef struct  {
	char adapter_name[30]; //网卡
    char com_port[10];
    char file_name[100];//回放文件
	int port;

	int interval; //间隔
	int fastforward; //快进
}ConfigParam_Def;

#define HEAD_BYTE0  0xAA  //头部字节1
#define HEAD_BYTE1  0x55  //头部字节2
#define HEAD_WORD   0x55AA //小端字节的头部
#define  FLAG_CHECK(flag)  ((flag == IMU_FLAG_INDEX) ||  (flag == GNSS_FLAG_INDEX) ||  (flag == SSR_FLAG_INDEX))

#pragma pack(push, 1)
struct ProDataBlock {
	uint16_t  head;    //头数据
	uint16_t  len;    //表示成员 data 的字节数
	uint8_t   flag;   //数据类型
	uint16_t  index;  //数据索引
	uint8_t   data[0];
};
#pragma pack(pop)

typedef struct {
	int imu_frame_cnt;
	int gnss_frame_cnt;
	int ssr_frame_cnt;

	int imu_frame_loss;
	int gnss_frame_loss;
	int ssr_frame_loss;

	uint64_t total_write;
	int error;
}DATA_WR_INFO;


enum PKT_RET_NUM {
	PKT_RET_SUCCEED,
	PKT_RET_HEAD_FAIL,
	PKT_RET_LEN_FAIL,
	PKT_RET_ALLOC_FAIL,
	PKT_RET_DATA_FAIL,
	
};

ConfigParam_Def glo_configs = {
	.adapter_name = "有线网络",
    .file_name = "SsrImuGnss.log",
	.interval = 10,  //10ms
	.port = 5234,
};
static uint64_t write_total_len = 0;  //总共发送字节数
static uint64_t frame_total_len = 0;  //总共可发的帧数
static uint64_t frame_total_len_static = 0; //初始化时获取的总可发帧数 
static DATA_WR_INFO g_data_wr_info;
static long g_file_offset = 0; //文件偏移 
static struct  pc_net_info cur_net_info[2];

int ProLoadPktInFile(FILE* file, struct ProDataBlock** blk)
{
	uint16_t head;
	uint16_t  len;
	size_t bSize;
	struct ProDataBlock* block;
	uint8_t *point;
	int i;
	static int cnt = 0;

    // 读取前2字节的 head 值
    if (fread(&head, sizeof(uint16_t), 1, file) != 1) {
	//	printf("文件读取头部错误.\n");
    //   fclose(file);
        return PKT_RET_HEAD_FAIL;
    }
	
	
    // 读取前2字节的 len 值
    if (fread(&len, sizeof(uint16_t), 1, file) != 1) {
    //    printf("Failed to read len value.\n");
    //   fclose(file);
        return PKT_RET_LEN_FAIL;
    }
	
    // 计算数据块的大小，并分配内存
    bSize = sizeof(struct ProDataBlock) + len;
	//printf("bSize = %d \n",bSize);
    block = (struct ProDataBlock*)malloc(bSize);
    if (block == NULL) {
        printf("Memory allocation failed.\n");
    //    fclose(file);
        return PKT_RET_ALLOC_FAIL;
    }
	
	// 填充结构体的 head, len 字段
	block->head = head;
	block->len = len;

    // 读取后续数据段
    if (fread(&(block->flag), sizeof(uint8_t), bSize - 4, file) != (bSize - 4)) {
        printf("Failed to read data segment.\n");
    //    fclose(file);
        free(block);
        return PKT_RET_DATA_FAIL;
    }
	
	*blk = block;
	
	cnt++;
		
	return PKT_RET_SUCCEED;
}



int DataHeadCheck(struct ProDataBlock* block)
{
	if(block->head != HEAD_WORD) {
		return  ERR_HEAD_FIELDS;//头非法
	}
	
	if(!FLAG_CHECK(block->flag)) {
		return  ERR_FLAG_FIELDS;//数据标志位非法
	}

	if(block->len > 8192 || block->len < 56) {
		return  ERR_LEN_FIELDS;//数据标志位非法
	}

	return ERR_NONE;
}


//文件数据读取 -- 出错不暂停
uint32_t ProLoadPktInFileNop(FILE* file, struct ProDataBlock** blk)
{
	int ret = 0;
	struct ProDataBlock *block, tmp_blk;
	size_t bSize;//整包大小

	fseek(file, g_file_offset, SEEK_SET);/* 设置文件偏移 */

	while(1) {
		
		if (fread(&tmp_blk, sizeof(tmp_blk), 1, file) != 1) {//尝试读取数据头部数据
			ret = ERR_FILE_END; //文件已经结束
			break;
		}

		if(DataHeadCheck(&tmp_blk) != 0) { /* 检查头部是否合法 */
			g_file_offset++;
			fseek(file, g_file_offset, SEEK_SET);/* 设置文件偏移 */
			continue;
		}

		// 计算数据块的大小，并分配内存
		bSize = sizeof(struct ProDataBlock) + tmp_blk.len;
		//printf("bSize = %d \n",bSize);
		block = (struct ProDataBlock*)malloc(bSize);
		if (block == NULL) {
			printf("Memory allocation failed.\n");
			break;
    	}

		*block = tmp_blk;//拷贝头部到堆中

		// 读取后续数据段
		if (fread(block->data, sizeof(uint8_t), block->len, file) != block->len) {
			free(block);
			ret = ERR_FILE_END; //文件已经结束
			break;
		}

		*blk = block; //将block地址返回
		g_file_offset += bSize; //跳过当前包
		
		return PKT_RET_SUCCEED;
	}

	return ret;
}

uint16_t GetLostCnt(uint16_t last, uint16_t current)
{
	uint16_t lostcnt = 0;

	if (current >= last) {
		lostcnt = current - last - 1;
	} else {
		// 考虑到溢出，需要加上溢出的周期
		lostcnt = (65535 - last) + current;
	}
	return lostcnt;
}


//文件数据检查
int FileDataCheckAndVerify(FILE* file)
{
	struct ProDataBlock* block;
	uint16_t last_index[4] = {0}; //数据索引检查 -- 用于检测丢包
	uint32_t error_flag;
	
	while(ProLoadPktInFileNop(file, &block) == PKT_RET_SUCCEED) {	
		error_flag = 0;

		if(block->flag == IMU_FLAG_INDEX) {
			if(g_data_wr_info.imu_frame_cnt > 0)
				g_data_wr_info.imu_frame_loss += GetLostCnt(last_index[block->flag], block->index);
			
			g_data_wr_info.imu_frame_cnt++;	

		}else if(block->flag == GNSS_FLAG_INDEX) {
			if(g_data_wr_info.gnss_frame_cnt > 0)
				g_data_wr_info.gnss_frame_loss += GetLostCnt(last_index[block->flag], block->index);

			g_data_wr_info.gnss_frame_cnt++;

		}else if(block->flag == SSR_FLAG_INDEX) {
			if(g_data_wr_info.ssr_frame_cnt > 0)
				g_data_wr_info.ssr_frame_loss += GetLostCnt(last_index[block->flag], block->index);			
			g_data_wr_info.ssr_frame_cnt++;
		
		}
		
		last_index[block->flag] = block->index; //存储当前帧的索引，与下一帧比较，判断是否丢包
		free(block);
		
	}
	
	printf("可用数据帧数  IMU: %7u  GNSS: %7u  SSR: %7u\n", g_data_wr_info.imu_frame_cnt, g_data_wr_info.gnss_frame_cnt, 
		g_data_wr_info.ssr_frame_cnt);
	printf("丢失数据帧数  IMU: %7u  GNSS: %7u  SSR: %7u\n", g_data_wr_info.imu_frame_loss, g_data_wr_info.gnss_frame_loss, 
		g_data_wr_info.ssr_frame_loss);

	frame_total_len = g_data_wr_info.imu_frame_cnt + g_data_wr_info.gnss_frame_cnt + g_data_wr_info.ssr_frame_cnt;
	frame_total_len_static = frame_total_len;
	if(frame_total_len <= 0)
		return 1;
	
	return 0;
}



HANDLE hEvent; //事件对象

void CALLBACK TimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) {
    // 定时器触发后设置事件对象信号
    SetEvent(hEvent);
}


void dump_cur_net_info(struct  pc_net_info *info, int size)
{
    int i = 0;
    for(i = 0; i < size; i++) {
        if(!strcmp(info[i].net_name, "以太网"))
            printf("有线网络 (%s)  --IP地址 :%s\n",Interfaces_Status_To_Str(info[i].status),info[i].net_ip);
        else if(!strcmp(info[i].net_name, "WLAN"))
            printf("无线网络 (%s)  --IP地址 :%s\n",Interfaces_Status_To_Str(info[i].status),info[i].net_ip); 
    }
}

struct  pc_net_info * find_the_right_network(struct  pc_net_info *info, int size)
{
    int i = 0;
    char eth_name[30] = {0};
    
    if(!strcmp(glo_configs.adapter_name, "有线网络"))
        strcpy(eth_name, "以太网");
    else if(!strcmp(glo_configs.adapter_name, "无线网络"))
        strcpy(eth_name, "WLAN");
    else {
        printf("非法网络参数: %s \n", glo_configs.adapter_name);
        return (struct  pc_net_info *)NULL;
    }

    for(i = 0; i < size; i++) {
        if(!strcmp(info[i].net_name, eth_name)) {
            break;
        }
    }

    if(i == size) {
       printf("无法查找到所需网络: %s \n", glo_configs.adapter_name); 
       return (struct  pc_net_info *)NULL; 
    }

    return (info + i);
}


int init_config(void) 
{
    char buff[128];
    int ret;
 
    memset(buff, 0, sizeof(buff));
    ret = GetIniKeyString("database", "file_name", "config.ini", buff);
    if(ret == 0) {
        strcpy(glo_configs.file_name, buff);
    }

    memset(buff, 0, sizeof(buff));
    ret = GetIniKeyString("database", "net", "config.ini", buff);
    if(ret == 0) {
        strcpy(glo_configs.adapter_name, buff);
    }
	
    memset(buff, 0, sizeof(buff));
    ret = GetIniKeyString("database", "port", "config.ini", buff);
    if(ret == 0) {
        glo_configs.port = atoi(buff);
    }

    memset(buff, 0, sizeof(buff));
    ret = GetIniKeyString("database", "interval", "config.ini", buff);
    if(ret == 0) {
        glo_configs.interval = atoi(buff);
    }

    memset(buff, 0, sizeof(buff));
    ret = GetIniKeyString("database", "fastforward", "config.ini", buff);
    if(ret == 0) {
        glo_configs.fastforward = atoi(buff);
    }
	

    return 0;
}

void moveCursorUp(int lines) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD newCoord;

    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        newCoord.X = csbi.dwCursorPosition.X;
        newCoord.Y = csbi.dwCursorPosition.Y - lines;

        // Make sure the new Y-coordinate does not go below 0
        if (newCoord.Y < 0) {
            newCoord.Y = 0;
        }

        SetConsoleCursorPosition(hOut, newCoord);
    }
}

/**
 * @brief 打印发送的状态
 * 
 * @param status 
 */
void print_status_of_send(int status)
{
	int remain_time = 0;
	static uint64_t last_write_len = 0; //上一秒总发送字节数
	uint64_t speed; //速度
	int send_alread = 0;

	if( status == 0 ) {
		speed = write_total_len - last_write_len;//计算秒字节速率
		remain_time = frame_total_len * glo_configs.interval / 1000;
		send_alread = (frame_total_len_static - frame_total_len) * glo_configs.interval / 1000;

		int minutes_predict = remain_time / 60; // 计算预计消耗分钟数
		int seconds_predict = remain_time % 60; // 计算预计消耗秒数
		int minutes_already = send_alread / 60; // 计算已发送分钟数
		int seconds_already = send_alread % 60; // 计算已发送的秒数

		printf("剩余%llu帧未发, 预计 %d分钟%d秒 后回放完成!       \n", frame_total_len, minutes_predict, seconds_predict);
		printf("已发送: %llu字节, 已发送用时: %d分钟%d秒, 当前速度 : %llu/s                       \n",
			   write_total_len, minutes_already, seconds_already, speed);
		last_write_len = write_total_len;
	}
	else if (status == 404)
	{
		printf("剩余 %llu 帧未发!!                                    \n", frame_total_len);
		printf("连接断开, 等待PBOX重连...                             \n");
	}
}

// 定义线程函数
void* status_thread_function(void* arg) 
{
	int pr_cnt = 2;
	int *status = (int *)arg;

	//初始化留空一些行
	Sleep(1000);
	for(int i = 0; i < pr_cnt; i++)
		printf("\n");
	
	while(1) {
		print_status_of_send(*status);
		moveCursorUp(pr_cnt);
		
		/* 发送完毕则退出 */
		if(frame_total_len == 0)
			break;
		Sleep(1000);	
	}
	
	//更新状态到完成时
	print_status_of_send(*status);
	printf("发送已经完成\n");

}

/**
 * @brief 简单的对文件进行扫描
 * 
 * @param name 
 * @return int 
 */
int scan_for_playback_file(const char *name)
{
	FILE* file;
    // 打开文件
    file = fopen(glo_configs.file_name, "rb");
    if (file == NULL) {
        printf("Failed to open the file :%s .\n", glo_configs.file_name);
		getchar();
        return 1;
    }

	if(FileDataCheckAndVerify(file)) {
		fclose(file);
		printf("文件中的数据不可靠，禁止发送!!!\n");
		return 0;	
	}
	fclose(file);// 关闭文件
	return 0;
}

int main() 
{
    FILE* file;
    DWORD bytesRead;
    DWORD bytesWritten;
    int interval = 10;
	struct ProDataBlock* block;
	UINT timerID;
	pthread_t thread;
	int result;
	struct  pc_net_info * info;
	int socket;
	int status;

	hEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // 创建事件对象
    // 设置控制台输出的字符编码为 GBK
    SetConsoleOutputCP(CP_UTF8);

    printf("\n\r%s \n", SoftCommit);
    printf("程序版本: %s 构建时间 : %s %s \n\n", SoftVersion, __DATE__,__TIME__);

	//加载配置文件
	init_config();

    //初始化-服务器
    memset(cur_net_info, 0, sizeof(&cur_net_info));
    ScanNetworkInterfaces(cur_net_info);
    dump_cur_net_info(cur_net_info, ARRAY_SIZE(cur_net_info));
    printf("使用 [%s] 与PBOX建立连接\n", glo_configs.adapter_name);
    printf("\n");
	//查找适用的网卡设备
    info = find_the_right_network(cur_net_info, ARRAY_SIZE(cur_net_info));
    if(info == NULL) {
        getchar();
        return 0;
    }
	/**
	 * @brief 扫描回放的文件
	 * 
	 */
	scan_for_playback_file(glo_configs.file_name);
	printf("当前被回放的文件: %s \n", glo_configs.file_name);
	printf("数据帧发送间隔:%d 毫秒 !!\n\n",  glo_configs.interval);

    //创建以太网服务器套接字
	socket = eth_playback_server_init(info, glo_configs.port);
	if(socket == -1) {
		getchar();
		return 0;
	}
	/* 创建服务器数据块 */
	create_playback_server_data(socket);
	/* 等待客户端接入 */
	playback_server_wait_connect();

	status = 0;
	//创建状态线程 - 更新显示的数据和状态
	result = pthread_create(&thread, NULL, status_thread_function, &status);
	if (result) {
		printf("Error creating thread  %d\n", result);
		exit(EXIT_FAILURE);
	}

	file = fopen(glo_configs.file_name, "rb");
	g_file_offset = 0;
	
	//从文件中加载一个新的数据发送给PBOX
	while(ProLoadPktInFileNop(file, &block) == PKT_RET_SUCCEED) {
		//产生一个 interval 间隔的闹钟事件
		timerID = timeSetEvent(glo_configs.interval, 0, TimerCallback, 0, TIME_ONESHOT); 
		// 阻塞直到闹钟事件的产生
		WaitForSingleObject(hEvent, INFINITE);

		/**
		 * @brief 尝试发送数据帧，如果失败，则重新等待客户端接入
		 * 
		 */
        if (playback_server_Send((char *)block, block->len + sizeof(struct ProDataBlock)) <= 0) {
			status = 404; //切换到客户端断开
			playback_server_reconnection();
			status = 0;//网络切换到恢复
			/* 重发失败的数据帧 */
			playback_server_Send((char *)block, block->len + sizeof(struct ProDataBlock));
        }

		write_total_len += block->len + sizeof(struct ProDataBlock);
		frame_total_len--; //帧数量自减
		
		free(block);
	}
	
    // 程序退出前停止定时器和关闭事件对象
	timeKillEvent(timerID);
	CloseHandle(hEvent);

    // 关闭监听套接字
    closesocket(socket);
    // 清理Winsock
    WSACleanup();

	getchar();
	
    return 0;
}
