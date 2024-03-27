
#ifndef _TCP_SERVER_H_
#define _TCP_SERVER_H_
#include "defines.h"


class CTcpServer
{
public:
    CTcpServer() = default;
    virtual ~CTcpServer();
    
    void Run(int port);
    void SetBufferSize(int size) {
        m_buffer_size = size;
    }

protected:
    void SendResponse(int client_sockfd, const std::string& response_type, const std::string& response);
    virtual void BeforeRun() {}
    // deal with request from client
    virtual void DealWithRequest(int client_sockfd, char* message, struct sockaddr_in* client_addr, bool& keep_running);
    virtual bool IsKeepRunning() {
        return true;
    }

    int m_buffer_size = MSG_BUF_SIZE;
    int m_port = 0;

private:
    void HandleRequest(int client_sockfd, struct sockaddr_in* client_addr);
    void StartAccepting(int port);
};


#endif      // _TCP_SERVER_H_