
#ifndef _REGISTER_SERVER_H_
#define _REGISTER_SERVER_H_
#include "defines.h"
#include "tcp_server.h"

struct SClientInfo {
    PeerID peer_id;
    std::string ip;
};

class CRegisterServer : public CTcpServer {
public:
    static CRegisterServer* getInstance() {
        static CRegisterServer instance;
        return &instance;
    }

    void SetNumOfPeers(int num);

protected:
    void DealWithRequest(int client_sockfd, char* message, struct sockaddr_in* client_addr, bool& keep_running) override;
    virtual bool IsKeepRunning() {
        std::lock_guard<std::mutex> lock(m_mutex_exit_count);
        return m_thread_exit_count < m_num_of_peers;
    }

private:
    CRegisterServer() {
        m_num_of_peers = 0;
        m_thread_exit_count = 0;
    };
    CRegisterServer(const CRegisterServer&) = delete;
    CRegisterServer& operator=(const CRegisterServer&) = delete;
    ~CRegisterServer() = default;

    bool m_all_peers_registered;
    std::mutex m_mutex_client_info;
    std::unordered_map<PeerID, SClientInfo> m_client_info;
    std::mutex m_mutex_exit_count;
    int m_thread_exit_count;
    int m_num_of_peers;
};

#endif      // _REGISTER_SERVER_H_