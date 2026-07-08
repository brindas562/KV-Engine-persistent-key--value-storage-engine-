#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <cstring>
#include <csignal>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

atomic<long long> setCount(0);
atomic<long long> getCount(0);
atomic<long long> delCount(0);
atomic<long long> failCount(0);

bool sendCommand(int sockfd,const string& cmd)
{
    int sent =
        send(
            sockfd,
            cmd.c_str(),
            cmd.size(),
            0
        );

    if(sent <= 0)
        return false;

    char buffer[1024];

    int n =
        recv(
            sockfd,
            buffer,
            sizeof(buffer)-1,
            0
        );

    if(n <= 0)
        return false;

    return true;
}

void worker(int id,atomic<bool>& stop)
{
    int sockfd =
        socket(AF_INET,SOCK_STREAM,0);

    if(sockfd < 0)
    {
        failCount++;
        return;
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
        close(sockfd);
        failCount++;
        return;
    }

    mt19937 rng(
        random_device{}()
    );

    while(!stop.load())
    {
        int op =
            rng() % 10;

        int key =
            rng() % 100000;

        string cmd;

        if(op < 4)
        {
            cmd =
                "GET key" +
                to_string(key);

            if(sendCommand(sockfd,cmd))
                getCount++;
            else
                failCount++;
        }
        else if(op < 8)
        {
            cmd =
                "SET key" +
                to_string(key) +
                " value" +
                to_string(key);

            if(sendCommand(sockfd,cmd))
                setCount++;
            else
                failCount++;
        }
        else
        {
            cmd =
                "DEL key" +
                to_string(key);

            if(sendCommand(sockfd,cmd))
                delCount++;
            else
                failCount++;
        }
    }

    close(sockfd);
}

int main()
{
    signal(SIGPIPE,SIG_IGN);

    constexpr int NUM_CLIENTS = 100;
    constexpr int RUN_TIME_SEC = 300;

    atomic<bool> stop(false);

    vector<thread> clients;

    cout << "=========================\n";
    cout << "KVS BENCHMARK\n";
    cout << "=========================\n";
    cout << "Clients : "
         << NUM_CLIENTS
         << "\n";
    cout << "Duration: "
         << RUN_TIME_SEC
         << " sec\n\n";

    auto start =
        chrono::steady_clock::now();

    for(int i=0;i<NUM_CLIENTS;i++)
    {
        clients.emplace_back(
            worker,
            i,
            ref(stop)
        );
    }

    this_thread::sleep_for(
        chrono::seconds(
            RUN_TIME_SEC
        )
    );

    stop = true;

    for(auto& t : clients)
        t.join();

    auto end =
        chrono::steady_clock::now();

    double sec =
        chrono::duration<double>(
            end - start
        ).count();

    long long total =
        setCount.load() +
        getCount.load() +
        delCount.load();

    cout << "\n";
    cout << "=========================\n";
    cout << "BENCHMARK RESULTS\n";
    cout << "=========================\n";

    cout << "Duration : "
         << sec
         << " sec\n";

    cout << "SET      : "
         << setCount.load()
         << "\n";

    cout << "GET      : "
         << getCount.load()
         << "\n";

    cout << "DEL      : "
         << delCount.load()
         << "\n";

    cout << "Failed   : "
         << failCount.load()
         << "\n";

    cout << "Total    : "
         << total
         << "\n\n";

    cout << "SET/sec  : "
         << setCount.load()/sec
         << "\n";

    cout << "GET/sec  : "
         << getCount.load()/sec
         << "\n";

    cout << "DEL/sec  : "
         << delCount.load()/sec
         << "\n";

    cout << "OPS/sec  : "
         << total/sec
         << "\n";
    


    cout << "=========================\n";

    //for stats
int sockfd = socket(AF_INET, SOCK_STREAM, 0);

sockaddr_in server_addr{};
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(8080);

inet_pton(
    AF_INET,
    "127.0.0.1",
    &server_addr.sin_addr
);

connect(
    sockfd,
    (sockaddr*)&server_addr,
    sizeof(server_addr)
);

string cmd = "STATS";

send(
    sockfd,
    cmd.c_str(),
    cmd.size(),
    0
);

char buffer[1024] = {0};

recv(
    sockfd,
    buffer,
    sizeof(buffer)-1,
    0
);

cout << "\nSERVER STATS\n";
cout << buffer << endl;

close(sockfd);

}