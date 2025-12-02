#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);  
    inet_aton("127.0.0.1", &addr.sin_addr); 
    
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    write(sock, "C test", 6);
    
    char buf[100];
    int n = read(sock, buf, 99);
    buf[n] = 0;
    printf("Got: %s\n", buf);
    
    close(sock);
    return 0;
}