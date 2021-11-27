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
#include <signal.h>

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
    int status; // 0 for start 1 get command 2 for putting 3 for getting
    int get_fd;
    int put_fd;
    long long int file_size;
    std::string user_name;
} request;

server svr;
request* requestP = NULL;
std::map<std::string, int> user;
int maxfd; 
int readFD[100000]={0};
int writeFD[100000]={0};

static void init_server(unsigned short port);

static void init_request(request* reqP);

static void free_request(request* reqP);

int handle_read(request* reqP){
    int r;
    char buf[2048];
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r==0){
        fprintf(stderr, "client closed\n");
        free_request(reqP);
        return 0;
    }
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
    return 1;
}

int file_select(const struct dirent *entry){
   return strcmp(entry->d_name, ".")&&strcmp(entry->d_name, "..");
}

int min(int a, int b){
	return (a>b)?b:a;
}

int main(int argc, char **argv){

    if (argc != 2){
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);
	mkdir("./server_dir", 0777);
    chdir("./server_dir");    

    struct sockaddr_in cliaddr; // used by accept()
    int clilen;
    int conn_fd; // fd for a new connection with client
    int file_fd; // fd for file that we open for reading
    char buf[1024];
    int buf_len;

    // Initialize server
    init_server((unsigned short)atoi(argv[1]));

    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    char buffer[1024];
    fd_set read_OK;
    fd_set write_OK;

    while(1){
        FD_ZERO(&read_OK);
        FD_ZERO(&write_OK);
        FD_SET(svr.listen_fd, &read_OK);
        for(int a=4;a<10000;a++){
            if(writeFD[a]){
                FD_SET(a, &write_OK);
            }
            if(readFD[a]){
                FD_SET(a, &read_OK);
            }   
        }

        int co = select(maxfd+1, &read_OK, &write_OK, NULL, NULL);

        if(FD_ISSET(svr.listen_fd, &read_OK)){
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
            readFD[conn_fd]=1;
            requestP[conn_fd].conn_fd=conn_fd;
            requestP[conn_fd].status=0;
            strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
            write(requestP[conn_fd].conn_fd, "input your username:\n", 21);
            fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
            continue;
        }

        for(conn_fd=4;conn_fd<maxfd;conn_fd++){
        
            if(!FD_ISSET(conn_fd, &read_OK)&&!FD_ISSET(conn_fd, &write_OK))
                continue;
            if(requestP[conn_fd].status==0){
                int ret=handle_read(&requestP[conn_fd]);
                if(ret<=0){
                    continue;
                }

                std::string now = requestP[conn_fd].buf;
                if(user.find(now)==user.end()){
                    user[now] = 1;
                    requestP[conn_fd].status = 1;
                    requestP[conn_fd].user_name =now;
                    int sent=send(requestP[conn_fd].conn_fd, "connect successfully\n", 22,0);
                    if(sent<0){
                        fprintf(stderr, "client closed\n");
                        free_request(&requestP[conn_fd]);
                        continue;
                    }
                }
                else{
                    int sent=send(requestP[conn_fd].conn_fd, "username is in used, please try another:\n", 42, 0);
                    if(sent<0){
                        fprintf(stderr, "client closed\n");
                        free_request(&requestP[conn_fd]);
                        continue;
                    }
                }
                continue;
            }
            else if(requestP[conn_fd].status==1){
                int ret=handle_read(&requestP[conn_fd]);
                if(ret<=0){
                    continue;
                }
                
                if(strcmp(requestP[conn_fd].buf,"ls")==0){
                    struct dirent **user_file;
                    int n=scandir(".", &user_file, file_select, alphasort);
                    char response[2048]="\0";
                    while(n--){
                        strcat(response, user_file[n]->d_name);
                        strcat(response, "\n");
                        free(user_file[n]);
                    }
                    strcat(response, "\0");
                    int sent = send(requestP[conn_fd].conn_fd, response, strlen(response), 0);
                    free(user_file);
                    if(sent<0){
                        fprintf(stderr, "client closed\n");
                        free_request(&requestP[conn_fd]);
                        continue;
                    }
                    continue;
                }
                else if(strcmp(requestP[conn_fd].buf, "put")==0){
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    if(handle_read(&requestP[conn_fd])<=0){
                        continue;
                    }
                    char file_name[128];
                    strcpy(file_name, requestP[conn_fd].buf);
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    if(handle_read(&requestP[conn_fd])<=0){
                        continue;
                    }
                    requestP[conn_fd].file_size=atoi(requestP[conn_fd].buf);
                    write(requestP[conn_fd].conn_fd, "ACK", 4);
                    
                    char file_place[512];
                    sprintf(file_place,"./%s", file_name);
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
                    char file_place[256];
                    sprintf(file_place,"./%s", file_name);
                    requestP[conn_fd].get_fd = open(file_place, O_RDONLY, 0777);
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
                    requestP[conn_fd].status=3;
                    readFD[conn_fd] = 0;
                    writeFD[conn_fd] = 1;
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
                if(recv(requestP[conn_fd].conn_fd, file_buf, 1024, MSG_PEEK)<=0){
                    write(requestP[conn_fd].put_fd, file_buf, min(requestP[conn_fd].file_size, 1024));
                    fprintf(stderr, "client closed\n");
                    free_request(&requestP[conn_fd]);
                    continue;
                }
                int now=recv(requestP[conn_fd].conn_fd, file_buf, 1024, 0);
                requestP[conn_fd].file_size-=now;
                write(requestP[conn_fd].put_fd, file_buf, now);
            }
            else if(requestP[conn_fd].status==3){
                if(requestP[conn_fd].file_size<=0){
                    requestP[conn_fd].get_fd = -1;
                    requestP[conn_fd].status = 1;
                    readFD[requestP[conn_fd].conn_fd] = 1;
                    writeFD[requestP[conn_fd].conn_fd] = 0;
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
                if(sent<=0){
                    fprintf(stderr, "client closed\n");
                    free_request(&requestP[conn_fd]);
                    continue;
                }
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
    reqP->get_fd = -1;
    reqP->put_fd = -1;
    reqP->file_size = 0;
    reqP->status = 0;
}

static void free_request(request *reqP){
    readFD[reqP->conn_fd]=0;
    user.erase(reqP->user_name);
    reqP->user_name.clear();
    close(reqP->conn_fd);
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
