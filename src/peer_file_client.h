#ifndef _PEER_FILE_CLIENT_H_
#define _PEER_FILE_CLIENT_H_

#include "defines.h"

struct SClientRequest{
    ERequestType req_type;
    std::string filename;
    std::string ip;
    int port;
};

class CPeerFileClient {
public:
    static CPeerFileClient* getInstance() {
        static CPeerFileClient instance;
        return &instance;
    }
    void Init(int peer_id);
    
    void PushFile(const std::string& filename, const std::string& ip, int port);
    void ObtainFile(const std::string& filename, const std::string& ip, int port);

private:
    void HandleRequestThreadFunc();
    void ExecPushFile(const std::string& filename, const std::string& ip, int port);
    void ExecObtainFile(const std::string& filename, const std::string& ip, int port);
    std::string CreateRequestHeader(ERequestType req_type, std::string filename);
    void HandleObtainResponse(int socket_fd, const std::string& filename);
    void HandlePushResponse(int socket_fd, const std::string& filename);
    CPeerFileClient() = default;
    CPeerFileClient(const CPeerFileClient&) = delete;
    CPeerFileClient& operator=(const CPeerFileClient&) = delete;

    std::mutex m_request_queue_mutex;
    std::condition_variable m_request_queue_cv;
    std::queue<SClientRequest> m_request_queue;
    
    std::mutex m_obtain_num_mutex;
    int m_peer_id;
    // for single node test purpose
    int m_obtain_num = 0;
};

#endif // _PEER_FILE_CLIENT_H_