#pragma once
#include "kvstore.h"
#include<iostream>
using namespace std;

class Server{
private:
    int port;
    KVStore kv;
    void handleClient(int client_fd);
public:
    Server(int port,const string& file);
    void start();
};