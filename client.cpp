#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <iostream>
#include <dirent.h>
#include <algorithm>

typedef struct{
	int listen_fd;
	char host_name[1024];

} server;
server svr;

int min(int a, int b){
	return (a>b)?b:a;
}

void build_connect(int port, char *ip){
	svr.listen_fd=socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
	if(connect(svr.listen_fd, (struct sockaddr *)&addr, sizeof(addr))<0){
		perror("socket wrong");
	}

}

int main(int argc, char **argv){
	if(argc!=2){
		fprintf(stderr, "wrong argument number\n");
		return 0;
	}
	char ip[100], port[100];
	int idx=0;
	while(idx==0||argv[1][idx]!=':'){
		ip[idx++]=argv[1][idx];
	}
	ip[idx++]='\0';
	for(int a=0;idx<strlen(argv[1]);a++,idx++){
		port[a]=argv[1][idx];
		port[a+1]='\0';
	}
	build_connect(atoi(port), ip);
	mkdir("./client_dir", 0777);
	chdir("./client_dir");

	char name[32], name_buf[1024];
	read(svr.listen_fd, name_buf, 1024);
	char *end = strstr(name_buf, "\n");
	*(end+1)='\0';
	printf("%s", name_buf);
	while(strcmp(name_buf, "connect successfully\n")!=0){
		fgets(name, 32, stdin);
		if(strstr(name," ")!=NULL){
			printf("Command format error\n");
			continue;
		}
		write(svr.listen_fd,name,strlen(name));
		read(svr.listen_fd, name_buf, 1024);
		char *end = strstr(name_buf, "\n");
		*(end+1)='\0';
		printf("%s", name_buf);
	}

	while(1){
		char command[128],file_name[128]="\0", buf[256];
		fgets(buf, 256, stdin);
		*strstr(buf, "\n")='\0';
		char *space=strstr(buf, " ");
		if(space!=NULL){
			*space='\0';
			strcpy(command, buf);
			strcpy(file_name, space+1);
			char *space2=strstr(space+1, " ");
			if(space2!=NULL){
				printf("Command format error\n");
				continue;
			}
			*space=' ';
		}
		else{
			strcpy(command, buf);
		}

		if(strcmp(command, "ls")==0){
			if(strlen(buf)!=2){
				printf("Command format error\n");
				continue;
			}
			char command_buf[2048];
			write(svr.listen_fd, "ls\n", 4);
			int len = read(svr.listen_fd, command_buf, sizeof(command_buf));
			command_buf[len]='\0';
			printf("%s",command_buf);
		}
		else if(strcmp(command, "put")==0){
			int file_fd=open(file_name, O_RDONLY);
			if(file_fd<0){
				printf("The %s doesn’t exist\n",file_name);
				continue;
			}

			write(svr.listen_fd, "put\n", 5);
			char ACK[16];
			read(svr.listen_fd, ACK, 4);
			if(strcmp(ACK, "ACK")!=0){
				fprintf(stderr, "putting error\n");
				return 0;
			}

			char tofile[129];
			sprintf(tofile, "%s\n", file_name);
			write(svr.listen_fd, tofile, strlen(tofile));
			read(svr.listen_fd, ACK, 4);
			if(strcmp(ACK, "ACK")!=0){
				fprintf(stderr, "putting error\n");
				return 0;
			}
			
			long long int filesize=lseek(file_fd, 0, SEEK_END);
			lseek(file_fd, 0, SEEK_SET);
			char tosize[128];
			sprintf(tosize, "%lld\n", filesize);
			write(svr.listen_fd, tosize, strlen(tosize));
			read(svr.listen_fd, ACK, 4);
			if(strcmp(ACK, "ACK")!=0){
				fprintf(stderr, "putting error\n");
				return 0;
			}
			while(filesize>0){
				char file_buf[1100]="\0";
				int now = read(file_fd, file_buf, 1024);
				int sent = send(svr.listen_fd, file_buf, now, 0);
				filesize-=now;
			}

			printf("put %s successfully\n",file_name);
		}
		else if(strcmp(command, "get")==0){
			write(svr.listen_fd, "get\n", 5);
			char ACK[32];
			read(svr.listen_fd, ACK, 4);
			if(strcmp(ACK, "ACK")!=0){
				fprintf(stderr, "getting error\n");
				return 0;
			}
			char tofile[129];
			long long int file_size;
			sprintf(tofile, "%s\n", file_name);
			write(svr.listen_fd, tofile, strlen(tofile));
			read(svr.listen_fd, ACK, 32);
			if(strcmp(ACK, "DNE")==0){
				printf("The %s doesn’t exist\n", file_name);
				continue;
			}
			else{
				char *change=strstr(ACK, "\n");
				*change='\0';
				file_size = atoll(ACK);
			}
			
			int file_fd=open(file_name, O_WRONLY|O_CREAT|O_APPEND, 0777);
			if(file_fd<0){
				fprintf(stderr, "open file get error\n");
				perror("Error : ");
				continue;
			}
			char file_buf[1100];
			while(file_size>0){
				int get = recv(svr.listen_fd, file_buf, 1024, 0);
				int rev = write(file_fd, file_buf, get);
				file_size -= get;
			}
			printf("get %s successfully\n",file_name);
		}
		else{
			printf("Command not found\n");
			continue;
		}
	}
	

	
	return 0;
}
