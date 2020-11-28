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

#define MAX_POOL_SIZE 1024

void *worker_job(int tid);

void httpd(char *command, int socket);

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct myNode *npt;
typedef struct myNode {
    int val;
    npt next;
} node;

npt head = NULL, tail = NULL;

int isEmpty() {
    if (head == NULL) {
        return 1;
    }
    return 0;
}

void enqueue(int socket) {
    npt new = (npt) malloc(sizeof(node));
    new->val = socket;
    new->next = NULL;
    if (isEmpty()) {
        head = tail = new;
    } else {
        tail->next = new;
    }
}

int dequeue() {
    if (isEmpty())
        return -1;

    npt top = head;
    int val = top->val;
    head = top->next;
    free(top);
    return val;
}

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
    int serv_socket;
    if ((serv_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("ERROR: error while creating socket\n");
        return -1;
    }

    // bind socket with address
    struct sockaddr_in serv_addr;
    int one;
    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one));
    if (bind(serv_socket, (const struct sockaddr *) &serv_addr, sizeof(struct sockaddr_in)) == -1) {
        printf("ERROR: error while binding socket with address\n");
        return -1;
    }

    // listen to socket
    if (listen(serv_socket, 10) == -1) {
        printf("ERROR: error while start listening socket\n");
        return -1;
    }

    // create threads to thread pool
    pthread_t service_thr;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    for (i = 0; i < pool_size; i++) {
        if (pthread_create(&service_thr, NULL, worker_job, i)) {
            printf("ERROR: error while creating thread\n");
            return -1;
        }
    }

    // loop infinitely and accept client
    int client_socket;
    socklen_t clien;
    struct sockaddr_in cli_addr;

    while (1) {
        clien = sizeof(cli_addr);
        client_socket = accept(serv_socket, (struct sockaddr *) &cli_addr, &clien);
        if (client_socket < 0) {
            printf("connection to client failed\n");
            continue;
        } else {
            enqueue(client_socket);
            pthread_cond_signal(&cond);
        }
    }
    pthread_join(service_thr, NULL);
    return 0;
}


void *worker_job(int tid) {
    char command[10000];
    printf("create thread %d\n", tid);
    int client_socket;
    while (1) {
        pthread_mutex_lock(&mutex);
        while (isEmpty()) {
            printf("thread %d wait\n", tid);
            pthread_cond_wait(&cond, &mutex);
            printf("thread %d up!\n", tid);
            client_socket = dequeue();
            if (client_socket > 0)
                break;
        }
        pthread_mutex_unlock(&mutex);
        memset(command, 0, 10000);

        read(client_socket, command, 10000);
        printf("client req : %s\n", command);
        httpd(command, client_socket);

        close(client_socket);
        printf("thread %d finish\n", tid);
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
