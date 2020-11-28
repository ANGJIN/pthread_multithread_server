#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#define MAX_POOL_SIZE 1024

void *service_dispatch(int tid);

char *httpd(char *command);

pthread_cond_t *cond;
int *busy;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

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

    cond = (pthread_cond_t *) malloc(sizeof(pthread_cond_t) * pool_size);
    busy = (int *) calloc(sizeof(int), pool_size);
    for (i = 0; i < pool_size; i++) {
        pthread_cond_init(&cond[i], NULL);

        if (pthread_create(&service_thr, NULL, service_dispatch, i)) {
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
            // wake up thread in thread pool
            // #TODO : request queue
            int worker_idx = -1;
            for (i = 0; i < pool_size; i++) {
//                pthread_mutex_lock(&mutex);
                if (busy[i] == 0) {
                    worker_idx = i;
                    break;
                }
//                pthread_mutex_unlock(&mutex);
            }
            if (worker_idx < 0) {
                printf("ERROR: socket is full\n");
                close(client_socket);
            } else {
                pthread_mutex_lock(&mutex);
                busy[worker_idx] = client_socket;
                printf("wake up thread %d\n", worker_idx);
                pthread_cond_signal(&cond[worker_idx]);
                pthread_mutex_unlock(&mutex);
            }
        }

    }

    pthread_join(service_thr, NULL);
    return 0;
}


void *service_dispatch(int tid) {
    char command[10000];
    printf("create thread %d\n", tid);
    int client_socket;
    while (1) {
        pthread_mutex_lock(&mutex);
        while (busy[tid] == 0) {
            printf("thread %d wait\n", tid);
            pthread_cond_wait(&cond[tid], &mutex);
        }
        printf("thread %d up!\n", tid);
        client_socket = busy[tid];
        pthread_mutex_unlock(&mutex);
        memset(command, 0, 10000);

        read(client_socket, command, 10000);
        printf("client req : %s\n", command);
        char *buf;
        buf = httpd(command);
        write(client_socket, &buf, 100000);
        close(client_socket);

        printf("thread %d finish\n", tid);
        pthread_mutex_lock(&mutex);
        busy[tid] = 0;
        pthread_mutex_unlock(&mutex);
    }
}

#define SERVER_NAME "micro_httpd"
#define SERVER_URL "http://www.acme.com/software/micro_httpd/"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"


/* Forwards. */
static void file_details(char *dir, char *name);

static void send_error(int status, char *title, char *extra_header, char *text);

static void send_headers(int status, char *title, char *extra_header, char *mime_type, off_t length, time_t mod);

static char *get_mime_type(char *name);

static void strdecode(char *to, char *from);

static int hexit(char c);

static void strencode(char *to, size_t tosize, const char *from);


char *
httpd(char *command) {
    char method[10000], path[10000], protocol[10000], idx[20000], location[20000];
    char *line, *result;
    char *file;
    size_t len;
    int ich;
    struct stat sb;
    FILE *fp;
    struct dirent **dl;
    int i, n;

    result = (char *) malloc(100000);
    line = command;

    if (sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol) != 3)
        send_error(400, "Bad Request", (char *) 0, "Can't parse request.");

    if (strcasecmp(method, "get") != 0)
        send_error(501, "Not Implemented", (char *) 0, "That method is not implemented.");
    if (path[0] != '/')
        send_error(400, "Bad Request", (char *) 0, "Bad filename.");
    file = &(path[1]);
    strdecode(file, file);
    if (file[0] == '\0')
        file = "./";
    len = strlen(file);
    if (file[0] == '/' || strcmp(file, "..") == 0 || strncmp(file, "../", 3) == 0 ||
        strstr(file, "/../") != (char *) 0 || strcmp(&(file[len - 3]), "/..") == 0)
        send_error(400, "Bad Request", (char *) 0, "Illegal filename.");
    if (stat(file, &sb) < 0)
        send_error(404, "Not Found", (char *) 0, "File not found.");

    else if (S_ISDIR(sb.st_mode)) {
        if (file[len - 1] != '/') {
            (void) snprintf(
                    location, sizeof(location), "Location: %s/", path);
            send_error(302, "Found", location, "Directories must end with a slash.");
        }
        (void) snprintf(idx, sizeof(idx), "%sindex.html", file);
        if (stat(idx, &sb) >= 0) {
            file = idx;
            goto do_file;
        }
        send_headers(200, "Ok", (char *) 0, "text/html", -1, sb.st_mtime);
        (void) printf("\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
<html>\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>Index of %s</title>\n\
  </head>\n\
  <body bgcolor=\"#99cc99\">\n\
    <h4>Index of %s</h4>\n\
    <pre>\n", file, file);
        n = scandir(file, &dl, NULL, alphasort);
        if (n < 0) {
            perror("scandir");
        } else {
            for (i = 0; i < n; ++i)
                file_details(file, dl[i]->d_name);
        }
        (void) printf("\
    </pre>\n\
    <hr>\n\
    <address><a href=\"%s\">%s</a></address>\n\
  </body>\n\
</html>\n", SERVER_URL, SERVER_NAME);
    } else {
        do_file:
        fp = fopen(file, "r");
        if (fp == (FILE *) 0) {
            send_error(403, "Forbidden", (char *) 0, "File is protected.");
        } else {
            send_headers(200, "Ok", (char *) 0, get_mime_type(file), sb.st_size, sb.st_mtime);
            while ((ich = getc(fp)) != EOF)
                putchar(ich);
        }
    }
    (void) fflush(stdout);
    return result;
}


static void
file_details(char *dir, char *name) {
    static char encoded_name[1000];
    static char path[2000];
    struct stat sb;
    char timestr[16];

    strencode(encoded_name, sizeof(encoded_name), name);
    (void) snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &sb) < 0)
        (void) printf("<a href=\"%s\">%-32.32s</a>    ???\n", encoded_name, name);
    else {
        (void) strftime(timestr, sizeof(timestr), "%d%b%Y %H:%M", localtime(&sb.st_mtime));
        (void) printf("<a href=\"%s\">%-32.32s</a>    %15s %14lld\n", encoded_name, name, timestr,
                      (long long) sb.st_size);
    }
}


static void
send_error(int status, char *title, char *extra_header, char *text) {
    send_headers(status, title, extra_header, "text/html", -1, -1);
    (void) printf("\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
<html>\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>%d %s</title>\n\
  </head>\n\
  <body bgcolor=\"#cc9999\">\n\
    <h4>%d %s</h4>\n", status, title, status, title);
    (void) printf("%s\n", text);
    (void) printf("\
    <hr>\n\
    <address><a href=\"%s\">%s</a></address>\n\
  </body>\n\
</html>\n", SERVER_URL, SERVER_NAME);
    (void) fflush(stdout);
}


static void
send_headers(int status, char *title, char *extra_header, char *mime_type, off_t length, time_t mod) {
    time_t now;
    char timebuf[100];

    (void) printf("%s %d %s\015\012", PROTOCOL, status, title);
    (void) printf("Server: %s\015\012", SERVER_NAME);
    now = time((time_t *) 0);
    (void) strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    (void) printf("Date: %s\015\012", timebuf);
    if (extra_header != (char *) 0)
        (void) printf("%s\015\012", extra_header);
    if (mime_type != (char *) 0)
        (void) printf("Content-Type: %s\015\012", mime_type);
    if (length >= 0)
        (void) printf("Content-Length: %lld\015\012", (long long) length);
    if (mod != (time_t) -1) {
        (void) strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&mod));
        (void) printf("Last-Modified: %s\015\012", timebuf);
    }
    (void) printf("Connection: close\015\012");
    (void) printf("\015\012");
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


static void
strencode(char *to, size_t tosize, const char *from) {
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char *) 0) {
            *to = *from;
            ++to;
            ++tolen;
        } else {
            (void) sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}
