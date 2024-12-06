#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <vector>
#include <string>
#include <cstring>
#include <errno.h>

#define PORT 8080

volatile sig_atomic_t wasSigHup = 0;

void sigHupHandler(int r) {
    wasSigHup = 1;
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);// (не забудь AF_INET для IPv4)
    if (server_fd == -1) {
        perror("socket failed");
        exit(1);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigHupHandler;
    sa.sa_flags |= SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }

    sigset_t blockedMask, origMask;
    sigemptyset(&blockedMask);
    sigaddset(&blockedMask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blockedMask, &origMask) == -1) {
        perror("sigprocmask failed");
        exit(1);
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { //проверка на связывание сокета с указанным адресом и портом.
        perror("bind failed");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 3) < 0) { //максимальное количество ожидающих подключений в очереди. Не больше, не меньше
        perror("listen failed");
        close(server_fd);
        exit(1);
    }

    std::vector<int> clients;
    fd_set fds;
    int maxFd = server_fd;

    while (true) {
    FD_ZERO(&fds);
    FD_SET(server_fd, &fds);
    maxFd = server_fd;

    for (int client : clients) {
        FD_SET(client, &fds);
        maxFd = std::max(maxFd, client);
    }

    if (pselect(maxFd + 1, &fds, NULL, NULL, NULL, &origMask) == -1) { //позволяет указать маску заблокированных сигналов
        if (errno == EINTR) {
            if (wasSigHup) {
                std::cout << "SIGHUP received!\n";
                wasSigHup = 0;
            }
        } 
        else {
            perror("pselect failed");
            close(server_fd);
            exit(1);
        }
    }

    if (FD_ISSET(server_fd, &fds)) { //есть ли входящее соединение (или его нет)
        struct sockaddr_in clientAddress;
        socklen_t clientLen = sizeof(clientAddress);
        int client_fd = accept(server_fd, (struct sockaddr*)&clientAddress, &clientLen);
        if (client_fd != -1) {
            clients.push_back(client_fd);
            std::cout << "New connection accepted.\n";
        } 
        else {
            perror("accept failed");
        }
    }

    for (size_t i = 0; i < clients.size(); ++i) { // обработка данных клиентов
        int client = clients[i];
        if (FD_ISSET(client, &fds)) {
            char buffer[1024] = {0};
            ssize_t bytesReceived = recv(client, buffer, 1024, 0);
            if (bytesReceived > 0) {
                std::cout << "Received: " << buffer << " from client " << client << "\n";
            } 
            else if (bytesReceived == 0) { // тут клиент мутный (не до конца читается его данные)
                std::cout << "Client " << client << " disconnected.\n";
                close(client);
                clients.erase(clients.begin() + i);
                i--; 
            } 
            else {
                perror("recv failed");
                close(client);
                clients.erase(clients.begin() + i);
                i--;
            }
        }
    }
    }
    close(server_fd);
}

int main() {
    run_server();
    return 0;
}