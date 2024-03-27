#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "defines.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 1235

int main() {
    std::string test = "a b c d e";
    auto result = SplitString(test, " ", 2);
    for (auto& str : result) {
        printf("%s\n", str.c_str());
    }
}


