#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define MAX_POOL_SIZE 1024
#define EPOLL_SIZE 1024
#define MAX_EVENTS 1024

void *worker_job(int tid);

void httpd(char *command, int socket);

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int epoll_fd, serv_socket;

int main(int argc, char **argv) {

    int i;

    if (argc != 3) {
        printf("ERROR: port number and thread pool size are not specified\n");
        return -1;
    }

    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);

    if (pool_size > MAX_POOL_SIZE) {
        printf("ERROR: max thread pool size exceeded\n max thread pool size is %d\n", MAX_POOL_SIZE);
        return -1;
    }

    // create socket

    if ((serv_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("ERROR: error while creating socket\n");
        return -1;
    }

    // socket option setting
    int one = 1;
    if (setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one)) < 0) {
        printf("ERROR: error while set socket option\n");
        return -1;
    }

    // bind socket with address
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(serv_socket, (const struct sockaddr *) &serv_addr, sizeof(struct sockaddr_in)) == -1) {
        printf("ERROR: error while binding socket with address\n");
        return -1;
    }

    // listen to socket
    if (listen(serv_socket, 10) == -1) {
        printf("ERROR: error while start listening socket\n");
        return -1;
    }

    // create epoll
    epoll_fd = epoll_create(EPOLL_SIZE);
    if (epoll_fd < 0) {
        printf("ERROR: error while creating epoll\n");
        return -1;
    }

    // add server socket to epoll
    struct epoll_event events;
    events.events = EPOLLIN;
    events.data.fd = serv_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serv_socket, &events) < 0) {
        printf("ERROR: error while set server socket to epoll");
    }

    // create worker threads to thread pool
    pthread_t *worker_thr;
    worker_thr = (pthread_t *) malloc(sizeof(pthread_t) * pool_size);

    pthread_mutex_init(&mutex, NULL);

    for (i = 0; i < pool_size; i++) {
        if (pthread_create(&worker_thr[i], NULL, worker_job, i)) {
            printf("ERROR: error while creating thread\n");
            return -1;
        }
    }

    for (i = 0; i < pool_size; i++) {
        pthread_join(worker_thr[i], NULL);
    }

    return 0;
}


void *worker_job(int tid) {
    char command[10000];
    int event_count, timeout = -1, i;
    struct epoll_event epoll_events[MAX_EVENTS];

    printf("create thread %d\n", tid);
    while (1) {
        event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, timeout);
        if (event_count < 0) {
            printf("ERROR: error while epoll_wait\n");
            exit(-1);
        }
        printf("thread %d receive %d events\n", tid, event_count);
        for (i = 0; i < event_count; i++) {
            if (epoll_events[i].data.fd == serv_socket) /* Accept */ {
                int client_fd;
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                // accept connection and get client fd
                pthread_mutex_lock(&mutex);
                client_fd = accept(serv_socket, (struct sockaddr *) &client_addr, &client_len);
                pthread_mutex_unlock(&mutex);
                printf("client accepted at fd : %d\n", client_fd);
                int flags = fcntl(client_fd, F_GETFL);
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                struct epoll_event client_events;
                client_events.events = EPOLLIN;
                client_events.data.fd = client_fd;

                // add client fd to epoll
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_events) < 0) {
                    printf("ERROR: error while add client_fd to epoll\n");
                    exit(-1);
                }
            } else { /* Get reqeust */
                int client_fd = epoll_events[i].data.fd;
                char buf[10000];
                memset(buf, 0, sizeof(buf));

                if ( read(client_fd, &buf, sizeof(buf)) == 0) { /* disconnect */
                    close(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

                } else { /* process request */
                    printf("client req : %s\n", buf);
                    httpd(buf, client_fd);

                    printf("thread %d finish\n", tid);
                }
            }
        }
    }
}
#define SERVER_NAME "ANGJIN"
#define SERVER_URL "http://www.github.com/angjin"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"


static void send_error(int status, char *title, char *extra_header, char *text, int socket);

static void
send_headers(int status, char *title, char *extra_header, char *mime_type, off_t length, time_t mod, int socket);

static char *get_mime_type(char *name);

static void strdecode(char *to, char *from);

static int hexit(char c);


#define PATH_DEFAULT "/var/tmp/20151528/"

void
httpd(char *command, int socket) {
    char method[10000], path[10000], protocol[10000];
    char tmp_path[10000] = PATH_DEFAULT;
    char *line;
    char *file;
    size_t len;
    int ich;
    struct stat sb;
    FILE *fp;

    line = command;

    if (sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol) != 3) {
        send_error(400, "Bad Request", (char *) 0, "Can't parse request.", socket);
        return;
    }

    if (strcasecmp(method, "get") != 0) {
        send_error(501, "Not Implemented", (char *) 0, "That method is not implemented.", socket);
        return;
    }

    if (path[0] != '/') {
        send_error(400, "Bad Request", (char *) 0, "Bad filename.", socket);
        return;
    }

    file = &(path[1]);
    strdecode(file, file);
    if (file[0] == '\0') {
        file = "./";
    } else {
        strcat(tmp_path, file);
        strcpy(path, tmp_path);
        file = path + strlen(PATH_DEFAULT);
    }
    printf("path : %s\nfile : %s\n", path, file);

    len = strlen(file);
    if (file[0] == '/' || strcmp(file, "..") == 0 || strncmp(file, "../", 3) == 0 ||
        strstr(file, "/../") != (char *) 0 || strcmp(&(file[len - 3]), "/..") == 0) {
        send_error(400, "Bad Request", (char *) 0, "Illegal filename.", socket);
        return;
    }

    if (stat(path, &sb) < 0) {
        send_error(404, "Not Found", (char *) 0, "File not found.", socket);
        return;
    }

    if (S_ISDIR(sb.st_mode)) {
        send_error(400, "Bad Request", (char *) 0, "Directory is not allowed.", socket);
        return;
    }

    // send file
    fp = fopen(path, "r");
    if (fp == (FILE *) 0) {
        send_error(403, "Forbidden", (char *) 0, "File is protected.", socket);
        return;
    }

    send_headers(200, "Ok", (char *) 0, get_mime_type(file), sb.st_size, sb.st_mtime, socket);
    while ((ich = getc(fp)) != EOF) {
        write(socket, &ich, 1);
    }
}

static void
send_error(int status, char *title, char *extra_header, char *text, int socket) {
    send_headers(status, title, extra_header, "text/html", -1, -1, socket);
    char buf[10000];
    (void) sprintf(buf, "\
<!DOCTYPE html>\n\
<html>\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>%d %s</title>\n\
  </head>\n\
  <body bgcolor=\"#cc9999\">\n\
    <h4>%d %s</h4>\n", status, title, status, title);
    write(socket, buf, strlen(buf));
    (void) sprintf(buf, "%s\n", text);
    write(socket, buf, strlen(buf));
    (void) sprintf(buf, "\
    <hr>\n\
    <address><a href=\"%s\">%s</a></address>\n\
  </body>\n\
</html>\n", SERVER_URL, SERVER_NAME);
    write(socket, buf, strlen(buf));
    (void) fflush(stdout);
}

static void
send_headers(int status, char *title, char *extra_header, char *mime_type, off_t length, time_t mod, int socket) {
    time_t now;
    char timebuf[100], buf[1000];

    (void) sprintf(buf, "%s %d %s\015\012", PROTOCOL, status, title);
    write(socket, buf, strlen(buf));
    (void) sprintf(buf, "Server: %s\015\012", SERVER_NAME);
    write(socket, buf, strlen(buf));
    now = time((time_t *) 0);
    (void) strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    (void) sprintf(buf, "Date: %s\015\012", timebuf);
    write(socket, buf, strlen(buf));
    if (extra_header != (char *) 0) {
        (void) sprintf(buf, "%s\015\012", extra_header);
        write(socket, buf, strlen(buf));
    }
    if (mime_type != (char *) 0) {
        (void) sprintf(buf, "Content-Type: %s\015\012", mime_type);
        write(socket, buf, strlen(buf));
    }
    if (length >= 0) {
        (void) sprintf(buf, "Content-Length: %lld\015\012", (long long) length);
        write(socket, buf, strlen(buf));
    }
    if (mod != (time_t) -1) {
        (void) strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&mod));
        (void) sprintf(buf, "Last-Modified: %s\015\012", timebuf);
        write(socket, buf, strlen(buf));
    }
    (void) sprintf(buf, "Connection: close\015\012");
    write(socket, buf, strlen(buf));
    (void) sprintf(buf, "\015\012");
    write(socket, buf, strlen(buf));
}


static char *
get_mime_type(char *name) {
    char *dot;

    dot = strrchr(name, '.');
    if (dot == (char *) 0)
        return "text/plain; charset=UTF-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=UTF-8";
    if (strcmp(dot, ".xhtml") == 0 || strcmp(dot, ".xht") == 0)
        return "application/xhtml+xml; charset=UTF-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".xml") == 0 || strcmp(dot, ".xsl") == 0)
        return "text/xml; charset=UTF-8";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";
    return "text/plain; charset=UTF-8";
}


static void
strdecode(char *to, char *from) {
    for (; *from != '\0'; ++to, ++from) {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            *to = hexit(from[1]) * 16 + hexit(from[2]);
            from += 2;
        } else
            *to = *from;
    }
    *to = '\0';
}


static int
hexit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;        /* shouldn't happen, we're guarded by isxdigit() */
}
