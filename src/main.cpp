#include "../include/server.h"

int main()
{
    Server server(8080,"data.log");
    server.start();
}