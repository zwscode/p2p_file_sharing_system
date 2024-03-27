#include "peer_file_server.h"
#include "peer_server.h"
#include "peer_client.h"


void runPeerFileServer(int peer_id, int port) {
    CPeerFileServer::getInstance()->Run(peer_id, port);
}

void runPeerServer(PeerID peer_id) {
    auto* peer_server = CPeerServer::getInstance();
    peer_server->SetPeerID(peer_id);
    peer_server->SetBufferSize(MSG_BUF_SIZE);
    int peer_server_port = GetPeerServerPort(peer_id);
    peer_server->Run(peer_server_port);
}


int main(int argc, char* argv[]) {
    std::string index_server_ip;
    if (argc < 2) {
        std::cout << "Usage: ./peer_exe <topology> <peer_id>" << std::endl;
        exit(1);
    }
    ETopologyType topology = static_cast<ETopologyType>(atoi(argv[1]));
    PeerID peer_id = atoi(argv[2]);

    std::thread peer_server_thread(runPeerServer, peer_id);
    peer_server_thread.detach();

    int port = GetFileServerPort(peer_id);
    std::thread peer_file_server_thread(runPeerFileServer, peer_id, port);
    peer_file_server_thread.detach();

    getPeerClientInstance()->Run(topology, peer_id);

    return 0;
}