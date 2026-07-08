#include "../include/kvstore.h"
#include "../include/sstable.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <cstdint>
#include<mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include<atomic>
#include<random>
#include<filesystem>
#include<algorithm>
namespace fs = std::filesystem;




using namespace std;
static constexpr const char* TOMBSTONE = "__TOMBSTONE__";






    

KVStore::KVStore(const string& filename){

        fs::create_directories("data");
        walFile="data/"+filename;
        loadSSTables();
        rebuildkeydir();
        wal.open(walFile, ios::binary | ios::app);
        if(!wal)
            {
                throw runtime_error("Failed to open WAL");
            }
        flushThread = thread(&KVStore::flushWorker,this);
        
        

    }

unordered_map<string,uint64_t>& KVStore::getKeyDir() {
    return keyDir;
}

bool KVStore::set(const string& key, const string& value){
        //locking mtx
        
        unique_lock<shared_mutex> lock(sm);
        //this_thread::sleep_for(chrono::seconds(2));
        

        if (!wal){
            cout<< "OPEN FAILED: "<< walFile<< endl;
            return false;}

        uint32_t keyLen = key.size();
        uint32_t valueLen = value.size();

        uint64_t offset = wal.tellp();//tellp gives the position of write pointer

        //cout << "Writing " << key<< " at offset "<< offset << endl;

        wal.write((char*)&keyLen, sizeof(keyLen));
        wal.write((char*)&valueLen, sizeof(valueLen));

        wal.write(&key[0], keyLen);//.WRITE IS A BINARTY function whcih dpes not undersatnd string so we give key.data which gives pointer poointing to start of the line there by write len characteds

        wal.write(&value[0], valueLen);

        //wal.flush();//writrs to file from buffer. background threads are created to group commit 

        keyDir[key] = offset; //in memory key directory

        memTable[key] = value;

        if(memTable.size() >= FLUSH_THRESHOLD)
        {
            flushToSSTable();
        }

        return true;

    }

bool KVStore::get(const string& key, string& value){
        
        shared_lock<shared_mutex> lock(sm);
        //this_thread::sleep_for(chrono::seconds(2));


        auto mt = memTable.find(key);

        if(mt != memTable.end())
        {
            memHits++;
            value = mt->second;

            if(value == TOMBSTONE)
                return false;

            return true;
        }

        if(searchSSTable(key,value)) {sstableHits++;
            return true;}



        auto it = keyDir.find(key);

        if(it == keyDir.end()){
            misses++;
            return false;}

        uint64_t offset = it->second;
        ifstream in(walFile,ios::binary);
        if(!in)return false;
        in.seekg(offset);
        uint32_t keylen;
        uint32_t  vallen;


        if(!in.read((char*)&keylen,sizeof(keylen)))
            return false;

        if(!in.read((char*)&vallen,sizeof(vallen)))
            return false;
        
        if(keylen > MAX_KEY_SIZE)
            return false;

        if(vallen > MAX_VAL_SIZE)
            return false;

        string storedkey(keylen,'\0');
        string storedval(vallen,'\0');
        in.read(&storedkey[0],keylen);
        in.read(&storedval[0],vallen);
        value=storedval;
        if(storedval == TOMBSTONE){
        return false;}
        walHits++;

        return true;


    }

bool KVStore::del(const string& key){
       
        return set(key,TOMBSTONE);
    }

void KVStore::printKeyDir(){
          cout << "\nKeyDir\n";

    for (auto& p : keyDir) {
        cout << p.first << " -- " << p.second << '\n';
    }
    }
void KVStore::rebuildkeydirinternal(){
         keyDir.clear();
        ifstream in(walFile,ios::binary);
        if(!in)return;

      

        while(true){
              uint64_t  offset= in.tellg();
              uint32_t keylen;
              uint32_t vallen;
              if(!in.read((char*)&keylen,sizeof(keylen)))break;
              if(!in.read((char*)&vallen,sizeof(vallen)))break;

              string key(keylen,'\0');
              string val(vallen,'\0');

              if(!in.read(&key[0],keylen)) break;
              if(!in.read(&val[0],vallen))break;
              keyDir[key]=offset;

              //cout << "Recovered "<< key<< " at "<< offset<< endl;


        }
    }
void KVStore::rebuildkeydir(){

        unique_lock<shared_mutex> lock(sm);
        rebuildkeydirinternal();
       
    }
void KVStore::compact(){
       
        unique_lock<shared_mutex> lock(sm);
        unordered_map<string,uint64_t>newkvd;
        ofstream out("data/compact.log",ios::binary);
        ifstream in(walFile,ios::binary);
        for(const auto &p:keyDir){
            
            uint64_t compactoffset =out.tellp();

            uint64_t offset=p.second;
            in.seekg(offset);
              uint32_t keylen;
              uint32_t vallen;
              if(!in.read((char*)&keylen,sizeof(keylen)))break;
              if(!in.read((char*)&vallen,sizeof(vallen)))break;

              string key(keylen,'\0');
              string val(vallen,'\0');

              if(!in.read(&key[0],keylen)) break;
              if(!in.read(&val[0],vallen))break;
              if(val!=TOMBSTONE){

                        out.write((char*)&keylen, sizeof(keylen));
                        out.write((char*)&vallen, sizeof(vallen));

                        out.write(&key[0], keylen);//.WRITE IS A BINARTY function whcih dpes not undersatnd string so we give key.data which gives pointer poointing to start of the line there by write len characteds
                        out.write(&val[0], vallen);

                       

                        newkvd[key] = compactoffset; //in memory key directory

              }
            out.flush();//writrs to file from buffer

        }
        out.close();
        in.close();
        wal.flush();
        wal.close();

        if(rename("data/compact.log", walFile.c_str()) != 0)
            {
                perror("rename");
                return;
            }
        wal.open(walFile,ios::binary | ios::app);
        if(!wal.is_open())
        {
            rebuildkeydirinternal();
            return;
        }

        keyDir = newkvd;
        //rebuildkeydirinternal();

    }

void KVStore::flushToSSTable()
{
    string filename ="data/sstable_" +to_string(nextTableId++) +".dat";
    size_t keyCount = memTable.size();
    auto range =SSTable::writeTable(filename,memTable);

    SSTableMeta meta;
    meta.filename    = filename;
    meta.minKey      = range.first;
    meta.maxKey      = range.second;
    meta.sparseIndex = SSTable::buildSparseIndex(filename);
    // Build bloom filter with exact key count known at flush time.
    meta.bloom       = SSTable::buildBloomFilter(filename, keyCount);

    sstables.push_back(std::move(meta));

    memTable.clear();
    if(sstables.size() >= 10)
        {
            compactSSTables();
        }
}




bool  KVStore::searchSSTable(const string & key,string &value){
    for(int i = sstables.size()- 1;i >= 0;i--)
{
    auto &table=sstables[i];

    if(key < table.minKey)
        continue;

    if(key > table.maxKey)
        continue;

    // Bloom filter check: if it says "definitely not", skip file I/O entirely.
    // Only proceed to the sparse index (which opens the file) when the bloom
    // filter says "probably yes". This eliminates disk seeks for missing keys.
    if(!table.bloom.mightContain(key))
        continue;

    if(SSTable::getUsingSparseIndex(
        table.filename,
        table.sparseIndex,
        key,
        value))
    {
        if(value == TOMBSTONE)
            return false;

        return true;
    }
}
    return false;
}

//after restart loading from sstables
void KVStore::loadSSTables()
{
    sstables.clear();

    int maxId = 0;

    for(const auto& entry : fs::directory_iterator("data"))
    {
        string fullPath =
            entry.path().string();

        string filename =
            entry.path().filename().string();

        if(filename.rfind("sstable_", 0) == 0)
        {
            auto range =SSTable::getMinMaxKey(fullPath);
            auto sparseIndex = SSTable::buildSparseIndex(fullPath);
            // Key count unknown at load time — buildBloomFilter scans the file
            // itself; pass 0 so it uses the safe internal fallback, or we can
            // derive the count from sparseIndex size * 100 (our stride).
            size_t estimatedKeys = sparseIndex.size() * 100 + 100;
            auto bloom = SSTable::buildBloomFilter(fullPath, estimatedKeys);

            SSTableMeta meta;
            meta.filename    = fullPath;
            meta.minKey      = range.first;
            meta.maxKey      = range.second;
            meta.sparseIndex = std::move(sparseIndex);
            meta.bloom       = std::move(bloom);

            sstables.push_back(std::move(meta));

            size_t start = 8;
            size_t end = filename.find(".dat");

            if(end != string::npos)
            {
                int id =stoi(filename.substr(start,end - start));
                maxId = max(maxId, id);
            }
        }
    }

   sort(sstables.begin(),sstables.end(),[](const SSTableMeta& a,const SSTableMeta& b)
    {
        auto getId =
            [](const string& path)
            {
                string filename =
                    fs::path(path)
                        .filename()
                        .string();

                return stoi(
                    filename.substr(
                        8,
                        filename.find(".dat") - 8
                    )
                );
            };

        return getId(a.filename)
             < getId(b.filename);
    });

    nextTableId = maxId + 1;
}






//compacts many sstables into one single sstable

void KVStore::compactSSTables()
{
    unordered_map<string,string> merged;

    for(auto &file : sstables)
    {   //loadtable reads the file and updates the map merged 
        //new values will correctly stores in merged map and tombstone values are not added into the map itself
        //output is new clean map
        SSTable::loadTable(file.filename,merged);
    }

    string newFile ="data/sstable_" +to_string(nextTableId++) +".dat";

    SSTable::writeTable(newFile,merged);

    for(auto &file : sstables)
    {
        remove(file.filename.c_str());
    }

    sstables.clear();
    auto range =SSTable::getMinMaxKey(newFile);
    auto sparseIndex = SSTable::buildSparseIndex(newFile);
    size_t keyCount  = merged.size();
    auto bloom       = SSTable::buildBloomFilter(newFile, keyCount);

    SSTableMeta meta;
    meta.filename    = newFile;
    meta.minKey      = range.first;
    meta.maxKey      = range.second;
    meta.sparseIndex = std::move(sparseIndex);
    meta.bloom       = std::move(bloom);

    sstables.push_back(std::move(meta));
}

//background thread to groupcommit in background
void KVStore::flushWorker()
{
    while(!stopFlusher.load())
    {
        this_thread::sleep_for(
            chrono::milliseconds(10)
        );

        unique_lock<shared_mutex> lock(sm);

        if(wal.is_open())
        {
            wal.flush();
        }
    }
}


KVStore::~KVStore()
{
    stopFlusher = true;

    if(flushThread.joinable())
    {
        flushThread.join();
    }

    if(wal.is_open())
    {
        wal.flush();
        wal.close();
    }
    cout << "\n===== KV STATS =====\n";

    cout << "MemTable Hits : "
        << memHits.load()
        << endl;

    cout << "SSTable Hits  : "
        << sstableHits.load()
        << endl;

    cout << "WAL Hits      : "
        << walHits.load()
        << endl;

    cout << "Misses        : "
        << misses.load()
        << endl;
}