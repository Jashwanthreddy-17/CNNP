#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int connect_to_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    return sock;
}

void read_reply(int sock) {
    char reply[1024] = {0};
    int bytes = recv(sock, reply, sizeof(reply) - 1, 0);
    if (bytes > 0) {
        printf("Server Response:\n%s\n", reply);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <test_number 1-4>\n", argv[0]);
        printf(" 1: Fragmented Header + POST Body\n");
        printf(" 2: HTTP Pipelining (Two requests in one send)\n");
        printf(" 3: Content-Length Mismatch Error\n");
        printf(" 4: Premature EOF Error (Missing \\r\\n\\r\\n)\n");
        return 1;
    }
    int test = atoi(argv[1]);
    int sock = connect_to_server();
    if (test == 1) {
        const char *part1 = "POST /data HTTP/1.1\r\nContent-Len";
        const char *part2 = "gth: 13\r\n\r\nHello Framing";
        send(sock, part1, strlen(part1), 0);
        sleep(1);
        send(sock, part2, strlen(part2), 0);
        read_reply(sock);
    } else if (test == 2) {
        const char *pipelined = "GET /first HTTP/1.1\r\n\r\nGET /second HTTP/1.1\r\n\r\n";
        send(sock, pipelined, strlen(pipelined), 0);
        sleep(1);
        read_reply(sock);
    } else if (test == 3) {
        const char *mismatch = "POST /err HTTP/1.1\r\nContent-Length: 50\r\n\r\nShort";
        send(sock, mismatch, strlen(mismatch), 0);
        shutdown(sock, SHUT_WR);
        read_reply(sock);
    } else if (test == 4) {
        const char *broken = "GET /broken HTTP/1.1\r\nHost: localhost";
        send(sock, broken, strlen(broken), 0);
        shutdown(sock, SHUT_WR);
        read_reply(sock);
    }
    close(sock);
    return 0;
}
