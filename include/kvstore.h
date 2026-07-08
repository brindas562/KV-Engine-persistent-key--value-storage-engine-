#pragma once
#include<vector>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <fstream>
#include <thread>
#include <atomic>
#include "bloom_filter.h"
using namespace std;




struct SSTableMeta
{
    string filename;
    string minKey;
    string maxKey;

    vector<pair<string,uint64_t>> sparseIndex;
    BloomFilter bloom{1000};  // rebuilt in-memory; size overwritten on load/flush
};

class KVStore {
private:
    string walFile;
    unordered_map<string, uint64_t> keyDir;
    shared_mutex sm;
    ofstream wal;
    uint32_t MAX_KEY_SIZE = 1024;
    uint32_t MAX_VAL_SIZE = 1024 * 1024;


    vector<SSTableMeta>sstables;
    unordered_map<string,string>memTable;
    int nextTableId=1;
    static constexpr int FLUSH_THRESHOLD = 10000;


    void rebuildkeydirinternal();

public:
    atomic<long long> memHits{0};
    atomic<long long> sstableHits{0};
    atomic<long long> misses{0};
    atomic<long long> walHits{0};
    thread flushThread;
    atomic<bool> stopFlusher{false};
    KVStore(const string& filename);

    unordered_map<string,uint64_t>& getKeyDir();

    bool set(const string& key,const string& value);

    bool get(const string& key,string& value);

    bool del(const string& key);

    void printKeyDir();

    void rebuildkeydir();

    void compact();

    void flushToSSTable();

    bool searchSSTable(const string & key,string & value);

    void loadSSTables();

    void compactSSTables();

    void flushWorker();


    ~KVStore();
};