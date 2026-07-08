#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <csignal>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

atomic<long long> setCount(0);
atomic<long long> getCount(0);
atomic<long long> delCount(0);

void worker(int id, atomic<bool>& stop)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd < 0)
        return;

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
        return;
    }

    mt19937 rng(random_device{}());

    char buffer[1024];

    while(!stop.load())
    {
        int op = rng() % 10;

        int key =
            rng() % 10000;

        string cmd;

        if(op < 4)
        {
            cmd =
                "GET key" +
                to_string(key);

            getCount++;
        }
        else if(op < 8)
        {
            cmd =
                "SET key" +
                to_string(key) +
                " value" +
                to_string(key);

            setCount++;
        }
        else
        {
            cmd =
                "DEL key" +
                to_string(key);

            delCount++;
        }

        send(
            sockfd,
            cmd.c_str(),
            cmd.size(),
            0
        );

        recv(
            sockfd,
            buffer,
            sizeof(buffer)-1,
            0
        );
    }

    close(sockfd);
}

int main()
{
    signal(SIGPIPE,SIG_IGN);

    const int NUM_CLIENTS = 100;

    atomic<bool> stop(false);

    vector<thread> clients;

    cout << "Starting benchmark...\n";
    cout << "Clients : "
         << NUM_CLIENTS
         << "\n";

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

    cout
        << "Running for 5 minutes...\n";

    this_thread::sleep_for(
        chrono::minutes(5)
    );

    stop = true;

    for(auto& t : clients)
        t.join();

    auto end =
        chrono::steady_clock::now();

    double seconds =
        chrono::duration<double>(
            end - start
        ).count();

    long long totalOps =
        setCount.load() +
        getCount.load() +
        delCount.load();

    cout << "\n";
    cout << "=========================\n";
    cout << "BENCHMARK RESULTS\n";
    cout << "=========================\n";

    cout
        << "Duration : "
        << seconds
        << " sec\n";

    cout
        << "SET      : "
        << setCount.load()
        << "\n";

    cout
        << "GET      : "
        << getCount.load()
        << "\n";

    cout
        << "DEL      : "
        << delCount.load()
        << "\n";

    cout
        << "Total    : "
        << totalOps
        << "\n\n";

    cout
        << "SET/sec  : "
        << setCount.load()/seconds
        << "\n";

    cout
        << "GET/sec  : "
        << getCount.load()/seconds
        << "\n";

    cout
        << "DEL/sec  : "
        << delCount.load()/seconds
        << "\n";

    cout
        << "OPS/sec  : "
        << totalOps/seconds
        << "\n";

    cout << "=========================\n";

    return 0;
}