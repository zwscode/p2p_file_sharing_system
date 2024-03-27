#include "register_server.h"

void CRegisterServer::DealWithRequest(int client_sockfd, char* message, struct sockaddr_in* client_addr, bool& keep_running) {
    printf("CRegisterServer received message:%s\n", message);
    char* delim_pos = strchr(message, '\n');
    if (delim_pos == nullptr) {
        // wrong message
        assert(false);
    }
    char command[COMMAND_MAX_LEN];
    strncpy(command, message, delim_pos - message);
    command[delim_pos - message] = '\0';
    if (strcmp(command, REGISTER_REQUEST_REGISTER) == 0) {
        // register peer
        int peer_id = atoi(delim_pos + 1);
        m_mutex_client_info.lock();
        m_client_info[peer_id] = {peer_id, inet_ntoa(client_addr->sin_addr)};
        printf("peer %d registered, %s\n", peer_id, inet_ntoa(client_addr->sin_addr));
        if (m_client_info.size() == m_num_of_peers) {
            // all peers registered
            std::cout << "All peers registered." << std::endl;
            // trigger send peer info to all peers
            m_all_peers_registered = true;
        }
        m_mutex_client_info.unlock();
        while(!m_all_peers_registered) {
            // wait for all peers to be registered
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // send all peer info to peer
        // format: peer_id peer_ip
        std::string response_message = "";
        for (auto& client : m_client_info) {
            response_message += (std::to_string(client.first) + " " + client.second.ip + "\n");
        }
        SendResponse(client_sockfd, REGISTER_RESPONSE_ALL_PEERS, response_message);
        keep_running = false;
        m_mutex_exit_count.lock();
        m_thread_exit_count++;
        m_mutex_exit_count.unlock();
    } else {
        // wront command
        assert(false);
    }
}

void CRegisterServer::SetNumOfPeers(int num) {
    m_num_of_peers = num;
}




int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: ./register_server <num_of_peers>" << std::endl;
        exit(1);
    }
    int peer_num;
    peer_num = atoi(argv[1]);
    CRegisterServer* register_server = CRegisterServer::getInstance();
    register_server->SetNumOfPeers(peer_num);
    register_server->Run(REGISTER_SERVER_PORT);
}