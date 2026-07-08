#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <chrono>
#include <cstring>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

atomic<bool> stopFlag(false);

atomic<long long> setOps(0);
atomic<long long> getOps(0);
atomic<long long> delOps(0);
atomic<long long> failures(0);

void worker(int id)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr{};

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddr.sin_addr
    );

    if(connect(
        sockfd,
        (sockaddr*)&serverAddr,
        sizeof(serverAddr)
    ) < 0)
    {
        failures++;
        return;
    }

    mt19937 rng(random_device{}());

    char buffer[1024];

    while(!stopFlag.load())
    {
        int op = rng()%100;
        int key = rng()%10000;

        string request;

        if(op < 50)
        {
            request =
                "GET key" +
                to_string(key);

            getOps++;
        }
        else if(op < 90)
        {
            request =
                "SET key" +
                to_string(key) +
                " value" +
                to_string(key);

            setOps++;
        }
        else
        {
            request =
                "DEL key" +
                to_string(key);

            delOps++;
        }

        memset(
            buffer,
            0,
            sizeof(buffer)
        );

        if(send(
            sockfd,
            request.c_str(),
            request.size(),
            0
        ) <= 0)
        {
            failures++;
            break;
        }

        int n = recv(
            sockfd,
            buffer,
            sizeof(buffer)-1,
            0
        );

        if(n <= 0)
        {
            failures++;
            break;
        }
    }

    close(sockfd);
}

int main()
{
    cout
        << "Starting 100 client benchmark...\n";

    vector<thread> clients;

    auto start =
        chrono::steady_clock::now();

    for(int i=0;i<10;i++)
    {
        clients.emplace_back(
            worker,
            i
        );
    }

    this_thread::sleep_for(
        chrono::minutes(5)
    );

    stopFlag = true;

    for(auto& t : clients)
    {
        t.join();
    }

    auto end =
        chrono::steady_clock::now();

    double seconds =
        chrono::duration<double>(
            end-start
        ).count();

    long long total =
        setOps +
        getOps +
        delOps;

    cout << "\n========== RESULTS ==========\n";

    cout
        << "Duration      : "
        << seconds
        << " sec\n";

    cout
        << "SET Ops       : "
        << setOps
        << "\n";

    cout
        << "GET Ops       : "
        << getOps
        << "\n";

    cout
        << "DEL Ops       : "
        << delOps
        << "\n";

    cout
        << "Failures      : "
        << failures
        << "\n";

    cout
        << "Total Ops     : "
        << total
        << "\n";

    cout
        << "Ops/sec       : "
        << total/seconds
        << "\n";
}