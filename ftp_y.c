#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#define SERVER_ADDR "192.168.56.1"
#define CLIENT_ADDR "192.168.56.1"
#define TRUE 1
#define ACTIVE 1
#define PASSIVE 2
#define BUFFSIZE 1024
#define UPLOAD 1
#define DOWNLOAD 2

int bindAndListenSocket(int port);
int connectToCmd();
int acceptSocket(int servSock);
int connectToData(int port);
int main(int argc, const char *argv[])
{
	fd_set master_set, working_set;  //文件描述符集合
	struct timeval timeout;          //select 参数中的超时结构体
	int proxy_cmd_socket = 0;     //proxy listen控制连接
	int accept_cmd_socket = 0;     //proxy accept客户端请求的控制连接
	int connect_cmd_socket = 0;     //proxy connect服务器建立控制连接
	int proxy_data_socket = 0;     //proxy listen数据连接
	int accept_data_socket = 0;     //proxy accept得到请求的数据连接（主动模式时accept得到服务器数据连接的请求，被动模式时accept得到客户端数据连接的请求）
	int connect_data_socket = 0;     //proxy connect建立数据连接 （主动模式时connect客户端建立数据连接，被动模式时connect服务器端建立数据连接）
	int selectResult = 0;     //select函数返回值
	int select_sd = 20;    //select 函数监听的最大文件描述符

	int transfer_mode;
	int transfer_port;
	int is_upload_or_download;
	int do_cache;

	FD_ZERO(&master_set);   //清空master_set集合
	bzero(&timeout, sizeof(timeout));

	proxy_cmd_socket = bindAndListenSocket(21);  //开启proxy_cmd_socket、bind（）、listen操作

	FD_SET(proxy_cmd_socket, &master_set);  //将proxy_cmd_socket加入master_set集合

	timeout.tv_sec = 1000;    //Select的超时结束时间
	timeout.tv_usec = 0;    //ms

	while (TRUE) {
		FD_ZERO(&working_set); //清空working_set文件描述符集合
		memcpy(&working_set, &master_set, sizeof(master_set)); //将master_set集合copy到working_set集合

															   //select循环监听 这里只对读操作的变化进行监听（working_set为监视读操作描述符所建立的集合）,第三和第四个参数的NULL代表不对写操作、和误操作进行监听
		selectResult = select(select_sd, &working_set, NULL, NULL, &timeout);

		// fail
		if (selectResult < 0) {
			perror("select() failed\n");
			exit(0);
		}

		// timeout
		if (selectResult == 0) {
			printf("select() timed out.\n");
			continue;
		}

		// selectResult > 0 时 开启循环判断有变化的文件描述符为哪个socket
		int i;
		for (i = 0; i < select_sd; i++) {
			//判断变化的文件描述符是否存在于working_set集合
			if (FD_ISSET(i, &working_set)) {
				if (i == proxy_cmd_socket) {
					printf("enter i == proxy_cmd_socket\n");
					if (FD_ISSET(accept_cmd_socket, &master_set)) {
						close(accept_cmd_socket);
						FD_CLR(accept_cmd_socket, &master_set);
					}
					if (FD_ISSET(connect_cmd_socket, &master_set)) {
						close(connect_cmd_socket);
						FD_CLR(connect_cmd_socket, &master_set);
					}
					accept_cmd_socket = acceptSocket(proxy_cmd_socket);  //执行accept操作,建立proxy和客户端之间的控制连接
					connect_cmd_socket = connectTo(SERVER_ADDR, 21); //执行connect操作,建立proxy和服务器端之间的控制连接

																	 //将新得到的socket加入到master_set结合中
					FD_SET(accept_cmd_socket, &master_set);
					FD_SET(connect_cmd_socket, &master_set);
				}

				if (i == accept_cmd_socket) {
					printf("enter i == accept_cmd_socket\n");
					char buff[BUFFSIZE];
					int read_len;
					if ((read_len = read(i, buff, BUFFSIZE - 1)) == 0) {
						close(i); //如果接收不到内容,则关闭Socket
						close(connect_cmd_socket);

						//socket关闭后，使用FD_CLR将关闭的socket从master_set集合中移去,使得select函数不再监听关闭的socket
						FD_CLR(i, &master_set);
						FD_CLR(connect_cmd_socket, &master_set);
						printf("close cmd1\n");
					}
					else {
						//如果接收到内容,则对内容进行必要的处理，之后发送给服务器端（写入connect_cmd_socket）

						//处理客户端发给proxy的request，部分命令需要进行处理，如PORT、RETR、STOR                        
						//PORT
						if (strncmp(buff, "PORT", 4) == 0) {
							char content[BUFFSIZE]; //gai
							strcpy(content, buff + 5);

							char *temp; //gai
							int p1, p2;
							strtok(buff, ",");
							strtok(NULL, ",");
							strtok(NULL, ",");
							strtok(NULL, ",");
							temp = strtok(NULL, ",");
							p1 = atoi(temp);
							temp = strtok(NULL, ",");
							p2 = atoi(temp);

							sprintf(buff, "PORT 192,168,56,101,%d,%d\r\n", p1, p2);
							read_len = strlen(buff);
							transfer_mode = ACTIVE;
							transfer_port = p1 * 256 + p2;
							printf("PORT port:%d p1:%d p2:%d\n", transfer_port, p1, p2);

							if (FD_ISSET(proxy_data_socket, &master_set)) {
								close(proxy_data_socket);
								FD_CLR(proxy_data_socket, &master_set);
							}
							proxy_data_socket = bindAndListenSocket(transfer_port);
							FD_SET(proxy_data_socket, &master_set);//将proxy_data_socket加入master_set集合
						}

						//RETR
						if (strncmp(buff, "RETR", 4) == 0) {
							is_upload_or_download = DOWNLOAD;
						}
						//STOR
						if (strncmp(buff, "STOR", 4) == 0) {
							is_upload_or_download = UPLOAD;
						}

						//写入proxy与server建立的cmd连接,除了PORT之外，直接转发buff内容
						write(connect_cmd_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("buff:%s (end)\n", buff, read_len);
					}
				}

				if (i == connect_cmd_socket) {
					printf("enter i == connect_cmd_socket\n");
					//处理服务器端发给proxy的reply，写入accept_cmd_socket
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len == 0) {
						close(i); //如果接收不到内容,则关闭Socket
						close(accept_cmd_socket);

						//socket关闭后，使用FD_CLR将关闭的socket从master_set集合中移去,使得select函数不再监听关闭的socket
						FD_CLR(i, &master_set);
						FD_CLR(accept_cmd_socket, &master_set);
						printf("close cmd2\n");
					}
					else {
						//PASV收到的端口 227 （port）
						if (strncmp(buff, "227", 3) == 0) {
							char content[BUFFSIZE]; //gai
							strcpy(content, buff + 5);

							char *temp; //gai
							int p1, p2;
							strtok(buff, ",");
							strtok(NULL, ",");
							strtok(NULL, ",");
							strtok(NULL, ",");
							temp = strtok(NULL, ",");
							p1 = atoi(temp);
							temp = strtok(NULL, ",");
							p2 = atoi(temp);

							sprintf(buff, "227 Entering Passive Mode (192,168,56,101,%d,%d)\r\n", p1, p2);
							read_len = strlen(buff);
							transfer_mode = PASSIVE;
							transfer_port = p1 * 256 + p2;
							printf("PASV port:%d\n", transfer_port);

							if (FD_ISSET(proxy_data_socket, &master_set)) {
								close(proxy_data_socket);
								FD_CLR(proxy_data_socket, &master_set);
							}
							proxy_data_socket = bindAndListenSocket(transfer_port);
							FD_SET(proxy_data_socket, &master_set);//将proxy_data_socket加入master_set集合
						}
						write(accept_cmd_socket, buff, read_len);
						buff[read_len] = '\0';
						//printf("server reply:%s (end)\n", buff);
					}
					//////////////
				}

				if (i == proxy_data_socket) {
					printf("enter i == proxy_data_socket\n");
					//建立data连接(accept_data_socket、connect_data_socket)
					if (transfer_mode == ACTIVE) {
						//if upload
						accept_data_socket = acceptSocket(proxy_data_socket);
						connect_data_socket = connectTo(CLIENT_ADDR, transfer_port);

						FD_SET(accept_data_socket, &master_set);
						FD_SET(connect_data_socket, &master_set);
						printf("ACTIVE accept_data:%d\n", accept_data_socket);
						printf("ACTIVE connect_data:%d\n", connect_data_socket);
						//if download
						//判断缓存 如果没有缓存，判断文件类型
							//如果文件类型需要缓存 打开数据连接，创建本地文件，标记为需要缓存 do_cache = true
							//如果文件类型不需要缓存 打开数据连接 跟upload一样
						//如果有缓存，直接传输数据，不打开连接
					}
					if (transfer_mode == PASSIVE) {
						//upload
						accept_data_socket = acceptSocket(proxy_data_socket);
						connect_data_socket = connectTo(SERVER_ADDR, transfer_port);

						FD_SET(accept_data_socket, &master_set);
						FD_SET(connect_data_socket, &master_set);
						printf("PASSIVE accept_data:%d\n", accept_data_socket);
						printf("PASSIVE connect_data:%d\n", connect_data_socket);
						//if download
						//判断缓存 如果没有缓存，判断文件类型
							//如果文件类型需要缓存 打开数据连接，创建本地文件，标记为需要缓存 do_cache = true
							//如果文件类型不需要缓存 打开数据连接 跟upload一样
						//如果有缓存，直接传输数据，不打开连接
					}
				}

				if (i == accept_data_socket) {
					printf("enter i == accept_data_socket\n");
					//判断主被动和传输方式（上传、下载）决定如何传输数据
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len != 0) {
						write(connect_data_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("accept_data:%s(end)length:%d\n", buff, read_len);
						//if do_cache = true
						//写入文件
					}
					else {
						close(i);
						close(connect_data_socket);
						FD_CLR(accept_data_socket, &master_set);
						FD_CLR(connect_data_socket, &master_set);
						printf("accept_data_socket close()\n");
						//if do_cache = true
						//关闭文件
					}

				}

				if (i == connect_data_socket) {
					printf("enter i == connect_data_socket\n");
					//判断主被动和传输方式（上传、下载）决定如何传输数据
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len != 0) {
						write(accept_data_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("connect_data:%s\n(end)length:%d\n", buff, read_len);
						//if do_cache = true
						//写入文件
					}
					else {
						close(i);
						close(accept_data_socket);
						FD_CLR(accept_data_socket, &master_set);
						FD_CLR(connect_data_socket, &master_set);
						printf("connect_data_socket close()\n");
						//if do_cache = true
						//关闭文件
					}
				}
			}
		}
	}

	return 0;
}

int bindAndListenSocket(int port) {
	int sock;
	struct sockaddr_in servAddr;

	int maxConnection = 10;
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		printf("socket() failed.\n");
		exit(0);
	}

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	if ((bind(sock, (struct sockaddr *) &servAddr, sizeof(servAddr))) < 0) {
		printf("bind() failed.%d\n", port);
		exit(0);
	}

	if (listen(sock, maxConnection) < 0) {
		printf("listen error.\n");
		exit(0);
	}

	return sock;
}

int acceptSocket(int servSock) {
	int sock;
	struct sockaddr_in addr;
	int addr_len;
	addr_len = sizeof(addr);
	sock = accept(servSock, (struct sockaddr *)&addr, &addr_len);
	if (sock < 0) {
		perror("accept() failed\n");
		exit(0);
	}
	return sock;
}

int connectTo(char *servAddrIp, int port) {
	int sock;
	int addr_len;

	struct sockaddr_in servAddr;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket() failed\n");
		exit(0);
	}

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	inet_aton(servAddrIp, &servAddr.sin_addr);

	addr_len = sizeof(servAddr);

	if (connect(sock, (struct sockaddr *)&servAddr, sizeof(struct sockaddr)) < 0) {
		printf("connect() failed\n");

	}
	printf("connectTo() socket:%d\n", sock);
	return sock;
}

int file_is_cached(char *filename) {

}