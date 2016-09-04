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
	fd_set master_set, working_set;  //�ļ�����������
	struct timeval timeout;          //select �����еĳ�ʱ�ṹ��
	int proxy_cmd_socket = 0;     //proxy listen��������
	int accept_cmd_socket = 0;     //proxy accept�ͻ�������Ŀ�������
	int connect_cmd_socket = 0;     //proxy connect������������������
	int proxy_data_socket = 0;     //proxy listen��������
	int accept_data_socket = 0;     //proxy accept�õ�������������ӣ�����ģʽʱaccept�õ��������������ӵ����󣬱���ģʽʱaccept�õ��ͻ����������ӵ�����
	int connect_data_socket = 0;     //proxy connect������������ ������ģʽʱconnect�ͻ��˽����������ӣ�����ģʽʱconnect�������˽����������ӣ�
	int selectResult = 0;     //select��������ֵ
	int select_sd = 20;    //select ��������������ļ�������

	int transfer_mode;
	int transfer_port;
	int is_upload_or_download;
	int do_cache;

	FD_ZERO(&master_set);   //���master_set����
	bzero(&timeout, sizeof(timeout));

	proxy_cmd_socket = bindAndListenSocket(21);  //����proxy_cmd_socket��bind������listen����

	FD_SET(proxy_cmd_socket, &master_set);  //��proxy_cmd_socket����master_set����

	timeout.tv_sec = 1000;    //Select�ĳ�ʱ����ʱ��
	timeout.tv_usec = 0;    //ms

	while (TRUE) {
		FD_ZERO(&working_set); //���working_set�ļ�����������
		memcpy(&working_set, &master_set, sizeof(master_set)); //��master_set����copy��working_set����

															   //selectѭ������ ����ֻ�Զ������ı仯���м�����working_setΪ���Ӷ������������������ļ��ϣ�,�����͵��ĸ�������NULL������д����������������м���
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

		// selectResult > 0 ʱ ����ѭ���ж��б仯���ļ�������Ϊ�ĸ�socket
		int i;
		for (i = 0; i < select_sd; i++) {
			//�жϱ仯���ļ��������Ƿ������working_set����
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
					accept_cmd_socket = acceptSocket(proxy_cmd_socket);  //ִ��accept����,����proxy�Ϳͻ���֮��Ŀ�������
					connect_cmd_socket = connectTo(SERVER_ADDR, 21); //ִ��connect����,����proxy�ͷ�������֮��Ŀ�������

																	 //���µõ���socket���뵽master_set�����
					FD_SET(accept_cmd_socket, &master_set);
					FD_SET(connect_cmd_socket, &master_set);
				}

				if (i == accept_cmd_socket) {
					printf("enter i == accept_cmd_socket\n");
					char buff[BUFFSIZE];
					int read_len;
					if ((read_len = read(i, buff, BUFFSIZE - 1)) == 0) {
						close(i); //������ղ�������,��ر�Socket
						close(connect_cmd_socket);

						//socket�رպ�ʹ��FD_CLR���رյ�socket��master_set��������ȥ,ʹ��select�������ټ����رյ�socket
						FD_CLR(i, &master_set);
						FD_CLR(connect_cmd_socket, &master_set);
						printf("close cmd1\n");
					}
					else {
						//������յ�����,������ݽ��б�Ҫ�Ĵ���֮���͸��������ˣ�д��connect_cmd_socket��

						//����ͻ��˷���proxy��request������������Ҫ���д�����PORT��RETR��STOR                        
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
							FD_SET(proxy_data_socket, &master_set);//��proxy_data_socket����master_set����
						}

						//RETR
						if (strncmp(buff, "RETR", 4) == 0) {
							is_upload_or_download = DOWNLOAD;
						}
						//STOR
						if (strncmp(buff, "STOR", 4) == 0) {
							is_upload_or_download = UPLOAD;
						}

						//д��proxy��server������cmd����,����PORT֮�⣬ֱ��ת��buff����
						write(connect_cmd_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("buff:%s (end)\n", buff, read_len);
					}
				}

				if (i == connect_cmd_socket) {
					printf("enter i == connect_cmd_socket\n");
					//����������˷���proxy��reply��д��accept_cmd_socket
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len == 0) {
						close(i); //������ղ�������,��ر�Socket
						close(accept_cmd_socket);

						//socket�رպ�ʹ��FD_CLR���رյ�socket��master_set��������ȥ,ʹ��select�������ټ����رյ�socket
						FD_CLR(i, &master_set);
						FD_CLR(accept_cmd_socket, &master_set);
						printf("close cmd2\n");
					}
					else {
						//PASV�յ��Ķ˿� 227 ��port��
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
							FD_SET(proxy_data_socket, &master_set);//��proxy_data_socket����master_set����
						}
						write(accept_cmd_socket, buff, read_len);
						buff[read_len] = '\0';
						//printf("server reply:%s (end)\n", buff);
					}
					//////////////
				}

				if (i == proxy_data_socket) {
					printf("enter i == proxy_data_socket\n");
					//����data����(accept_data_socket��connect_data_socket)
					if (transfer_mode == ACTIVE) {
						//if upload
						accept_data_socket = acceptSocket(proxy_data_socket);
						connect_data_socket = connectTo(CLIENT_ADDR, transfer_port);

						FD_SET(accept_data_socket, &master_set);
						FD_SET(connect_data_socket, &master_set);
						printf("ACTIVE accept_data:%d\n", accept_data_socket);
						printf("ACTIVE connect_data:%d\n", connect_data_socket);
						//if download
						//�жϻ��� ���û�л��棬�ж��ļ�����
							//����ļ�������Ҫ���� ���������ӣ����������ļ������Ϊ��Ҫ���� do_cache = true
							//����ļ����Ͳ���Ҫ���� ���������� ��uploadһ��
						//����л��棬ֱ�Ӵ������ݣ���������
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
						//�жϻ��� ���û�л��棬�ж��ļ�����
							//����ļ�������Ҫ���� ���������ӣ����������ļ������Ϊ��Ҫ���� do_cache = true
							//����ļ����Ͳ���Ҫ���� ���������� ��uploadһ��
						//����л��棬ֱ�Ӵ������ݣ���������
					}
				}

				if (i == accept_data_socket) {
					printf("enter i == accept_data_socket\n");
					//�ж��������ʹ��䷽ʽ���ϴ������أ�������δ�������
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len != 0) {
						write(connect_data_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("accept_data:%s(end)length:%d\n", buff, read_len);
						//if do_cache = true
						//д���ļ�
					}
					else {
						close(i);
						close(connect_data_socket);
						FD_CLR(accept_data_socket, &master_set);
						FD_CLR(connect_data_socket, &master_set);
						printf("accept_data_socket close()\n");
						//if do_cache = true
						//�ر��ļ�
					}

				}

				if (i == connect_data_socket) {
					printf("enter i == connect_data_socket\n");
					//�ж��������ʹ��䷽ʽ���ϴ������أ�������δ�������
					char buff[BUFFSIZE];
					int read_len;
					read_len = read(i, buff, BUFFSIZE - 1);
					if (read_len != 0) {
						write(accept_data_socket, buff, read_len);
						buff[read_len] = '\0';
						printf("connect_data:%s\n(end)length:%d\n", buff, read_len);
						//if do_cache = true
						//д���ļ�
					}
					else {
						close(i);
						close(accept_data_socket);
						FD_CLR(accept_data_socket, &master_set);
						FD_CLR(connect_data_socket, &master_set);
						printf("connect_data_socket close()\n");
						//if do_cache = true
						//�ر��ļ�
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