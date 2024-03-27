
#include "tcp_server.h"

CTcpServer::~CTcpServer() {
    std::cout << "CTcpServer::~CTcpServer()" << std::endl;
}

void CTcpServer::Run(int port) {
    std::cout << "CTcpServer::Run()" << std::endl;
    m_port = port;
    BeforeRun();
    std::thread accept_thread(&CTcpServer::StartAccepting, this, port);
    accept_thread.detach();
    while(true) {
        if (IsKeepRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else {
            // end Run loop when IsKeepRunning() returns false
            break;
        }
    }
    exit(0);
}


void CTcpServer::StartAccepting(int port) {
    std::cout << "CRegisterServer::Run()\n" << std::endl;
    struct sockaddr_in server_addr;
    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);


    if (server_sockfd == -1){
        std::cout << "create socket failed." << std::endl;
        exit(1);
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr= htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // bind
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
        std::cout << "bind socket failed. errno:"<< errno << std::endl;
        exit(1);
        return;
    }

    // listen
    if(listen(server_sockfd, LISTEN_BACKLOG) == -1){
        std::cout << "listen socket failed." << std::endl;
        exit(1);
        return;
    }

    while(true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd == -1){
            std::cout << "[register server]accept socket failed." << std::endl;
            exit(1);
            return;
        }
        if (!IsKeepRunning()) {
            close(client_sockfd);
            break;
        }
        std::thread handle_thread(&CTcpServer::HandleRequest, this, client_sockfd, &client_addr);
        handle_thread.detach();
    }
    printf("register server exit.\n");
}


void CTcpServer:: SendResponse(int client_sockfd, const std::string& response_type, const std::string& response) {
        std::string response_msg = response_type + "\n" + response;
        send(client_sockfd, response_msg.c_str(), response_msg.length(), 0);
    }

void CTcpServer::HandleRequest(int client_sockfd, struct sockaddr_in* client_addr) {
    struct sockaddr_in index_client_addr;
    memset(&index_client_addr, 0, sizeof(index_client_addr));
    index_client_addr.sin_family = client_addr->sin_family;    index_client_addr.sin_addr.s_addr = client_addr->sin_addr.s_addr;
    index_client_addr.sin_port = client_addr->sin_port;

    char* buffer = new char[m_buffer_size];
    std::string message;
    bool keep_running = true;
    int bytesReceived;
    int bytesRead;
    bool errorRecv = false;
    while(keep_running) {
        if (errorRecv) {
            break;
        }
        memset(buffer, 0, m_buffer_size);
        bytesReceived = 0;
        do {
            bytesRead = recv(client_sockfd, buffer + bytesReceived, m_buffer_size - bytesReceived, 0);
            if (bytesRead < 0){
                std::cout << "[tcpserver] received bytes 0:"<< bytesReceived << std::endl;
                errorRecv = true;
                break;
            }
            bytesReceived += bytesRead;
        } while(bytesReceived < m_buffer_size);
        DealWithRequest(client_sockfd, buffer, &index_client_addr, keep_running);
    }
    delete[] buffer;
}


void CTcpServer::DealWithRequest(int client_sockfd, char* message, struct sockaddr_in* client_addr, bool& keep_running) {
    std::cout << "CTcpServer::DealWithRequest() error. child class should implement this" << std::endl;
}