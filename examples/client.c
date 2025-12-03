#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int client_fd;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(12345);
    inet_aton("127.0.0.1", &serv_addr.sin_addr);
    
    connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    
    send(client_fd, "Hello from client C", 19, 0);
    printf("Message sent\n");
    
    read(client_fd, buffer, sizeof(buffer) - 1);
    printf("Got: %s\n", buffer);
    
    close(client_fd);
    return 0;
}
