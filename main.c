/*
 * @Date: 2021-10-09 08:57:47
 * @LastEditTime: 2021-10-12 11:10:44
 * @Description: Proxy
 * @FilePath: /CS3103Proxy/proxy.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h> // Internet Protocol Family
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <sys/time.h> 


#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d, %b, %Y, %H:%M:%S GMT"
#define TIMEOUT 300

int init_server_socket(int port) {  // TODO: Only IPv4 at present
    struct sockaddr_in Saddress; // IPv4 Address netinet/in.h

    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // Socket Creation sys/socket.h; AF_INET: IPv4

    if (sockfd < 0) {
        perror("Server Socket creation failed.");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { // DIFF
        perror("Setsockopt failed.");
        exit(EXIT_FAILURE);
    }

    Saddress.sin_family = AF_INET;
    Saddress.sin_addr.s_addr = htonl(INADDR_ANY);
    Saddress.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &Saddress, sizeof(Saddress)) < 0) { // Cast to a general descriptor (struct sockaddr *)
        perror("Bind failed.");
        exit(EXIT_FAILURE);
    }

    int backlog = 3; // Number of pending connections to queue;

    if (listen(sockfd, backlog) < 0) {
        perror("Listen failed.");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int init_client_socket(int client_sock, char* host, int port) {
    
    struct sockaddr_in TAddress; // Target IPv4 address

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        // Target Socket Creation Failed
        raise_error(client_sock, 500, "Internal Error");

        perror("Target Socket creation failed.");
        return -1;
    }

    struct hostent *he; // netdb.h

    if ((he = gethostbyname(host)) == NULL) {
        // Unknown host
        raise_error(client_sock, 404, "Not Found");

        perror("Unknown target host.");
        return -1;
    }

    memset(&TAddress, '\0', sizeof(TAddress));
    TAddress.sin_family = AF_INET;
    memcpy((char *) &TAddress.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
    TAddress.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*) &TAddress, sizeof(TAddress)) < 0) {
        //Target connection failed
        raise_error(client_sock, 503, "Service Unavailable");

        perror("Target connection failed.");
        return -1;
    }

    return sockfd;
}

void send_headers(int client, int status, char *title, char *extra_header, char *content_type, int length) {
    char buffer[10000];

    // (HTTP/1.x) (200) (OK)
    snprintf(buffer, sizeof(buffer), "%s %d %s\r\n", PROTOCOL, status, title);
    send(client, buffer, strlen(buffer), 0);

    // Date
    time_t now = time((time_t*) 0);
    char time_buffer[100];
    strftime(time_buffer, sizeof(time_buffer), RFC1123FMT, gmtime(&now));
    snprintf(buffer, sizeof(buffer), "Date: %s\r\n", time_buffer);
    send(client, buffer, strlen(buffer), 0);

    // Context-Type
    if (content_type != (char *) 0) {
        snprintf(buffer, sizeof(buffer), "Content-Type: %s\r\n", content_type);
        send(client, buffer, strlen(buffer), 0);
    }

    // Content-Length
    if (length >= 0) {
        snprintf(buffer, sizeof(buffer), "Content-Length: %d\r\n", length);
        send(client, buffer, strlen(buffer), 0);
    }

    // \r\n END
    snprintf(buffer, sizeof(buffer), "\r\n");
    send(client, buffer, strlen(buffer), 0);
}

void raise_error(int client, int status, char *title){
    send_headers(client, status, title, (char *) 0, (char *) 0, -1);
}

/*it will discard the "\\r\\n" at the end of each line*/
int read_request_line(int sock, char *buffer, int buf_size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < buf_size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0); // receive one char from the socket
        buffer[i] = c;
        i++;

        //Debug: print received msg
        printf("%c", c);
    }

    while (buffer[i-1] == '\n' || buffer[i-1] == '\r') {
        buffer[i-1] = '\0';
        i--;
    }

    return i;
}

void proxy_https(int client, char* method, char* host, char* protocol, char* headers, FILE* sockrfp, FILE* sockwfp) {
    //HTTP/1.x 200 Connection established\r\n\r\n
    char *connection_established = "HTTP/1.0 200 Connection established\r\n\r\n";
    send(client, connection_established, strlen(connection_established), 0);

    int client_read_fd, server_read_fd, client_write_fd, server_write_fd;
    client_read_fd = client;
    server_read_fd = fileno(sockrfp);
    client_write_fd = client;
    server_write_fd = fileno(sockwfp);

    struct timeval timeout; // sys/time.h
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    int maxfd; //maximum file descriptor + 1
    if (client_read_fd >= server_read_fd) {
        maxfd = client_read_fd + 1;
    } else {
        maxfd = server_read_fd + 1;
    }

    fd_set fdset;
    int r;
    char buffer[10000];

    while (1){
        FD_ZERO(&fdset);
        FD_SET(client_read_fd, &fdset);
        FD_SET(server_read_fd, &fdset);
        
        r = select(maxfd, &fdset, (fd_set*) 0, (fd_set*) 0, &timeout);

        if (r == 0) {
            raise_error(client, 408, "Request Timeout");
            return;
        } else if (FD_ISSET(client_read_fd, &fdset)) {
            r = read(client_read_fd, buffer, sizeof(buffer));
            if (r <= 0) {
                break;
            }
            r = write(server_write_fd, buffer, r);
            if (r <= 0) {
                break;
            }
        } else if (FD_ISSET(server_write_fd, &fdset)) {
            
        }
    }
}

int check_blacklist(char *b_list_path, char *url) {
    FILE *blacklist_file = fopen(*b_list_path, "r");
    if(blacklist_file == NULL) 
        return 0;
    char input_url[10000];
    input_url = fscanf(*b_list_path);
    while (input_url != EOF) {
        char *cmp;
        cmp = strstr(url, input_url);
        if (cmp) {
            return 1;
        } else {
            return 0;
        }
    }
}

void *process_request(void * _client_sock) {
    int client_sock = (int) (long) _client_sock;

    // Parse the request
    char line[10000]; // TODO: Length?

    char method[10000];
    char url[10000];
    char protocol[10000];

    //Parse the first line
    int line_len = read_request_line(client_sock, line, sizeof(line));
    // Wrong format
    if (sscanf(line, "%[^ ] %[^ ] %[^ ]", method, url, protocol) != 3) {
        raise_error(client_sock, 400, "Bad Request");
        return NULL;
    }

    // Null URL
    if (url[0] == '\0') {
        raise_error(client_sock, 400, "Bad Request");
        return NULL;
    }

    //Check for blacklisted URL
    if (check_blacklist(char *b_list_path, char url) == 1) {
        raise_error(client_sock, 403, "Blacklisted URL");
        return NULL;
    }

    char host[10000];
    int port = 443;

    if (strcmp(method, "CONNECT") == 0) {
        // URL without :port
        if (sscanf(url, "%[^:]:%d", host, &port) != 2) {
            raise_error(client_sock, 400, "Bad Request");
            return NULL;
        } 
    } else {
        // Unknown method (NOT CONNECT)
        raise_error(client_sock, 400, "Bad Request");
        return NULL;
    }

    //Parse the headers
    char headers[20000];
    int headers_len = 0;
    while (read_request_line(client_sock, line, sizeof(line)) > 0) {
        int line_len = strlen(line);
        memcpy(&headers[headers_len], line, line_len);
        headers_len += line_len;
    }
    headers[headers_len] = '\0';

    int target_sock = open_client_socket(client_sock, host, port);

    if (target_sock >= 0) {
        FILE* sockrfp;
        FILE* sockwfp;
        
        proxy_https(client_sock, method, host, protocol, headers, sockrfp, sockwfp);

        close(target_sock);
    }

    close(client_sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <port> <flag_telemetry> <filename of blacklist>\n", argv[0]); // stdio.h
        exit(EXIT_FAILURE); // stdlib.h
    }

    int port = atoi(argv[1]);
    int flag_telemetry = atoi(argv[2]);
    char *b_list_path = argv[3];

    // Debug input
    // printf("%d\n%d\n%s\n", port, flag_telemetry, b_list_path);

    int server_sock = init_server_socket(port);
    printf(";) HTTPS Proxy running on port %d!\n", port);

    struct sockaddr_in Caddress;
    socklen_t clen = sizeof(Caddress);
    int client_sock;

    while (1) {
        if ((client_sock = accept(server_sock, (struct sockaddr_in *) &Caddress, &clen)) < 0) {
            perror("Accept failed.");
            exit(EXIT_FAILURE);
        }

        int valread;
        int buffer_size = 1024; // Buffer size;
        char buffer[buffer_size];
        
        // Debug Listen Value
        // valread = read(client_sock, buffer, sizeof(buffer)); // unistd.h
        // printf("%s\n", buffer);

        process_request((void *) (long) client_sock); // Void Pointer <== Long (same-size) <== Int
    }

    close(server_sock);
    
    return 0;
}