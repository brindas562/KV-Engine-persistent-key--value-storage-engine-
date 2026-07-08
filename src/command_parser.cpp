#include<string>
using namespace std;
#include "../include/command.h"

#include<sstream>

Command parseCommand(const string & input){
    Command cmd;
    stringstream ss(input);
    ss>>cmd.op;

    if(cmd.op =="SET"){
        ss >>cmd.key;
        ss>>cmd.value;
    }
    else if(cmd.op == "GET" || cmd.op =="DEL"){
        ss>>cmd.key;
    }
    return cmd;

}
