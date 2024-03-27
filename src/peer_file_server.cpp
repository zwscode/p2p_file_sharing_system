#include "peer_file_server.h"
#include "peer_client.h"


CPeerFileServer::CPeerFileServer() {
    m_socket_fd_counts = 0;
}

CPeerFileServer::~CPeerFileServer() {
}

void CPeerFileServer::Stop() {
    close(m_socket_fd);
}

void CPeerFileServer::Init() {
    for (int i = 0; i < FILE_SERVER_THREAD_NUM; i++) {
        // create thread to handle client request in the queue
        std::thread t(&CPeerFileServer::HandleRequestThreadFunc, this);
        t.detach();
    }
}

void CPeerFileServer::HandleRequestThreadFunc() {
    while(true) {
        std::unique_lock<std::mutex> lock(m_socket_fd_queue_mutex);
        m_socket_fd_queue_cv.wait(lock, [this](){return !m_socket_fd_queue.empty();});
        if (m_socket_fd_queue.empty()) {
            continue;
        }
        int socket_fd = m_socket_fd_queue.front();
        m_socket_fd_queue.pop();
        lock.unlock();
        HandleClient(socket_fd);

        std::lock_guard<std::mutex> fd_counts_lock(m_socket_fd_counts_mutex);
        m_socket_fd_counts--;
    }
}

void CPeerFileServer::Run(int peer_id, int port) {
    m_peer_id = peer_id;
    Init();
    // create shared directory
    DIR* dir = opendir(PEER_FILE_DIR);
    if (dir == NULL && ENOENT == errno) {
        std::cout << "open dir failed." << std::endl;
        // create the folder
        mkdir(PEER_FILE_DIR, 0777);
    } else {
        closedir(dir);
    }

    // Set up socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    m_socket_fd = socket_fd;
    if (socket_fd < 0) {
        printf("[PeerServer]Error: cannot create socket\n");
        exit(1);
    }
    
    // set socket buffer size
    int socket_buffer_size = FILE_TRANSFER_PACKET_SIZE;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &socket_buffer_size, sizeof(socket_buffer_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer_size, sizeof(socket_buffer_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }
    // Configure server address
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    m_port = port;
    // Bind socket to server address
    if (bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("[PeerServer]Error: cannot bind socket\n");
        exit(1);
    }
    
    if (listen(socket_fd, LISTEN_BACKLOG) == -1) {
        printf("[PeerServer]Error: listen failed\n");
        exit(1);
    }

    // receive request
    std::string msg_str;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sockfd;
    while(true) {
        client_sockfd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sockfd == -1) {
            printf("[PeerServer]Error: cannot accept socket, errno: %d\n", errno);
            continue;
        }
        printf("accepted socket %d\n", client_sockfd);
        {
            std::lock_guard<std::mutex> fd_counts_lock(m_socket_fd_counts_mutex);
            if (m_socket_fd_counts > MAX_SOCKET_FD_QUEUE_SIZE) {
                printf("[PeerServer]Error: socket fd max size reached, close socket.\n");
                close(client_sockfd);
                continue;
            }
            m_socket_fd_counts++;
        }

        std::unique_lock<std::mutex> lock(m_socket_fd_queue_mutex);
        printf("m_socket_fd_queue push %d\n", client_sockfd);
        m_socket_fd_queue.push(client_sockfd);
        lock.unlock();
        m_socket_fd_queue_cv.notify_one();
    }
    Stop();
}

void CPeerFileServer::HandleClient(int client_sockfd) {
    std::cout << "[CPeerFileServer] HandleClient" << std::endl;

    char msg_buffer[MSG_BUF_SIZE];
    memset(msg_buffer, 0, MSG_BUF_SIZE);
    int byte_size = recv(client_sockfd, msg_buffer, MSG_BUF_SIZE, 0);
    if (byte_size < 0) {
        printf("[PeerServer]Error: cannot receive data\n");
        close(client_sockfd);
        return;
    }
    msg_buffer[byte_size] = '\0';
    int pos[PACKET_SPLIT_NUM_REQUEST];
    int counter = 0;
    for (int i = 0; i < byte_size; i++) {
        if (msg_buffer[i] == '\n') {
            pos[counter++] = i;
            if (counter == PACKET_SPLIT_NUM_REQUEST) {
                break;
            }
        }
    }
    ERequestType request_type = (ERequestType)std::stoi(std::string(msg_buffer, pos[0]));
    std::string filename = std::string(msg_buffer + pos[0] + 1, pos[1] - pos[0] - 1);
    if (request_type == ERequestType::REQUEST_DOWNLOAD_FILE){
        SendFile(client_sockfd, filename);
    } else if (request_type == ERequestType::REQUEST_PUSH_FILE) {
        ReceiveFile(client_sockfd, filename);
    } else {
        printf("[PeerServer]Error: invalid request type: %d\n", (int)request_type);
    }
    close(client_sockfd);
}

std::string CPeerFileServer::CreateResponseHeader(EResponseType res_type, const std::string& filename ) {
    /*
    response_type;
    filename;
    */
    std::string response;
    response += std::to_string((int)res_type) + "\n";
    response += filename + "\n";
    return response;
}

void CPeerFileServer::ReceiveFile(int socket_fd, const std::string& filename) {
    printf("ReceiveFile (%s)\n", filename.c_str());
    std::string receive_dir = std::string(PEER_FILE_DIR) + "/received" + std::to_string(m_peer_id) + "/";

    DIR* dir = opendir(receive_dir.c_str());
    if (dir == NULL && ENOENT == errno) {
        std::cout << "open dir failed." << std::endl;
        // create the folder
        mkdir(receive_dir.c_str(), 0777);
    } else {
        closedir(dir);
    }

    std::string out_file_path = receive_dir + "/" + filename;
    std::ofstream out_file(out_file_path, std::ios::binary | std::ios::out | std::ios::trunc);
    char* msg_buffer = new char[FILE_TRANSFER_PACKET_SIZE];
    // send "start" to unblock file_client to start sending file data.
    send(socket_fd, "start", 2, 0);
    while (true) {
        memset(msg_buffer, 0, FILE_TRANSFER_PACKET_SIZE);
        int received = recv(socket_fd, msg_buffer, FILE_TRANSFER_PACKET_SIZE, 0);
        if (received <= 0) {
            break;
        }
        out_file.write(msg_buffer, received);
    }
    delete[] msg_buffer;
    out_file.close();
    close(socket_fd);
    CPeerClient::getInstance()->SetFileInfoDirty();
}

void CPeerFileServer::SendFile(int socket_fd, const std::string& filename){
    printf("SendFile (%s)\n", filename.c_str());
    std::string filePath = std::string(PEER_FILE_DIR) + "/" + filename;
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) {
        printf("[PeerServer]Error: cannot open file %s\n", filePath.c_str());
        return;
    }
    
    size_t read_size;
    char* msg_buffer = new char[FILE_TRANSFER_PACKET_SIZE];
    while(true) {
        memset(msg_buffer, 0, FILE_TRANSFER_PACKET_SIZE);
        read_size = fread(msg_buffer, 1, FILE_TRANSFER_PACKET_SIZE, fp);

        int bytes_sent = 0;
        while (bytes_sent < read_size) {
            int sent = send(socket_fd, msg_buffer + bytes_sent, read_size - bytes_sent, 0);
            if (sent <= 0) {
                printf("[PeerServer]Error sending data, errno:%d\n", errno);
                break;
            }
            bytes_sent += sent;
        }

        if (read_size < FILE_TRANSFER_PACKET_SIZE) {
            if(feof(fp)) {
                printf("[PeerServer]End of file reached\n");
            } else {
                printf("[PeerServer]Error reading file\n");
            }
            shutdown(socket_fd, SHUT_WR);
            break;
        }
    }
    auto now_time = GetTimestamp();
    std::cout << "CPeerFileServer]Peer:" << m_peer_id << " Send file finished:" << filename << " at " << now_time << std::endl;
    delete[] msg_buffer;
    fclose(fp);
}