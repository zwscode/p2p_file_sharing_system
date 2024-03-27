
#include "peer_client.h"
#include "peer_server.h"
#include "peer_file_client.h"

#define QUERY_CACHE_SIZE_CHECK_INTERVAL 100
// seconds
#define QUERY_CACHE_TIME_OUT 120

// how long the query result is considered valid, seconds
#define QUERY_HIT_RESULTS_TIME_OUT 120

// how long obtain waits for a query
#define OPERATION_WAIT_QUERY_MS 3000

CPeerClient* getPeerClientInstance() {
    return CPeerClient::getInstance();
}

CPeerClient::CPeerClient() {
    m_msg_id_counter = 0;
    m_send_buffer = new char[MSG_BUF_SIZE];
}

CPeerClient::~CPeerClient() {
    delete[] m_send_buffer;
}

void CPeerClient::LoadFileInfo() {
    // scan file directory, record file info
    DIR* dir = opendir(PEER_FILE_DIR);
    if (dir == NULL && ENOENT == errno) {
        std::cout << "open dir failed." << std::endl;
        // create the folder
        mkdir(PEER_FILE_DIR, 0777);
        dir = opendir(PEER_FILE_DIR);
    }
    struct dirent* file_entry;
    while((file_entry = readdir(dir))) {
        if (file_entry->d_type != DT_DIR && m_files.find(file_entry->d_name) == m_files.end()) {
            m_files.insert(file_entry->d_name);
        }
    }
    closedir(dir);
}

void CPeerClient::LoadSettings() {
    std::ifstream file("settings.config");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::vector<std::string> line_vec = SplitString(line, " ");
            if (line_vec[0] == "replication_factor") {
                m_replicatioin_factor = std::stoi(line_vec[1]);
            } else if (line_vec[0] == "server_ip") {
                m_register_server_ip = line_vec[1];
            } else if (line_vec[0] == "ensure_query_hit_file") {
                m_ensure_queryhit_file = line_vec[1];
            } else if (line_vec[0] == "large_file_transfer") {
                m_large_file_name = line_vec[1];
            }
        }
        printf("replication_factor:%d\n", m_replicatioin_factor);
        printf("server_ip:%s\n", m_register_server_ip.c_str());
        file.close();
    } else {
        std::cout << "Failed to load settings.config." << std::endl;
        exit(-1);
    }
}

void CPeerClient::LoadTopology() {
    std::string topology_file_name;
    if (m_topology_type == ETopologyType::TEST) {
        topology_file_name = "top_test.config";
    } else if (m_topology_type == ETopologyType::STAR) {
        topology_file_name = "top_star.config";
    } else if (m_topology_type == ETopologyType::MESH_2D){
        topology_file_name = "top_mesh_2d.config";
    } else {
        assert(false);
    }
    std::ifstream file(topology_file_name);
    std::string delem = ":";
    size_t pos;
    if (file.is_open()) {
        std::string line;
        int peer_id;
        while (std::getline(file, line)) {
            pos = line.find_first_of(delem);
            peer_id = std::stoi(line.substr(0, pos));
            if (peer_id != m_peer_id) {
                continue;
            }
            std::string peer_list = line.substr(pos + 1);
            printf("peer_list:%s\n", peer_list.c_str());
            std::vector<std::string> peers = SplitString(peer_list, " ", -1);
            for (auto& connected_peer : peers) {
                if (!IsNumber(connected_peer)) {
                    continue;
                }
                printf("LoadTopology connected_peer:%s\n", connected_peer.c_str());
                m_peer_id_to_ip[std::stoi(connected_peer)] = "";
            }
            break;
        }
        file.close();
    } else {
        std::cout << "Failed to open the file." << std::endl;
    }
    // make sure peer is connected to at least one peer
    assert(m_peer_id_to_ip.size() > 0);
    printf("Topology loaded.\n");
    for(auto& peer_pair : m_peer_id_to_ip) {
        std::cout << "peer_id: " << peer_pair.first << ", peer_ip: " << peer_pair.second << std::endl;
    }
}

bool CPeerClient::HasFile(const std::string& filename) {
    return m_files.find(filename) != m_files.end();
}

void CPeerClient::Run(ETopologyType top, PeerID peer_id) {
    m_topology_type = top;
    m_peer_id = peer_id;
    LoadSettings();
    InitPeerFileClient();
    LoadFileInfo();
    LoadTopology();
    Register();

    std::thread mantain_query_cache_thread(&CPeerClient::MantainQueryMapSize, this);
    mantain_query_cache_thread.detach();

    std::thread handle_message_thread(&CPeerClient::HandleMessageThread, this);
    handle_message_thread.detach();

    std::thread broadcast_thread(&CPeerClient::BroadcastProcessThread, this);
    broadcast_thread.detach();

    std::thread replicate_thread(&CPeerClient::StartReplicateAllFiles, this);
    replicate_thread.detach();

    std::string user_command;
    std::string command_type;
    char user_command_char[100];
    SMessageID message_id;
    size_t command_len = 0;
    int con_test_query_max_peer = 9;
    while (true) {
        // do interactive stuff
        memset(user_command_char, 0, sizeof(user_command_char));
        std::cout << "Please input command: \n";
        fgets(user_command_char, sizeof(user_command_char), stdin);
        user_command = std::string(user_command_char);
        user_command = user_command.substr(0, user_command.find_last_of("\n"));
        printf("user_command:(%s)\n", user_command.c_str());
        if (!IsValidUserCommand(user_command)) {
            std::cout << "Invalid command:" << user_command << ";" << std::endl;
            continue;
        }
        size_t pos = user_command.find_first_of(" ");
        command_type = user_command.substr(0, pos);
        std::string content = user_command.substr(pos + 1); 
        if (command_type == USER_COMMAND_HELP) {
            PrintHelp();
            continue;
        } else if (command_type == USER_COMMAND_QUERY) {
            SendQuery(content, &message_id);
        } else if (command_type == USER_COMMAND_OBTAIN) {
            ObtainFile(content, &message_id);
        } else if (command_type == USER_COMMAND_INV_QUERY){ 
            SendInvQuery(content, &message_id);
        } else if (command_type == USER_COMMAND_REPLICATE){
            ReplicateFile(content, &message_id);
        } else if (command_type == USER_COMMAND_TEST_QUERY){
            SendTestQuery();
        } else if (command_type == USER_COMMAND_CONCURRENT_TEST_QUERY) {
            try {
                con_test_query_max_peer = std::stoi(content);
            } catch (const std::exception& e) {
                con_test_query_max_peer = 9;
            }
            SendConcurrentTestQuery(con_test_query_max_peer);
        } else if (command_type == USER_COMMAND_TEST_OBTAIN) {
            SendTestObtain();
        } else if (command_type == USER_COMMAND_TEST_OBTAIN2) {
            try {
                con_test_query_max_peer = std::stoi(content);
            } catch (const std::exception& e) {
                con_test_query_max_peer = 9;
            }
            SendTestObtain2(con_test_query_max_peer);
        } else if (command_type == USER_COMMAND_TEST_OBTAIN3) {
            try {
                con_test_query_max_peer = std::stoi(content);
            } catch (const std::exception& e) {
                con_test_query_max_peer = 9;
            }
            SendTestObtain3(con_test_query_max_peer);
        }else if (command_type == USER_COMMAND_CLEAR) {
            system("clear");
        } else {
            std::cout << "Invalid command." << std::endl;
            continue;
        }
    }
    exit(0);
}

void CPeerClient::StartReplicateAllFiles() {
    if (m_replicatioin_factor <= 1) {
        return;
    }
    int sleep_time = GetRandomNumber(1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));

    std::vector<std::string> replicate_filenames;
    // only the first time issuer of the replicate query is responsible for replication of that file
    {
        std::lock_guard<std::mutex> replicate_query_filename_lock(m_replicate_query_filename_mutex);
        for (auto& filename : m_files) {
            if (m_replicate_query_filename.find(filename) != m_replicate_query_filename.end()) {
                continue;
            }
            replicate_filenames.push_back(filename);
        }
    }
    for (auto& filename : replicate_filenames) {
        SMessageID message_id;
        ReplicateFile(filename, &message_id);
    }
}

void CPeerClient::SetFileInfoDirty() {
    std::lock_guard<std::mutex> file_info_lock(m_file_info_mutex);
    m_file_info_dirty = true;
}

void CPeerClient::UpdateFileInfoPeriodically() {
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        std::lock_guard<std::mutex> file_info_lock(m_file_info_mutex);
        if (m_file_info_dirty) {
            m_file_info_dirty = false;
            LoadFileInfo();
        }
    }
}

void CPeerClient::MantainQueryMapSize() {
    std::vector<SMessageID> to_remove;
    to_remove.reserve(1000);
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(QUERY_CACHE_SIZE_CHECK_INTERVAL));
        // if query cache size is less than 1000 * time_out_seconds, don't remove
        if (m_queries.size() < QUERY_CACHE_TIME_OUT * 1000) {
            continue;
        }
        to_remove.clear();
        auto now_timestamp = GetTimestamp();
        {
            std::lock_guard<std::mutex> queries_map_lock(queries_map_mutex);
            // remove old queries
            for (auto it = m_queries.begin(); it != m_queries.end(); it++) {
                if (now_timestamp - it->first.timestamp > QUERY_CACHE_TIME_OUT * 1000) {
                    to_remove.push_back(it->first);
                }
            }
            if (!to_remove.empty()) {
                for (auto& message_id : to_remove) {
                m_queries.erase(message_id);
            }
            }
            to_remove.clear();
        }
        
        {
            // remove old queryhit results
            std::lock_guard<std::mutex> query_hit_result_lock(m_query_hit_result_mutex);
            for (auto it = m_query_hit_result.begin(); it != m_query_hit_result.end(); it++) {
                if (now_timestamp - it->first.timestamp > QUERY_HIT_RESULTS_TIME_OUT * 1000) {
                    to_remove.push_back(it->first);
                }
            }
            if (!to_remove.empty()) {
                for (auto& message_id : to_remove) {
                    m_query_hit_result.erase(message_id);
                }
            }
            to_remove.clear();
        }

        {
            // remove old wait queryhit entries
            std::lock_guard<std::mutex> query_hit_result_lock(m_wait_queryhit_mutex);
            for (auto it = m_wait_queryhit.begin(); it != m_wait_queryhit.end(); it++) {
                if (now_timestamp - it->first.timestamp > QUERY_HIT_RESULTS_TIME_OUT * 1000) {
                    to_remove.push_back(it->first);
                }
            }
            if (!to_remove.empty()) {
                for (auto& message_id : to_remove) {
                    m_wait_queryhit.erase(message_id);
                }
            }
            to_remove.clear();
        }
        
    }
}


void CPeerClient::WaitReplicateFileQueryHit(SMessageID message_id, const std::string& filename) {
    std::this_thread::sleep_for(std::chrono::milliseconds(OPERATION_WAIT_QUERY_MS));
    std::lock_guard<std::mutex> query_hit_result_lock(m_query_hit_result_mutex);
    if (m_query_hit_result.find(message_id) == m_query_hit_result.end()) {
        printf("WaitReplicateFileQueryHit: queryhit not found\n");
        return;
    }
    auto& query_hit_results = m_query_hit_result[message_id];
    int count = 0;
    for (auto& query_hit_result : query_hit_results) {
        if (count >= m_replicatioin_factor - 1) {
            printf("break, count=%d, m_replicate_factor=%d\n", count, m_replicatioin_factor);
            break;
        }
        // start file transfer
        StartPushFile(filename, query_hit_result.server_info);
        count++;
    }
}

void CPeerClient::HandleMessageThread() {
    std::string message;
    while (true) {
        std::unique_lock<std::mutex> lock(m_message_queue_mutex);
        m_message_queue_cv.wait(lock, [this](){return !m_messages_queue.empty();});
        if (m_messages_queue.empty()) {
            continue;
        }
        message = m_messages_queue.front();
        m_messages_queue.pop();
        lock.unlock();

        std::string command = message.substr(0, message.find(PEER_MSG_TYPE_DELIM));
        if (command == PEER_MSG_QUERY) {
            HandleQuery(message);
        } else if (command == PEER_MSG_INV_QUERY) {
            HandleInvQuery(message);
        } else if (command == PEER_MSG_QUERY_HIT) {
            HandleQueryHit(message);
        } else if (command == PEER_MSG_INV_QUERY_HIT) {
            HandleInvQueryHit(message);
        } else if (command == PEER_MSG_CON_TEST_QUERY) {
            HandleConcurrentTestQuery(message);
        } else if (command == PEER_MSG_TEST_OBTAIN2) {
            HandleConcurrentTestObtain(message);
        } else {
            // wrong command
            continue;
        }
    }
}

void CPeerClient::HandleMessage(const std::string& message) {
    std::unique_lock<std::mutex> message_lock(m_message_queue_mutex);
    m_messages_queue.push(message);
    message_lock.unlock();
    m_message_queue_cv.notify_one();
}

void CPeerClient::HandleQuery(const std::string& message) {
    // parse message
    std::vector<std::string> message_vec = SplitString(message, PEER_MSG_TYPE_DELIM);
    // refer to CPeerClient::CreateQueryMessage
    SMessageID message_id;
    message_id.peer_id = std::stoi(message_vec[1]);
    message_id.msg_id = std::stoull(message_vec[2]);
    message_id.timestamp = std::stoll(message_vec[3]);
    
    // record query to m_queries
    if(IsQueryCached(message_id)) {
        // drop message
        printf("IsQueryCached, Peer %d drop queryn", m_peer_id);
        return;
    }
    PeerID upstream_peer_id = std::stoi(message_vec[5]);
    CacheQuery(message_id, upstream_peer_id);

    // check if has file
    if (HasFile(message_vec[6])) {
        // send queryhit
        printf("file found, send queryhit, filename:%s\n", message_vec[6].c_str());
        std::string queryhit_message = CreateQueryHitMessage(message_vec[6], message_id);
        RelayQueryHit(queryhit_message, message_id);
        return;
    }

    // decrease TTL
    int ttl = std::stoi(message_vec[4]);
    ttl--;
    if (ttl <= 0) {
        // drop message
        std::cout << "ttl <= 0, drop message\n" << std::endl;
        return;
    }

    // modify the message to before relay the message
    // if TTL > 0, send query to other peers
    message_vec[4] = std::to_string(ttl);
    // change sender peer id to current peerServer's peer id
    message_vec[5] = std::to_string(m_peer_id);
    std::string new_message = JoinString(message_vec, PEER_MSG_TYPE_DELIM);
    RelayQuery(new_message, upstream_peer_id);
}

void CPeerClient::HandleConcurrentTestQuery(const std::string& message) {
    // parse message
    std::vector<std::string> message_vec = SplitString(message, PEER_MSG_TYPE_DELIM);
    // refer to CPeerClient::CreateQueryMessage
    SMessageID message_id;
    message_id.peer_id = std::stoi(message_vec[1]);
    message_id.msg_id = std::stoull(message_vec[2]);
    message_id.timestamp = std::stoll(message_vec[3]);
    // record query to m_queries
    if(IsQueryCached(message_id)) {
        // drop message
        printf("IsQueryCached, Peer %d drop queryn", m_peer_id);
        return;
    }
    PeerID upstream_peer_id = std::stoi(message_vec[5]);
    CacheQuery(message_id, upstream_peer_id);
    // start concurrent test query
    if (m_peer_id < std::stoi(message_vec[6])) {
        SendTestQuery();
    }
    // decrease TTL
    int ttl = std::stoi(message_vec[4]);
    ttl--;
    if (ttl <= 0) {
        // drop message
        std::cout << "ttl <= 0, drop message\n" << std::endl;
        return;
    }

    // modify the message to before relay the message
    // if TTL > 0, send query to other peers
    message_vec[4] = std::to_string(ttl);
    // change sender peer id to current peerServer's peer id
    message_vec[5] = std::to_string(m_peer_id);
    std::string new_message = JoinString(message_vec, PEER_MSG_TYPE_DELIM);
    RelayQuery(new_message, upstream_peer_id);
}

void CPeerClient::HandleConcurrentTestObtain(const std::string& message) {
    // parse message
    std::vector<std::string> message_vec = SplitString(message, PEER_MSG_TYPE_DELIM);
    // refer to CPeerClient::CreateQueryMessage
    SMessageID message_id;
    message_id.peer_id = std::stoi(message_vec[1]);
    message_id.msg_id = std::stoull(message_vec[2]);
    message_id.timestamp = std::stoll(message_vec[3]);
    // record query to m_queries
    if(IsQueryCached(message_id)) {
        // drop message
        printf("IsQueryCached, Peer %d drop queryn", m_peer_id);
        return;
    }
    PeerID upstream_peer_id = std::stoi(message_vec[5]);
    CacheQuery(message_id, upstream_peer_id);
    // start concurrent testObtain
    if (m_peer_id < std::stoi(message_vec[6])) {
        if (message_vec[7] == "2") {
            SendTestObtain();
        } else {
            SendTestObtainLarge();
        }
    }
    // decrease TTL
    int ttl = std::stoi(message_vec[4]);
    ttl--;
    if (ttl <= 0) {
        // drop message
        std::cout << "ttl <= 0, drop message\n" << std::endl;
        return;
    }

    // modify the message to before relay the message
    // if TTL > 0, send query to other peers
    message_vec[4] = std::to_string(ttl);
    // change sender peer id to current peerServer's peer id
    message_vec[5] = std::to_string(m_peer_id);
    std::string new_message = JoinString(message_vec, PEER_MSG_TYPE_DELIM);
    RelayQuery(new_message, upstream_peer_id);
}

void CPeerClient::HandleInvQuery(const std::string& message) {
    std::vector<std::string> message_vec = SplitString(message, PEER_MSG_TYPE_DELIM);
    SMessageID message_id;
    message_id.peer_id = std::stoi(message_vec[1]);
    message_id.msg_id = std::stoull(message_vec[2]);
    message_id.timestamp = std::stoll(message_vec[3]);
    if(IsQueryCached(message_id)) {
        // drop message
        printf("IsQueryCached, Peer %d drop queryn", m_peer_id);
        return;
    }
    PeerID upstream_peer_id = std::stoi(message_vec[5]);
    CacheQuery(message_id, upstream_peer_id);
    int replicate_factor = std::stoi(message_vec[7]);
    
    std::string filename = message_vec[6];
    // only the first time issuer of the replicate query is responsible for replication of that file
    {
        std::lock_guard<std::mutex> replicate_query_filename_lock(m_replicate_query_filename_mutex);
        // only pass replicate query of a filename once
        if (m_replicate_query_filename.find(filename) != m_replicate_query_filename.end()) {
            return;
        }
        m_replicate_query_filename.insert(filename);
    }
    // if do not have file, send invQuearyHit
    if (!HasFile(filename)) {
        std::string inv_query_hit_message = CreateInvQueryHitMessage(filename, message_id);
        RelayQueryHit(inv_query_hit_message, message_id);
    }
    // decrease replicate factor
    replicate_factor--;
    if (replicate_factor <= 0) {
        // drop message
        std::cout << "replicate_factor <= 0, drop message\n" << std::endl;
        return;
    }
    // decrease TTL
    int ttl = std::stoi(message_vec[4]);
    ttl--;
    if (ttl <= 0) {
        // drop message
        std::cout << "ttl <= 0, drop message\n" << std::endl;
        return;
    }
    message_vec[4] = std::to_string(ttl);
    message_vec[5] = std::to_string(m_peer_id);
    message_vec[7] = std::to_string(replicate_factor);
    std::string new_message = JoinString(message_vec, PEER_MSG_TYPE_DELIM);
    RelayQuery(new_message, upstream_peer_id);
}

void CPeerClient::HandleWaitQueryHit(const std::string& message_type, const SMessageID& message_id) {
    std::string filename;
    // lock_guard scope
    {
        std::lock_guard<std::mutex> wait_queryhit_mutex_lock(m_wait_queryhit_mutex);
        if (m_wait_queryhit.find(message_id) == m_wait_queryhit.end()) {
            printf("HandleWaitQueryHit: queryhit not found\n");
            return;
        }
        filename = m_wait_queryhit[message_id];
        printf("HandleWaitQueryHit filename=%s\n", filename.c_str());
        // delete wait queryhit, to avoid duplicate startFileTransfer
        m_wait_queryhit.erase(message_id);
    }

    // go through all the queryhits in m_query_hit_result
    std::lock_guard<std::mutex> query_hit_result_lock(m_query_hit_result_mutex);
    if (m_query_hit_result.find(message_id) == m_query_hit_result.end()) {
        printf("WaitObtainFileQueryHit: queryhit not found\n");
        return;
    }
    auto& query_hit_results = m_query_hit_result[message_id];
    if (message_type == PEER_MSG_QUERY_HIT) {
        auto& elem = query_hit_results.front();
        StartFileTransfer(filename, elem.server_info);
    } else if (message_type == PEER_MSG_INV_QUERY_HIT) {
        // PEER_MSG_INV_QUERY_HIT
        int count = 0;
        for (auto& elem : query_hit_results) {
            if (count >= m_replicatioin_factor - 1) {
                break;
            }
            StartPushFile(filename, elem.server_info);
            count++;
        }
    }
    
}

void CPeerClient::HandleQueryHit(const std::string& message) {
    // parse message
    std::vector<std::string> message_vec = SplitString(message, PEER_MSG_TYPE_DELIM);
    // lookup m_queries find the upstream peer_id
    SMessageID message_id;
    try {
        message_id.peer_id = std::stoi(message_vec[1]);
        message_id.msg_id = std::stoull(message_vec[2]);
        message_id.timestamp = std::stoll(message_vec[3]);
    } catch (const std::exception& e) { 
        printf("HandleQueryHit: stoi error\n");
        return;
    }
    std::string message_type = message_vec[0];
    printf("On queryhit/invqueryhit:%s  received, peer_id:%d, msg_id:%d\n", message_type.c_str(), message_id.peer_id, (int)message_id.msg_id);
    if (message_id.peer_id == m_peer_id) {
        // this client is the originator of the query
        // send file transfer request to the peer file server
        if (!IsQueryCached(message_id)) {
            // when query is not cached, this means it is timed out,  ignore queryhit message
            printf("query is not caches, ignore queryhit\n");
            return;
        }
        PeerID originator_peer_id = std::stoi(message_vec[5]);
        std::cout << "query/invquery originator peer " << m_peer_id << " received quryhit/invqueryhit, from target peer " << originator_peer_id << "!" << std::endl;
        auto now_timestamp = GetTimestamp();
        std::cout << "now timestamp:" << now_timestamp << ", latency: " << now_timestamp - message_id.timestamp << std::endl;
        int port = GetFileServerPort(originator_peer_id);
        SPeerServerInfo peer_server_info {
            message_vec[6],     // originator_peer's ip
            port
        };
        // lock_guard scope
        {
            std::lock_guard<std::mutex> query_hit_result_lock(m_query_hit_result_mutex);
            if (m_query_hit_result.find(message_id) == m_query_hit_result.end()) {
                m_query_hit_result[message_id] = {};
            }
            auto& query_hit_list = m_query_hit_result[message_id];
            query_hit_list.push_back({peer_server_info, now_timestamp});
        }
        
        // if queryhit find the query initiator, don't relay queryhit.
        HandleWaitQueryHit(message_type, message_id);
        // when queryhit find the query initiator, don't relay queryhit.
        return;
    }

    // decrease TTL
    int ttl = std::stoi(message_vec[4]);
    ttl--;
    if (ttl <= 0) {
        // drop message
        std::cout << "ttl <= 0, drop message\n" << std::endl;
        return;
    }
    std::string new_message = JoinString(message_vec, PEER_MSG_TYPE_DELIM);
    // send queryhit to upstream peer recored in peerClient.m_queries
    RelayQueryHit(new_message, message_id);
}


void CPeerClient::HandleInvQueryHit(const std::string& message) {
   HandleQueryHit(message);
}

void CPeerClient::ObtainFile(const std::string& filename, SMessageID* message_id) {
    // todo zws uncomment this after testing
    // if (HasFile(filename)) {
    //     std::cout << "File exists locally, abort obtainFile." << std::endl;
    //     return;
    // }
    SendQuery(filename, message_id);
    SetWaitQueryHit(*message_id, filename);
}

void CPeerClient::ReplicateFile(const std::string& filename, SMessageID* message_id) {
    if (!HasFile(filename)) {
        std::cout << "File don't exists." << std::endl;
        return;
    }
    SendInvQuery(filename, message_id);
    SetWaitQueryHit(*message_id, filename);
}


void CPeerClient::StartPushFile(const std::string& filename, const SPeerServerInfo& server_info) {
    if (!HasFile(filename)) {
        std::cout << "File don't exist." << std::endl;
        return;
    }
    printf("StartPushFile: filename: %s, ip: %s, port: %d\n", filename.c_str(), server_info.ip.c_str(), server_info.port);
    CPeerFileClient::getInstance()->PushFile(filename, server_info.ip, server_info.port);
}

void CPeerClient::StartFileTransfer(const std::string& filename, const SPeerServerInfo& server_info) {
    printf("StartFileTransfer: receiver peerid:%d filename: %s, ip: %s, port: %d\n", m_peer_id, filename.c_str(), server_info.ip.c_str(), server_info.port);
    CPeerFileClient::getInstance()->ObtainFile(filename, server_info.ip, server_info.port);
}

void CPeerClient::SetWaitQueryHit(SMessageID message_id, const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_wait_queryhit_mutex);
    m_wait_queryhit[message_id] = filename;
}

void CPeerClient::SendQuery(const std::string& filename, SMessageID* message_id) {
    // generate message, broadcast and wait for queryhit
    std::string msg = CreateQueryMessage(filename, message_id);
    CacheQuery(*message_id, -1);
    Broadcast(msg);
}

void CPeerClient::SendTestQuery() {
    auto now_time = GetTimestamp();
    std::cout << "m_peer_id:" << m_peer_id << ", SendTestQuery, now_stamp:" << now_time << std::endl;
    std::string filename;
    SMessageID message_id;
    for (int i = 0; i < TEST_QUERY_COUNT; i++) {
        filename = std::to_string(i);
        SendQuery(filename, &message_id);
    }
    // last query is a query to known file to ensure a queryhit
    SendQuery(m_ensure_queryhit_file, &message_id);
}

void CPeerClient::SendTestObtain() {
    auto now = GetTimestamp();
    std::cout << "SendTestObtain, now:" << now << std::endl;
    std::string filename = m_ensure_queryhit_file;
    SMessageID message_id;
    for (int i = 0; i < TEST_OBTAIN_COUNT; i++) {
        ObtainFile(filename, &message_id);
    }
}

void CPeerClient::SendTestObtainLarge() {
    auto now = GetTimestamp();
    std::cout << "SendTestObtain, now:" << now << std::endl;
    std::string filename = m_large_file_name;
    SMessageID message_id;
    for (int i = 0; i < TEST_OBTAIN_LARGE_COUNT; i++) {
        ObtainFile(filename, &message_id);
    }
}


std::vector<std::string> CPeerClient::CreateCommonQueryMsgVec(const std::string& msg_type, SMessageID* message_id) {
    std::lock_guard<std::mutex> lock(m_msg_id_counter_mutex);
    message_id->peer_id = m_peer_id;
    message_id->msg_id = m_msg_id_counter;
    message_id->timestamp = GetTimestamp();
    std::vector<std::string> msg_vec = {
        msg_type,   // 0
        std::to_string(m_peer_id),  // 1
        std::to_string(m_msg_id_counter),   // 2
        std::to_string(message_id->timestamp), // 3
        std::to_string(TTL),    // 4
        // upstream peer_id
        std::to_string(m_peer_id),  // 5
    };
    m_msg_id_counter++;
    return msg_vec;
}

void CPeerClient::SendConcurrentTestQuery(int requirement_peer_id) {
    auto now = GetTimestamp();
    std::cout << "SendConcurrentTestQuery, now:" << now << std::endl;
    SMessageID message_id;
    auto msg_vec = CreateCommonQueryMsgVec(PEER_MSG_CON_TEST_QUERY, &message_id);
    msg_vec.push_back(std::to_string(requirement_peer_id)); // 6

    CacheQuery(message_id, -1);
    std::string msg = JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
    Broadcast(msg);
    // initiator of concurrent testquery will do testquery
    SendTestQuery();
}

// test concurrent obtain
void CPeerClient::SendTestObtain2(int requirement_peer_id) {
    auto now = GetTimestamp();
    std::cout << "SendTestObtain2, now:" << now << std::endl;
    SMessageID message_id;
    auto msg_vec = CreateCommonQueryMsgVec(PEER_MSG_TEST_OBTAIN2, &message_id);
    msg_vec.push_back(std::to_string(requirement_peer_id)); // 6
    msg_vec.push_back(std::to_string(2)); // obtain2
    CacheQuery(message_id, -1);
    std::string msg = JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
    Broadcast(msg);
    // initiator of concurrent testobtain will do testquery
    SendTestObtain();
}

void CPeerClient::SendTestObtain3(int requirement_peer_id) {
    auto now = GetTimestamp();
    std::cout << "SendTestObtain3, now:" << now << std::endl;
    SMessageID message_id;
    auto msg_vec = CreateCommonQueryMsgVec(PEER_MSG_TEST_OBTAIN2, &message_id);
    msg_vec.push_back(std::to_string(requirement_peer_id)); // 6
    msg_vec.push_back(std::to_string(3)); // obtain3
    CacheQuery(message_id, -1);
    std::string msg = JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
    Broadcast(msg);
    // initiator of concurrent testobtain will do testquery
    SendTestObtainLarge();

}

void CPeerClient::SendInvQuery(const std::string& filename, SMessageID* message_id) {
    if (m_replicatioin_factor <= 1) {
        printf("SendInvQuery: replication_factor <= 1, don't send inv_query\n");
        return;
    }
    printf("SendInvQuery\n");
    std::string msg = CreateInvQueryMessage(filename, message_id);
    CacheQuery(*message_id, -1);
    Broadcast(msg);
}


void CPeerClient::CacheQuery(const SMessageID& message_id, PeerID upstream_peer_id) {
    std::lock_guard<std::mutex> lock(queries_map_mutex);
    if (m_queries.find(message_id) != m_queries.end()) {
        return;
    }
    m_queries[message_id] = upstream_peer_id;
}

bool CPeerClient::IsQueryCached(const SMessageID& message_id) {
    std::lock_guard<std::mutex> lock(queries_map_mutex);
    if (m_queries.find(message_id) != m_queries.end()) {
        return true;
    }
    return false;
}


void CPeerClient::RelayQuery(const std::string& message, PeerID exclude_peer_id) {
    // relay query to all peers except the one who sent the query
    printf("RelayQuery exclude_peer_id:%d\n", exclude_peer_id);
    Broadcast(message, exclude_peer_id);
}

void CPeerClient::InitPeerFileClient() {
    CPeerFileClient::getInstance()->Init(m_peer_id);
}

void CPeerClient::Register() {
    // register to register server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cout << "ERROR opening socket" << std::endl;
        exit(1);
    }
    // set server_addr
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr= inet_addr(m_register_server_ip.c_str());
    server_addr.sin_port = htons(REGISTER_SERVER_PORT);

    // connect
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cout << "ERROR connecting" << std::endl;
        close(sockfd);
        exit(1);
    }
    // send peer_id
    int buffer_size = MSG_BUF_SIZE;
    char* message = new char[buffer_size];
    sprintf(message, "%s%s%d", REGISTER_REQUEST_REGISTER, PEER_MSG_TYPE_DELIM, m_peer_id);
    
    if (send(sockfd, message, buffer_size, 0) < 0) {
        std::cout << "ERROR send message" << std::endl;
        close(sockfd);
        exit(1);
    }
    printf("message sent, len:%ld, msg:%s\n", strlen(message), message);
    // receive response
    memset(message, 0, buffer_size);
    if (recv(sockfd, message, buffer_size, 0) < 0) {
        std::cout << "ERROR recv from socket" << std::endl;
        close(sockfd);
        exit(1);
    }
    // handle response
    HandleRegisterResponse(message);
    delete[] message;
    // close socket
    close(sockfd);
}

void CPeerClient::ConnectToPeers() {
    for (auto& peer_info : m_peer_id_to_ip) {
        // make sure don't connect to self
        if (peer_info.first == m_peer_id) {
            continue;
        }

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cout << "ERROR opening socket" << std::endl;
            exit(1);
        }
        // set server_addr
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr= inet_addr(peer_info.second.c_str());
        server_addr.sin_port = htons(GetPeerServerPort(peer_info.first));
        printf("Connect to peerServer %d\n", peer_info.first);
        // connect
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cout << "ERROR connecting" << std::endl;
            close(sockfd);
            exit(1);
        }
        m_peer_id_to_sockfd[peer_info.first] = sockfd;
    }
}

void CPeerClient::Broadcast(const std::string& message, PeerID exclude_peer_id) {
    std::unique_lock<std::mutex> lock(m_broadcast_queue_mutex);
    m_broadcast_queue.push({message, exclude_peer_id});
    lock.unlock();
    m_broadcast_queue_cv.notify_one();
}

void CPeerClient::BroadcastProcessThread() {
    std::string message;
    PeerID exclude_peer_id;
    char* msg_buffer = new char[MSG_BUF_SIZE];
    int sentBytes;
    int sent;
    int64_t now_time;
    while(true) {
        std::unique_lock<std::mutex> lock(m_broadcast_queue_mutex);
        m_broadcast_queue_cv.wait(lock, [this] {return !m_broadcast_queue.empty();});
        if (m_broadcast_queue.empty()) {
            continue;
        }
        auto& val_pair = m_broadcast_queue.front();
        message = val_pair.first;
        exclude_peer_id = val_pair.second;
        m_broadcast_queue.pop();
        lock.unlock();

        std::lock_guard<std::mutex> lock_send_buf(m_send_buffer_mutex);
        now_time = GetTimestamp();
        std::cout << "PeerID:" << m_peer_id << ",now:" << now_time << ", Broadcasting message: \n{" << message << "}" << std::endl;
        // don't broadcast message back to sender
        for (auto& peer_sockfd : m_peer_id_to_sockfd) {
            if (exclude_peer_id == peer_sockfd.first) {
                continue;
            }
            memset(msg_buffer, 0, MSG_BUF_SIZE);
            memcpy(msg_buffer, message.c_str(), message.length());
            sentBytes = 0;
            while(sentBytes < MSG_BUF_SIZE) {
                sent = send(peer_sockfd.second, msg_buffer + sentBytes, MSG_BUF_SIZE - sentBytes, 0);
                if (sent < 0) {
                    std::cout << "ERROR send message" << std::endl;
                    break;
                }
                sentBytes += sent;
            }
        }
    }
    delete[] msg_buffer;
}

std::string CPeerClient::CreateQueryMessage(const std::string& filename, SMessageID* message_id) {
    std::lock_guard<std::mutex> lock(m_msg_id_counter_mutex);
    message_id->peer_id = m_peer_id;
    message_id->msg_id = m_msg_id_counter;
    message_id->timestamp = GetTimestamp();
    std::vector<std::string> msg_vec = {
        PEER_MSG_QUERY,   // 0
        std::to_string(m_peer_id),  // 1
        std::to_string(m_msg_id_counter),   // 2
        std::to_string(message_id->timestamp), // 3
        std::to_string(TTL),    // 4
        // upstream peer_id
        std::to_string(m_peer_id),  // 5
        filename    // 6
    };
    m_msg_id_counter++;
    return JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
}

std::string CPeerClient::CreateInvQueryMessage(const std::string& filename, SMessageID* message_id) {
    std::lock_guard<std::mutex> lock(m_msg_id_counter_mutex);
    message_id->peer_id = m_peer_id;
    message_id->msg_id = m_msg_id_counter;
    message_id->timestamp = GetTimestamp();
    std::vector<std::string> msg_vec = {
        PEER_MSG_INV_QUERY,   // 0
        std::to_string(m_peer_id),  // 1
        std::to_string(m_msg_id_counter),   // 2
        std::to_string(message_id->timestamp), // 3
        std::to_string(TTL),    // 4
        // upstream peer_id
        std::to_string(m_peer_id),  // 5
        filename,    // 6
        std::to_string(m_replicatioin_factor - 1) // 7
    };
    m_msg_id_counter++;
    return JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
}

std::string CPeerClient::CreateQueryHitMessage(const std::string& filename, const SMessageID& message_id) {
    std::string my_ip = m_peer_id_to_ip[m_peer_id];
    std::vector<std::string> msg_vec = {
        PEER_MSG_QUERY_HIT,   // 0
        std::to_string(message_id.peer_id),  // 1
        std::to_string(message_id.msg_id),   // 2
        std::to_string(message_id.timestamp), // 3
        std::to_string(TTL),    // 4
        // queryhit originator's peer_id
        std::to_string(m_peer_id),  // 5
        // originator's ip
        my_ip,  // 6
        filename    // 7
    };
    return JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
}

std::string CPeerClient::CreateInvQueryHitMessage(const std::string& filename, const SMessageID& message_id) {
    std::string my_ip = m_peer_id_to_ip[m_peer_id];
    std::vector<std::string> msg_vec = {
        PEER_MSG_INV_QUERY_HIT,   // 0
        std::to_string(message_id.peer_id),  // 1
        std::to_string(message_id.msg_id),   // 2
        std::to_string(message_id.timestamp), // 3
        std::to_string(TTL),    // 4
        // queryhit originator's peer_id
        std::to_string(m_peer_id),  // 5
        // originator's ip
        my_ip,  // 6
    };
    return JoinString(msg_vec, PEER_MSG_TYPE_DELIM);
}

void CPeerClient::RelayQueryHit(const std::string& message, const SMessageID& message_id) {
    std::lock_guard<std::mutex> lock(m_send_buffer_mutex);
    // only send message to recored upstream peer of the query
    PeerID upstream_peer_id = m_queries[message_id];
    printf("RelayQueryHit to upstream_peer_id:%d\n", upstream_peer_id);
    assert(m_peer_id_to_sockfd.find(upstream_peer_id) != m_peer_id_to_sockfd.end());
    int sock_fd = m_peer_id_to_sockfd[upstream_peer_id];
    memset(m_send_buffer, 0, MSG_BUF_SIZE);
    memcpy(m_send_buffer, message.c_str(), message.length());
    int sentBytes = 0;
    int sent = 0;
    while(sentBytes < MSG_BUF_SIZE) {
        sent = send(sock_fd, m_send_buffer + sentBytes, MSG_BUF_SIZE - sentBytes, 0);
        if (sent < 0) {
            std::cout << "ERROR send message" << std::endl;
            break;
        }
        sentBytes += sent;
    }
}

bool CPeerClient::IsValidUserCommand(const std::string& command) const {
    size_t spacePos = command.find(' ');
    std::string command_type = command.substr(0, spacePos);
    bool result = false;
    if (command_type == USER_COMMAND_HELP) {
        result = true;
    } else if (command_type == USER_COMMAND_QUERY) {
        result = true;
    } else if (command_type == USER_COMMAND_OBTAIN) {
        result = true;
    } else if (command_type == USER_COMMAND_INV_QUERY){
        result = true;
    } else if (command_type == USER_COMMAND_REPLICATE){
        result = true;
    } else if (command_type == USER_COMMAND_TEST_QUERY){
        result = true;
    } else if (command_type == USER_COMMAND_CONCURRENT_TEST_QUERY) {
        result = true;
    } else if (command_type == USER_COMMAND_TEST_OBTAIN) {
        result = true;
    } else if (command_type == USER_COMMAND_TEST_OBTAIN2) {
        result = true;
    } else if (command_type == USER_COMMAND_TEST_OBTAIN3) {
        result = true;
    }
    else if (command_type == USER_COMMAND_CLEAR) {
        result = true;
    } else {
        result = false;
    }
    return result;
}

void CPeerClient::PrintHelp() const {
    std::cout << "Supported user commands:" << std::endl;
    std::cout << "help: print all commands" << std::endl;
    std::cout << "query {filename}: query about a file" << std::endl;
    std::cout << "obtain {filename}: query and download a file" << std::endl;
    std::cout << "test_query: test issuing 10k queries" << std::endl;
    std::cout << "test_query2 {max_peer_id}: test concurrent issuing 10k queries, all peers whose peer_id < max_peer_id will do test_query" << std::endl;
    std::cout << "test_obtain: test issuing 10k obtain requests" << std::endl;
    std::cout << "test_obtain2 {max_peer_id}: test concurrent issuing 10k obtain requests, all peers whose peer_id < max_peer_id will do test_obtain" << std::endl;
}

void CPeerClient::HandleRegisterResponse(char* message) {
    // {message_type}\n{actual_message}
    std::string response(message);
    std::string delimiters = "\n";
    size_t pos = response.find_first_of(delimiters);
    std::string response_type = response.substr(0, pos);
    std::string actual_message = response.substr(pos + 1);
    std::cout << "client register response:\n" << message << std::endl;
    if (response_type == REGISTER_RESPONSE_ALL_PEERS) {
        std::vector<std::string> messages = SplitString(actual_message, "\n", -1);
        for (auto& peer_info : messages) {
            pos = peer_info.find_first_of(" ");
            PeerID peer_id = std::stoi(peer_info.substr(0, pos));
            if (m_peer_id_to_ip.find(peer_id) == m_peer_id_to_ip.end() && peer_id != m_peer_id) {
                // peer is not reachable by this client
                continue;
            }
            std::string peer_ip = peer_info.substr(pos + 1);
            m_peer_id_to_ip[peer_id] = peer_ip;
        }
        std::cout << "client receive msg that all peers registered." << std::endl;
        for (auto& peer_pair : m_peer_id_to_ip) {
            std::cout << "peer_id: " << peer_pair.first << ", peer_ip: " << peer_pair.second << std::endl;
        }
        ConnectToPeers();
    } else {
        std::cout << "Invalid register response type: " << response_type << std::endl;
    }
}