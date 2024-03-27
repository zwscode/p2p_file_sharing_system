#include "peer_file_client.h"
#include "peer_client.h"

std::string GetTempraryDirPath() {
    return std::string(PEER_FILE_DIR) + '/' + std::string(TEMP_SPLIT_FILE_DIR) + '/';
}

std::string CPeerFileClient::CreateRequestHeader(ERequestType req_type, std::string filename) {
    std::string msg;
    msg = std::to_string(static_cast<int>(req_type)) + "\n";
    msg += filename + "\n";
    return msg;
}

void CPeerFileClient::Init(int peer_id) {
    m_peer_id = peer_id;
    // init
    for (int i = 0; i < FILE_CLIENT_THREAD_NUM; i++) {
        std::thread handle_request_thread(&CPeerFileClient::HandleRequestThreadFunc, this);
        handle_request_thread.detach();
    }
}

void CPeerFileClient::HandleRequestThreadFunc() {
    while(true) {
        std::unique_lock<std::mutex> lock(m_request_queue_mutex);
        m_request_queue_cv.wait(lock, [this](){return !m_request_queue.empty();});
        if (m_request_queue.empty()) {
            continue;
        }
        SClientRequest req = m_request_queue.front();
        m_request_queue.pop();
        lock.unlock();
        if (req.req_type == ERequestType::REQUEST_DOWNLOAD_FILE) {
            ExecObtainFile(req.filename, req.ip, req.port);
        } else if (req.req_type == ERequestType::REQUEST_PUSH_FILE) {
            ExecPushFile(req.filename, req.ip, req.port);
        } else {
            printf("[PeerFileClient]Error: invalid request type: %d\n", (int)req.req_type);
        }
    }
}

void CPeerFileClient::PushFile(const std::string& filename, const std::string& ip, int port) {
    std::unique_lock<std::mutex> lock(m_request_queue_mutex);
    SClientRequest req;
    req.req_type = ERequestType::REQUEST_PUSH_FILE;
    req.filename = filename;
    req.ip = ip;
    req.port = port;
    m_request_queue.push(req);
    lock.unlock();
    m_request_queue_cv.notify_one();
}

void CPeerFileClient::ExecPushFile(const std::string& filename, const std::string& ip, int port) {
    printf("[PeerFileClient]PushFile: filename: %s, ip: %s\n", filename.c_str(), ip.c_str());
    if (filename.empty()){
        printf("[PeerFileClient]ExecPushFile Error: filename is empty\n");
        return;
    }
    // set up socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
 
    if (socket_fd < 0) {
        printf("[PeerFileClient]Error: cannot create socket\n");
        exit(1);
    }

    // set socket buffer size
    int buf_size = FILE_TRANSFER_PACKET_SIZE;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }

    // configure server address
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    // connect
    int try_connect_times = 0;
    bool connected = true;
    while(connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        std::cout << "tcp connect socket failed, errno:"<< errno << std::endl;
        try_connect_times++;
        if (try_connect_times > 10) {
            connected = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!connected) {
        printf("[PeerFileClient]Error: cannot connect to server\n");
        close(socket_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PushFile(filename, ip, port);
        return;
    }
    std::string msg;
    msg += CreateRequestHeader(ERequestType::REQUEST_PUSH_FILE, filename);
    // send request
    char* msg_buffer = new char[MSG_BUF_SIZE];
    memset(msg_buffer, 0, MSG_BUF_SIZE);
    memcpy(msg_buffer, msg.c_str(), msg.size());
    int sent = send(socket_fd, msg_buffer, MSG_BUF_SIZE, 0);
    delete[] msg_buffer;
    if (sent <= 0) {
        printf("[PeerFileClient]Error sending data, errno:%d\n", errno);
        close(socket_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PushFile(filename, ip, port);
        return;
    }
    HandlePushResponse(socket_fd, filename);
}

void CPeerFileClient::HandlePushResponse(int socket_fd, const std::string& filename) {
    std::string filePath = std::string(PEER_FILE_DIR) + "/" + filename;
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) {
        printf("[PeerFileClient]Error: cannot open file %s\n", filePath.c_str());
        return;
    }
    size_t read_size;
    char* msg_buffer = new char[FILE_TRANSFER_PACKET_SIZE];
    // block, wait for server to notify client to send file data
    recv(socket_fd, msg_buffer, MSG_BUF_SIZE, 0);
    while(true) {
        memset(msg_buffer, 0, FILE_TRANSFER_PACKET_SIZE);
        read_size = fread(msg_buffer, 1, FILE_TRANSFER_PACKET_SIZE, fp);
        int bytes_sent = 0;
        while (bytes_sent < read_size) {
            int sent = send(socket_fd, msg_buffer + bytes_sent, read_size - bytes_sent, 0);
            if (sent <= 0) {
                printf("[PeerFileClient]Error sending data, errno:%d\n", errno);
                break;
            }
            bytes_sent += sent;
        }

        if (read_size < FILE_TRANSFER_PACKET_SIZE) {
            if(feof(fp)) {
                printf("[PeerFileClient]End of file reached\n");
            } else {
                printf("[PeerFileClient]Error reading file\n");
            }
            shutdown(socket_fd, SHUT_WR);
            break;
        }
    }
    fclose(fp);
    close(socket_fd);
    auto now_time = GetTimestamp();
    std::cout << "[PeerFileClient]Push file: " << filename << " at " << now_time << std::endl;
    delete[] msg_buffer;
}

void CPeerFileClient::ObtainFile(const std::string& filename, const std::string& ip, int port) {
    std::unique_lock<std::mutex> lock(m_request_queue_mutex);
    SClientRequest req;
    req.req_type = ERequestType::REQUEST_DOWNLOAD_FILE;
    req.filename = filename;
    req.ip = ip;
    req.port = port;
    m_request_queue.push(req);
    lock.unlock();
    m_request_queue_cv.notify_one();
}

void CPeerFileClient::ExecObtainFile(const std::string& filename, const std::string& ip, int port) {
    printf("[PeerFileClient]ExecObtainFile: count=%d, filename: %s, ip: %s\n", m_obtain_num, filename.c_str(), ip.c_str());
    // set up socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
 
    if (socket_fd < 0) {
        printf("[PeerFileClient]Error: cannot create socket\n");
        exit(1);
    }

    // set socket buffer size
    int buf_size = FILE_TRANSFER_PACKET_SIZE;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
        printf("[PeerServer]Error: cannot set socket buffer size\n");
        exit(1);
    }

    // configure server address
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    // connect
    int try_connect_times = 0;
    bool connected = true;
    while(connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
        std::cout << "tcp connect socket failed, errno:"<< errno << std::endl;
        try_connect_times++;
        if (try_connect_times > 10) {
            connected = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!connected) {
        printf("[PeerFileClient]Error: cannot connect to server\n");
        close(socket_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ObtainFile(filename, ip, port);
        return;
    }
    std::string msg;
    msg += CreateRequestHeader(ERequestType::REQUEST_DOWNLOAD_FILE, filename);
    // send request
    char* msg_buffer = new char[MSG_BUF_SIZE];
    memset(msg_buffer, 0, MSG_BUF_SIZE);
    memcpy(msg_buffer, msg.c_str(), msg.size());
    int sent = send(socket_fd, msg_buffer, MSG_BUF_SIZE, 0);
    delete[] msg_buffer;
    if (sent <= 0) {
        printf("[PeerFileClient]Error sending data, errno:%d\n", errno);
        close(socket_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ObtainFile(filename, ip, port);
        return;
    }
    HandleObtainResponse(socket_fd, filename);
}

void CPeerFileClient::HandleObtainResponse(int socket_fd, const std::string& filename) {
    std::string receive_dir = std::string(PEER_FILE_DIR) + "/received" + std::to_string(m_peer_id) + "/";

    DIR* dir = opendir(receive_dir.c_str());
    if (dir == NULL && ENOENT == errno) {
        std::cout << "open dir failed." << std::endl;
        // create the folder
        mkdir(receive_dir.c_str(), 0777);
    } else {
        closedir(dir);
    }

    char* msg_buffer = new char[FILE_TRANSFER_PACKET_SIZE];
    std::string out_file_path;
    {
        std::lock_guard<std::mutex> num_lock(m_obtain_num_mutex);
        out_file_path = receive_dir + std::to_string(m_obtain_num) + "_" +filename;
        m_obtain_num++;
    }
    
    std::ofstream out_file(out_file_path, std::ios::binary | std::ios::app);
    auto start_time = GetTimestamp();
    while(true) {
        memset(msg_buffer, 0, FILE_TRANSFER_PACKET_SIZE);
        int bytes_read = recv(socket_fd, msg_buffer, FILE_TRANSFER_PACKET_SIZE, 0);
        if (bytes_read <= 0) {
            printf("[PeerFileClient]bytes_read:%d, break\n", bytes_read);
            break;
        }
        out_file.write(msg_buffer, bytes_read);
    }
    out_file.close();
    close(socket_fd);
    {
        std::lock_guard<std::mutex> num_lock2(m_obtain_num_mutex);
        auto now = GetTimestamp();
        printf("[PeerFileClient]Receive file: %d, time cost: %ld, now:%ld\n", m_obtain_num, now - start_time, now);
    }
    delete[] msg_buffer;
    CPeerClient::getInstance()->SetFileInfoDirty();
}