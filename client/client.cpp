#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

int main() {

    int sockfd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if(sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server_addr{};

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &server_addr.sin_addr
    );

    if(connect(
        sockfd,
        (sockaddr*)&server_addr,
        sizeof(server_addr)
    ) < 0)
    {
        perror("connect");
        return 1;
    }

    cout
        << "Connected to server\n";

    while(true)
    {
        string command;

        cout << "> ";

        getline(cin, command);

        if(command == "EXIT")
            break;

        send(
            sockfd,
            command.c_str(),
            command.size(),
            0
        );

        char buffer[1024] = {0};

        int bytes =
            recv(
                sockfd,
                buffer,
                sizeof(buffer)-1,
                0
            );

        if(bytes <= 0)
        {
            cout
                << "Server disconnected\n";
            break;
        }

        cout
            << buffer
            << endl;
    }

    close(sockfd);
}