#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#define PORT 8080
#define BACKLOG 2
#define BUFFER_SIZE 4096
#define TIMEOUT_SEC 5

void send_response(int client_fd, const char *status, const char *body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 %s\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: keep-alive\r\n\r\n"
                       "%s",
                       status, strlen(body), body);
    send(client_fd, response, len, 0);
}

int parse_content_length(const char *headers) {
    const char *cl = strstr(headers, "Content-Length:");
    if (!cl) cl = strstr(headers, "content-length:");
    if (!cl) return 0;
    return atoi(cl + 15);
}

void handle_client(int client_fd) {
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buffer[BUFFER_SIZE] = {0};
    int buf_used = 0;
    while (1) {
        char *header_end = NULL;
        while ((header_end = strstr(buffer, "\r\n\r\n")) == NULL) {
            if (buf_used >= BUFFER_SIZE - 1) {
                printf("[ERROR] Header exceeded buffer size (431)\n");
                send_response(client_fd, "431 Request Header Fields Too Large", "Header too large\n");
                return;
            }
            int bytes = recv(client_fd, buffer + buf_used, BUFFER_SIZE - 1 - buf_used, 0);
            if (bytes == 0) {
                if (buf_used > 0) {
                    printf("[ERROR] Premature EOF before \\r\\n\\r\\n delimiter (400)\n");
                    send_response(client_fd, "400 Bad Request", "Incomplete headers\n");
                } else {
                    printf("[INFO] Client closed connection cleanly.\n");
                }
                return;
            }
            if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("[ERROR] Socket timeout waiting for headers (408)\n");
                    send_response(client_fd, "408 Request Timeout", "Header timeout\n");
                }
                return;
            }
            buf_used += bytes;
            buffer[buf_used] = '\0';
        }
        int header_len = (header_end - buffer) + 4;
        *header_end = '\0';
        int content_length = parse_content_length(buffer);
        int total_frame_len = header_len + content_length;
        if (total_frame_len >= BUFFER_SIZE) {
            printf("[ERROR] Content-Length (%d) exceeds buffer limit\n", content_length);
            send_response(client_fd, "413 Payload Too Large", "Payload too large\n");
            return;
        }
        memcpy(header_end, "\r\n\r\n", 4);
        while (buf_used < total_frame_len) {
            int bytes = recv(client_fd, buffer + buf_used, BUFFER_SIZE - 1 - buf_used, 0);
            if (bytes <= 0) {
                printf("[ERROR] Content-Length mismatch! Expected %d body bytes, got %d (400/408)\n", content_length, buf_used - header_len);
                send_response(client_fd, "400 Bad Request", "Truncated body / Content-Length mismatch\n");
                return;
            }
            buf_used += bytes;
            buffer[buf_used] = '\0';
        }
        char first_line[128] = {0};
        sscanf(buffer, "%127[^\r\n]", first_line);
        printf("[FRAMED OK] Request: \"%s\" | Header: %dB | Body: %dB\n", first_line, header_len, content_length);
        if (content_length > 0) {
            printf("            Body Payload: %.*s\n", content_length, buffer + header_len);
        }
        send_response(client_fd, "200 OK", "Message framed successfully\n");
        int remaining = buf_used - total_frame_len;
        if (remaining > 0) {
            memmove(buffer, buffer + total_frame_len, remaining);
        }
        buf_used = remaining;
        buffer[buf_used] = '\0';
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind failed"); exit(EXIT_FAILURE); }
    if (listen(server_fd, BACKLOG) < 0) { perror("listen failed"); exit(EXIT_FAILURE); }
    printf("Server listening on port %d (Backlog = %d)...\n", PORT, BACKLOG);
    printf("Pausing 8 seconds before accept() so you can inspect the backlog queue...\n");
    sleep(8);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) continue;
        printf("\n--- Accepted connection from %s:%d ---\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        handle_client(client_fd);
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
