#ifndef _DEFINES_H_
#define _DEFINES_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <thread>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
#include <assert.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <list>
#include <queue>
#include <deque>
#include <condition_variable>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <random>
#include <chrono>

#define FILE_CLIENT_THREAD_NUM 2
#define FILE_SERVER_THREAD_NUM 4

// limit the number of connections, file server should handle
#define MAX_SOCKET_FD_QUEUE_SIZE 300


#define LISTEN_BACKLOG 20

#define PEER_FILE_DIR "./shared_files"
#define TEMP_SPLIT_FILE_DIR "temporary"

#define REGISTER_SERVER_PORT 1235

#define PEER_SERVER_PORT 2234
#define PEER_FILE_SERVER_PORT  3000

#define COMMAND_MAX_LEN 20
#define COMMAND_LIST_AMOUNT_LIMIT 100

#define REGISTER_REQUEST_REGISTER "register_peer"
#define REGISTER_RESPONSE_ALL_PEERS "all_peers"

// test 10k queries
#define TEST_QUERY_COUNT 10000

#define TEST_OBTAIN_COUNT 10000

#define TEST_OBTAIN_LARGE_COUNT 10

// index server response
#define INDEX_RESPONSE_PRINT "sv_print"
// trigger client to start downloading file from another peer
#define INDEX_RESPONSE_OBTAIN "sv_obtain_file"


#define PACKET_SPLIT_NUM_REQUEST 3 // client to server
#define PACKET_SPLIT_NUM_RESPONSE 4 // server to client 

typedef unsigned long long SessionID;

typedef int PeerID;

#define PEER_MSG_TYPE_DELIM  "\n"

#define PEER_MSG_QUERY "peer_query"
#define PEER_MSG_QUERY_HIT "peer_query_hit"

// inverse query only used to replicate files
#define PEER_MSG_INV_QUERY "peer_inv_query"
#define PEER_MSG_INV_QUERY_HIT "peer_inv_query_hit"

#define PEER_MSG_CON_TEST_QUERY "peer_con_test_query"

// concurrent test obtain
#define PEER_MSG_TEST_OBTAIN2 "peer_test_obtain_2"

// user commands in interactive mode
#define USER_COMMAND_HELP "help"
#define USER_COMMAND_OBTAIN "obtain"
#define USER_COMMAND_QUERY "query"
#define USER_COMMAND_INV_QUERY "inv_query"
#define USER_COMMAND_REPLICATE "replicate"
#define USER_COMMAND_TEST_QUERY "test_query"
#define USER_COMMAND_CONCURRENT_TEST_QUERY "test_query2"
#define USER_COMMAND_TEST_OBTAIN "test_obtain"
// test concurrent obtain
#define USER_COMMAND_TEST_OBTAIN2 "test_obtain2"
#define USER_COMMAND_TEST_OBTAIN3 "test_obtain3"

#define USER_COMMAND_CLEAR "clear"

#define OBTAIN_LARGE_FILE_NAME "1_large_file_1.bin"

enum class ETopologyType {
    TEST = 0,
    STAR = 1,
    MESH_2D = 2
};

// request message structure
// request_type, filename, data
enum class ERequestType {
    REQUEST_DOWNLOAD_FILE = 0,
    REQUEST_PUSH_FILE = 1
};

// response mesage structure
// response_type; filename; chunk_num; chunk_size; data
enum class EResponseType {
    SEND_CHUNK = 0,
    ALL_CHUNK_SENT = 1
};

#define TTL 5

typedef unsigned long long MessageCounter;

struct SMessageID {
    int peer_id;
    MessageCounter msg_id;
    int64_t timestamp;
    SMessageID() = default;
    SMessageID(const SMessageID& other) {
        peer_id = other.peer_id;
        msg_id = other.msg_id;
        timestamp = other.timestamp;
    }
    bool operator==(const SMessageID& other) const {
        return peer_id == other.peer_id && msg_id == other.msg_id;
    }
};

enum class EMessageType {
    QEURY = 0,
    QUERY_HIT = 1
};

struct SMessage {
    SMessageID message_id;
    EMessageType message_type;
    int ttl;
    char* data;
};

namespace std {
    template<>
    struct hash<SMessageID> {
        size_t operator()(const SMessageID& s) const {
            // compute the hash
            return hash<int>()(s.peer_id) ^ hash<int>()(s.msg_id);
        }
    };
}


/*
The maximum send buffer size is 1,048,576 bytes.
The default value of the SO_SNDBUF option is 32767.
For a TCP socket, the maximum length that you can specify is 1 GB.
For a UDP or RAW socket, the maximum length that you can specify is the smaller of the following values:
65,527 bytes (for a UDP socket) or 32,767 bytes (for a RAW socket).
The send buffer size defined by the SO_SNDBUF option.
*/
#define TCP_PACKET_SIZE (4 * 1024)
#define MSG_BUF_SIZE 1024

#define FILE_TRANSFER_PACKET_SIZE (42 * 1024)
#define FILE_CHUNK_SIZE (40 * 1024)

int GetRandomNumber(int mod);
int GetFileServerPort(int peer_id);
int GetPeerServerPort(int peer_id);

size_t GetFileSize(const std::string& filename);
std::vector<std::string> SplitString(const std::string& inputString, const std::string& delim, int split_num = -1);

std::string JoinString(const std::vector<std::string>& inputString, const std::string& delim);

std::vector<std::string> SplitCommandStringBySize(const std::string& inputString, \
    const std::string& command, size_t max_size);

size_t CalcChunkNums(size_t file_size);

bool IsNumber(const std::string& s);

std::int64_t GetTimestamp();
struct SPeerServerInfo {
    std::string ip;
    int port;
};

struct SQueryResult {
    SPeerServerInfo server_info;
    int64_t timestamp;
};

struct SFileInfo {
    std::string filename;
    size_t size;
};


#endif // _DEFINES_H_