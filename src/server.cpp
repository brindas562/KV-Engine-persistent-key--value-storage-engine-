#include "../include/server.h"
#include "../include/command.h"
#include <thread>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

Server::Server(int port,const string& file)
    : port(port),
      kv(file)
{
}


void Server::handleClient(int client_fd)
{
    cout << "Client connected\n";

    while(true)
    {
        char buffer[1024] = {0};

        int bytes =
            recv(
                client_fd,
                buffer,
                sizeof(buffer)-1,
                0
            );

        if(bytes <= 0)
            break;

        string request(buffer);

        //cout<< "Raw request = ["<< request<< "]"<< endl;

        Command cmd =
            parseCommand(request);

        string reply;

        if(cmd.op == "SET")
        {
            if(kv.set(cmd.key,cmd.value))
                reply = "OK";
            else
                reply = "NOT_INSERTED";
        }
        else if(cmd.op == "GET")
        {
            string value;

            if(kv.get(cmd.key,value))
                reply = value;
            else
                reply = "NOT_FOUND";
        }
        else if(cmd.op == "DEL")
        {
            if(kv.del(cmd.key))
                reply = "OK";
            else
                reply = "NOT_FOUND";
        }
        else if(cmd.op == "STATS")
        {
            reply =
                "MemHits=" +
                to_string(kv.memHits.load()) +
                " SSTHits=" +
                to_string(kv.sstableHits.load()) +
                " WALHits=" +
                to_string(kv.walHits.load()) +
                " Misses=" +
                to_string(kv.misses.load());
        }
        else
        {
            reply = "INVALID_COMMAND";
        }

        send(
            client_fd,
            reply.c_str(),
            reply.size(),
            0
        );
    }

    cout << "Client disconnected\n";

    close(client_fd);
}

void Server::start()
{
    int server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if(server_fd < 0)
    {
        perror("socket");
        return;
    }

    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(::bind(
            server_fd,
            (sockaddr*)&addr,
            sizeof(addr)
        ) < 0)
    {
        perror("bind");
        return;
    }

    if(listen(server_fd,1024) < 0)
    {
        perror("listen");
        return;
    }

    cout
        << "Server listening on "
        << port
        << endl;

    while(true)
    {
        int client_fd =
            accept(
                server_fd,
                nullptr,
                nullptr
            );

        if(client_fd < 0)
        {
            perror("accept");
            continue;
        }

        thread(&Server::handleClient,this,client_fd).detach();// for multiple clients
    }

    close(server_fd);
}