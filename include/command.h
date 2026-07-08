#pragma once
#include <string>
using namespace std;

struct Command{
    string op;
    string key;
    string value;
};
Command parseCommand(const string& request);