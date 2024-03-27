
#ifndef _PEER_SERVER_H_
#define _PEER_SERVER_H_

#include "defines.h"
#include "tcp_server.h"

/*The p2p server that sends queryhit*/
class CPeerServer : public CTcpServer{
public:
static CPeerServer* getInstance() {
        static CPeerServer instance;
        return &instance;
    }

    void SetPeerID(PeerID peer_id) {
        m_peer_id = peer_id;
    }

protected:
    void DealWithRequest(int client_sockfd, char* message, struct sockaddr_in* client_addr, bool& keep_running) override;
    virtual void BeforeRun() override;
    // peer server always keep running
    virtual bool IsKeepRunning() override {
        return true;
    }

private:
    CPeerServer() = default;
    CPeerServer(const CPeerServer&) = delete;
    CPeerServer& operator=(const CPeerServer&) = delete;
    ~CPeerServer() = default;
    PeerID m_peer_id;
};

#endif // _PEER_SERVER_H_ 