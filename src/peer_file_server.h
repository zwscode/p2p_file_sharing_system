#ifndef _PEER_FILE_SERVER_H_
#define _PEER_FILE_SERVER_H_
#include "defines.h"
#include <mutex>


class CPeerFileServer {
public:
    static CPeerFileServer* getInstance() {
        static CPeerFileServer instance;
        return &instance;
    }
    void Init();
    void Run(int peer_id, int port);
    void Stop();

    
private:
    int m_socket_fd;
    int m_port;
    int m_peer_id;

    std::mutex m_socket_fd_counts_mutex;
    int m_socket_fd_counts;

    std::mutex m_socket_fd_queue_mutex;
    std::condition_variable m_socket_fd_queue_cv;
    std::queue<int> m_socket_fd_queue;

    CPeerFileServer();
    CPeerFileServer(const CPeerFileServer&) = delete;
    CPeerFileServer& operator=(const CPeerFileServer&) = delete;
    ~CPeerFileServer();

    void HandleClient(int client_sockfd);
    void ReceiveFile(int client_sockfd, const std::string& filename);
    void SendFile(int client_sockfd, const std::string& filename);
    std::string CreateResponseHeader(EResponseType res_type, const std::string& filename);
    void HandleRequestThreadFunc();
};

#endif // _PEER_FILE_SERVER_H_