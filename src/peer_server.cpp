
#include "peer_server.h"
#include "peer_client.h"


void CPeerServer::DealWithRequest(int client_sockfd, char* message_char, struct sockaddr_in* client_addr, bool& keep_running) {
    std::string ip = inet_ntoa(client_addr->sin_addr);
    std::string message = std::string(message_char);
    if(message.length() == 0) {
        return;
    }
    auto now_time = GetTimestamp();
    std::cout << "CPeerServer]Peer:" << m_peer_id << " Receive msg:" << message << " at " << now_time << std::endl;
    auto* peer_client = CPeerClient::getInstance();
    peer_client->HandleMessage(message);
}

void CPeerServer::BeforeRun() {
}
