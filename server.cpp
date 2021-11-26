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
#include <map>
#include <iostream>
#include <dirent.h>
// using namespace std;

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[2048];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    int id;
    int status; // 0 for start 1 get command 2 for putting 3 for getting
    char user_name[128];
    int get_fd;
    int put_fd;
    long long int file_size;
} request;

server svr;
request* requestP = NULL;
int maxfd; 
int incomeFD[100000];

static void init_server(unsigned short port);

static void init_request(request* reqP);

static void free_request(request* reqP);

int handle_read(request* reqP) {
    int r;
    char buf[2048];

    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    if (p1 == NULL) {
       p1 = strstr(buf, "\012");
        if (p1 == NULL) {
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;
    return 1;
}

int main(int argc, char **argv){

    // Parse args.
    if (argc != 2){
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

	char dirname[30] = "./server_dir";
	mkdir(dirname, 0777);
    chdir(dirname);
    std::map<std::string, int> user;

    struct sockaddr_in cliaddr; // used by accept()
    int clilen;
    int conn_fd; // fd for a new connection with client
    int file_fd; // fd for file that we open for reading
    char buf[1024];
    int buf_len;

    // Initialize server
    init_server((unsigned short)atoi(argv[1]));

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    char buffer[1024];
    fd_set ready;

    while(1){
        FD_ZERO(&ready);
        FD_SET(svr.listen_fd, &ready);
        for(int a=3;a<10000;a++)
            if(incomeFD[a])
                FD_SET(a, &ready);

        int co = select(maxfd, &ready, &ready, NULL, NULL);

        if(FD_ISSET(svr.listen_fd, &ready)){
            clilen = sizeof(cliaddr);
            conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
            if (conn_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;  // try again
                if (errno == ENFILE){
                    (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                    continue;
                }
                ERR_EXIT("accept");
            }
            incomeFD[conn_fd] = 1;
            requestP[conn_fd].conn_fd = conn_fd;
            requestP[conn_fd].status = 0;
            strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
            write(requestP[conn_fd].conn_fd, "input your username:\n", 21);
            fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
            continue;
        }

        for(conn_fd=4;conn_fd<=maxfd;conn_fd++){
            if(!FD_ISSET(conn_fd, &ready))
                continue;

            if(requestP[conn_fd].status==0){
                int ret=handle_read(&requestP[conn_fd]);
                if(ret<0){
                    fprintf(stderr, "bad request\n");
                    return 0;
                }

                std::string now = requestP[conn_fd].buf;
                if(user.find(now)==user.end()){
                    user[now] = 1;
                    requestP[conn_fd].status = 1;
                    strcpy(requestP[conn_fd].user_name, now.c_str());
                    char user_dir[256];
                    sprintf(user_dir, "./%s", requestP[conn_fd].user_name);
                    mkdir(user_dir, 0777);
                    write(requestP[conn_fd].conn_fd, "connect successfully\n", 22);
                }
                else{
                    write(requestP[conn_fd].conn_fd, "username is in used, please try another:\n", 42);
                }
                continue;
            }
            else if(requestP[conn_fd].status==1){
                int ret=handle_read(&requestP[conn_fd]);
                if(ret<0){
                    fprintf(stderr, "bad request\n");
                    return 0;
                }
                
                if(strcmp(requestP[conn_fd].buf,"ls")==0){
                    char dir_name[256];
                    sprintf(dir_name, "./%s",requestP[conn_fd].user_name);
                    DIR* userDir = opendir(dir_name);
                    struct dirent* user_file = readdir(userDir);
                    user_file = readdir(userDir);

                    char response[2048]="\0";
                    while((user_file = readdir(userDir))!=NULL){
                        strcat(response, user_file->d_name);
                        strcat(response, "\n");
                    }
                    strcat(response, "\0");
                    write(requestP[conn_fd].conn_fd, response, strlen(response));
                    free(user_file);
                    closedir(userDir);
                    continue;
                }
                else if(strcmp(requestP[conn_fd].buf, "put")==0){
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    if(handle_read(&requestP[conn_fd])<=0){
                        fprintf(stderr, "error getting filename\n");
                        continue;
                    }
                    char file_name[128];
                    strcpy(file_name, requestP[conn_fd].buf);
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    if(handle_read(&requestP[conn_fd])<=0){
                        fprintf(stderr, "error getting filename\n");
                        continue;
                    }
                    requestP[conn_fd].file_size=atoi(requestP[conn_fd].buf);
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    
                    char file_place[512];
                    sprintf(file_place,"./%s/%s",requestP[conn_fd].user_name, file_name);
                    requestP[conn_fd].put_fd = open(file_place, O_RDWR|O_CREAT|O_APPEND, 0666);
                    if(requestP[conn_fd].put_fd<0){
                        fprintf(stderr, "error open file\n");
                        perror("Error: ");
                        continue;
                    }
                    requestP[conn_fd].status = 2;
                    continue;
                }
                else if(strcmp(requestP[conn_fd].buf, "get")==0){
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    if(handle_read(&requestP[conn_fd])<=0){
                        fprintf(stderr, "error getting filename\n");
                        continue;
                    }
                    char file_name[128];
                    strcpy(file_name, requestP[conn_fd].buf);
                    char file_place[512];
                    sprintf(file_place,"./%s/%s",requestP[conn_fd].user_name, file_name);
                    requestP[conn_fd].get_fd = open(file_place, O_RDONLY, 0666);
                    if(requestP[conn_fd].get_fd<0){
                        write(requestP[conn_fd].conn_fd, "DNE", 4);
                        continue;
                    }
                    else{
                        long long int file_size=lseek(requestP[conn_fd].get_fd, 0, SEEK_END);
                        lseek(requestP[conn_fd].get_fd, 0, SEEK_SET);
                        requestP[conn_fd].file_size = file_size;
                        char size[32];
                        sprintf(size, "%lld\n", file_size);                      
                        write(requestP[conn_fd].conn_fd, size, strlen(size));
                    }
                    requestP[conn_fd].status = 3;
                    continue;
                }
                else{
                    write(requestP[conn_fd].conn_fd, "Command not found\n", 19);
                    continue;
                }
            }
            else if(requestP[conn_fd].status==2){
                if(requestP[conn_fd].file_size<=0){
                    requestP[conn_fd].put_fd = -1;
                    requestP[conn_fd].status = 1;
                    continue;
                }
                char file_buf[1100];
                int now=recv(requestP[conn_fd].conn_fd, file_buf, 1024, 0);
                if(now<0){
                    fprintf(stderr, "recv error\n");
                    perror("Error : ");
                    continue;
                }
                requestP[conn_fd].file_size-=now;
                write(requestP[conn_fd].put_fd, file_buf, now);
            }
            else if(requestP[conn_fd].status==3){
                if(requestP[conn_fd].file_size<=0){
                    requestP[conn_fd].get_fd = -1;
                    requestP[conn_fd].status = 1;
                    continue;
                }
                char file_buf[1100];
                int now=read(requestP[conn_fd].get_fd, file_buf, 1024);
                if(now<0){
                    fprintf(stderr, "read error\n");
                    perror("Error : ");
                    continue;
                }
                int sent=send(requestP[conn_fd].conn_fd, file_buf, now, 0);
                requestP[conn_fd].file_size-=sent;
            }

        }


    }
    fprintf(stderr, "end\n");
	free(requestP);
    return 0;
}

// ======================================================================================================


static void init_request(request *reqP){
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request *reqP){
    init_request(reqP);
}

static void init_server(unsigned short port){
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0)
        ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;

    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&tmp, sizeof(tmp)) < 0){
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0){
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request *)malloc(sizeof(request) * maxfd);
    if (requestP == NULL){
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++){
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
