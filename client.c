#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_FILE_N 100
#define PORT_DEFAULT 4000
#define IP_DEFAULT "http://127.0.0.1"
int file_n, rep;
char req_files[MAX_FILE_N][100];
int port = PORT_DEFAULT;
char *ip = IP_DEFAULT;
int file_num = 0;

void *worker_job(int tid);

int main(int argc, char** argv) {
    int num_thread, i;
    FILE * fp;

    if (argc != 4) {
        printf("ERROR: cannot parse # of threads, # of repetition and request_list file\n");
        return -1;
    }

    num_thread = atoi(argv[1]);
    rep = atoi(argv[2]);

    srand((time(NULL)));

    fp = fopen(argv[3],"r");
    if(fp == NULL) {
        printf("ERROR: cannot open request_list file\n");
        return -1;
    }

    char buf[100];
    i=0;
    while(fgets(buf,sizeof(buf),fp)!=NULL) {
        strtok(buf,"\n");
        strcpy(req_files[i++],buf);
    }
    file_n=i;
    fclose(fp);

    pthread_t *worker_thr;
    pthread_attr_t attr;

    worker_thr = (pthread_t*)malloc(sizeof(worker_thr)*num_thread);

    for (i = 0; i < num_thread; i++) {
        pthread_attr_init(&attr);
        if (pthread_create(&worker_thr[i], NULL, worker_job, i)) {
            printf("ERROR: error while creating thread\n");
            return -1;
        }
    }

    for(i=0; i<num_thread; i++) {
        pthread_join(worker_thr[i], NULL);
    }
    return 0;
}

void *worker_job(int tid) {
    printf("create thread %d\n", tid);
    int i, cli_socket;
    struct sockaddr_in cli_addr;
    int clien=sizeof(cli_addr);
    char req[1000];
    int recv_cnt;

    for(i=0; i<rep; i++) {

        sleep(rand()%5);
        recv_cnt=0;
        // create socket
        if ((cli_socket = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
            printf("ERROR: error while creating socket\n");
            exit(-1);
        }

        // connect socket with address
        memset(&cli_addr, 0, clien);
        cli_addr.sin_family = AF_INET;
        inet_pton(AF_INET, ip, &cli_addr.sin_addr);
        cli_addr.sin_port = htons(port);

        if (connect(cli_socket,(struct sockaddr*)&cli_addr, clien) == -1) {
            printf("ERROR: error while binding socket with address\n");
            exit(-1);
        }

        sprintf(req,"GET /%s HTTP/1.0\r\n", req_files[file_num]);
        write(cli_socket,req,strlen(req));

        char c;
        while(read(cli_socket,&c,1)>0) {
            //fputc(c,stdout);
            recv_cnt++;
        }

        close(cli_socket);
        printf("thread %d finish %d/%d\n\
        received %d bytes\n",tid, i+1,rep,recv_cnt);
    }
	pthread_exit((void*)0);
}
