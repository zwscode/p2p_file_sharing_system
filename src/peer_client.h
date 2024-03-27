#ifndef _PEER_CLIENT_H_
#define _PEER_CLIENT_H_

#include "defines.h"
#include <queue>

/*
The p2p client that sends query
CPeerClient also act as a Manager, CPeerServer can get info from CPeerClient singleton instance
*/
class CPeerClient {
public:
    static CPeerClient* getInstance() {
        static CPeerClient instance;
        return &instance;
    }
    void Run(ETopologyType top, PeerID peer_id);
    
    void ObtainFile(const std::string& filename, SMessageID* message_id);
    void ReplicateFile(const std::string& filename, SMessageID* message_id);
    void StartPushFile(const std::string& filename, const SPeerServerInfo& peer_server_info);
    
    void RelayQuery(const std::string& message, PeerID exclude_peer_id);
    bool HasFile(const std::string& filename);

    void RelayQueryHit(const std::string& message, const SMessageID& message_id);
    void CacheQuery(const SMessageID& message_id, PeerID upstream_peer_id);
    bool IsQueryCached(const SMessageID& message_id);
    std::string CreateQueryHitMessage(const std::string& filename, const SMessageID& message_id);
    std::string CreateInvQueryHitMessage(const std::string& filename, const SMessageID& message_id);
    void StartFileTransfer(const std::string& filename, const SPeerServerInfo& peer_server_info);
   
    // periodically remove old queries
    void MantainQueryMapSize();
    void StartReplicateAllFiles();
    void UpdateFileInfoPeriodically();
    void BroadcastProcessThread();
    void HandleMessageThread();
    void WaitReplicateFileQueryHit(SMessageID message_id, const std::string& filename);

    // sever handle logic
    void HandleMessage(const std::string& message);
    void HandleQuery(const std::string& message);
    void HandleConcurrentTestQuery(const std::string& message);
    void HandleInvQuery(const std::string& message);

    void HandleQueryHit(const std::string& message);
    void HandleWaitQueryHit(const std::string& message_type, const SMessageID& message_id);
    void HandleInvQueryHit(const std::string& message);
    
    void SetFileInfoDirty();
private:

    void SendQuery(const std::string& filename, SMessageID* message_id);
    void SendInvQuery(const std::string& filename, SMessageID* message_id);

    std::vector<std::string> CreateCommonQueryMsgVec(const std::string& msg_type, SMessageID* message_id);

    void SetWaitQueryHit(SMessageID message_id, const std::string& filename);
    void SendTestQuery();
    void SendTestObtain();
    void SendTestObtainLarge();
    void SendTestObtain3(int requirement_peer_id);
    void SendConcurrentTestQuery(int peer_id);
    void SendTestObtain2(int requirement_peer_id);
    void HandleConcurrentTestObtain(const std::string& message);
    void InitPeerFileClient();
    void LoadSettings();
    void Register();
    void LoadTopology();
    void LoadFileInfo();
    void HandleRegisterResponse(char* message);
    void ConnectToPeers(); 

    void Broadcast(const std::string& message, PeerID exclude_peer_id = -1);
    std::string CreateQueryMessage(const std::string& filename, SMessageID* message_id=nullptr);
    std::string CreateInvQueryMessage(const std::string& filename, SMessageID* message_id=nullptr);
    bool IsValidUserCommand(const std::string& command) const;
    void PrintHelp() const;

    CPeerClient();
    CPeerClient(const CPeerClient&) = delete;
    CPeerClient& operator=(const CPeerClient&) = delete;
    ~CPeerClient();

    std::mutex m_file_info_mutex;
    bool m_file_info_dirty = false;

    int m_replicatioin_factor = 0;
    std::string m_register_server_ip;

    std::mutex m_send_buffer_mutex;
    char* m_send_buffer;

    std::mutex m_broadcast_queue_mutex;
    std::condition_variable m_broadcast_queue_cv;
    std::queue<std::pair<std::string, PeerID>> m_broadcast_queue;

    std::mutex m_message_queue_mutex;
    std::condition_variable m_message_queue_cv;
    std::queue<std::string> m_messages_queue;

    // peer_id of current peer client
    PeerID m_peer_id;
    // topology
    ETopologyType m_topology_type;    
    // store peer ip;
    std::unordered_map<PeerID, std::string> m_peer_id_to_ip;
    // store peer_id and socket fd
    std::unordered_map<PeerID, int> m_peer_id_to_sockfd;

    std::mutex m_msg_id_counter_mutex;
    MessageCounter m_msg_id_counter;
    

    std::mutex m_query_hit_result_mutex;
    std::unordered_map<SMessageID, std::list<SQueryResult>> m_query_hit_result;

    std::unordered_set<std::string> m_files;
    std::mutex queries_map_mutex;
    // stores SMessageID and upstream peer_id for query and queryhit
    std::unordered_map<SMessageID, PeerID> m_queries;

    std::mutex m_wait_queryhit_mutex;
    std::unordered_map<SMessageID, std::string> m_wait_queryhit;


    std::mutex m_replicate_query_filename_mutex;
    std::unordered_set<std::string> m_replicate_query_filename;
    std::string m_ensure_queryhit_file;
    std::string m_large_file_name;
};

CPeerClient* getPeerClientInstance(void);


#endif // _PEER_CLIENT_H_