#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int server_fd =socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd <0) {std::cerr << "Failed to create socket\n"; return 1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt , sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6380);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) <0) 
   { std::cerr << "Bind failed\n";
    return 1;}

    if (listen(server_fd, 10) < 0) {
        std::cerr <<"Listen failed\n";
        return 1;
    }

    std::cout << "my-redis listening on port 6380...\n";

    while (true) { 
        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd <0) {std::cerr << "Accept failed\n"; continue;}

        std::cout << "client connected: " << inet_ntoa(client_address.sin_addr) << "\n";

        char buffer[1024];
        while (true) {ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {std::cout << "Client disconnected\n";
        break;
    }
buffer[bytes_read] = '\0';
std::cout << "Received: " << buffer;
write(client_fd, buffer, bytes_read);
}
    
 close(client_fd);   }
close(server_fd);
return 0;
    
}

