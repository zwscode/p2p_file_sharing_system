#include "defines.h"
#include <string>
#include <vector>
#include <sys/stat.h>

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<> distrib(0, 100000);

int GetRandomNumber(int mod) {
    return distrib(gen) % mod;
}

int GetFileServerPort(PeerID peer_id) {
    return PEER_FILE_SERVER_PORT + peer_id;
}

int GetPeerServerPort(PeerID peer_id) {
    return PEER_SERVER_PORT + peer_id;
}


size_t GetFileSize(const std::string& filepath) {
    struct stat statbuf;
    stat(filepath.c_str(), &statbuf);
    size_t size = statbuf.st_size;
    return size;
}

std::vector<std::string> SplitString(const std::string& inputString, const std::string& delim, int split_num) {
    std::vector<std::string> result;
    size_t found = inputString.find_first_of(delim);
    size_t start = 0;
    int split_count = 0;
    while (found != std::string::npos) {
        result.push_back(inputString.substr(start, found - start));
        start = found + 1;
        split_count++;
        found = inputString.find_first_of(delim, start);
        if (split_num > 0 && split_count >= split_num) {
            break;
        }
    }
    if (start < inputString.size()) {
        result.push_back(inputString.substr(start));
    }
    return result;
}

std::string JoinString(const std::vector<std::string>& inputString, const std::string& delim) {
    if (inputString.empty()) {
        return "";
    }

    std::string result;
    for(int i = 0; i < inputString.size(); i++) {
        result += inputString[i];
        if (i != inputString.size() - 1) {
            result += delim;
        }
    }
    return result;
}

std::vector<std::string> SplitCommandStringBySize(const std::string& inputString, const std::string& command, size_t max_size) {
    std::vector<std::string> outputStrings;
    std::string currentString = command + '\n';
    std::istringstream iss(inputString);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.size() > max_size) {
            throw std::runtime_error("Line size is too big");
        }

        if (currentString.size() + line.size() + 2 > max_size) {
            outputStrings.push_back(currentString);
            currentString = command + '\n';
        }
        currentString += line + '\n';
    }

    if (!currentString.empty()) {
        outputStrings.push_back(currentString);
    }
    return outputStrings;
}


size_t CalcChunkNums(size_t file_size) {
    return (file_size + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE;
}


bool IsNumber(const std::string& s)
{
    if (s.empty()) {
        return false;
    }
    auto it = s.begin();
    while (it != s.end() && std::isdigit(*it)) {
        ++it;
    }
    return it == s.end();
}

int64_t GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}
