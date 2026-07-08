#include "../include/sstable.h"
#include "../include/bloom_filter.h"

#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;
static constexpr const char* TOMBSTONE = "__TOMBSTONE__";

pair<string,string> SSTable::writeTable(const string& filename,const unordered_map<string,string>& memTable)
{
    vector<pair<string,string>> data;

    for(const auto& p : memTable)
        data.push_back(p);

    sort(data.begin(),data.end());

    ofstream out(filename,ios::binary);

    for(const auto& p : data)
    {
        uint32_t keyLen =p.first.size();

        uint32_t valueLen =p.second.size();

        out.write((char*)&keyLen,sizeof(keyLen));

        out.write((char*)&valueLen,sizeof(valueLen));

        out.write(p.first.data(),keyLen);

        out.write(p.second.data(),valueLen);
    }
    string minKey = data.front().first;
    string maxKey = data.back().first;

    return {minKey,maxKey};
}

//currently not using this function since we have index and all we are using that
bool SSTable::get(const string& filename,const string& key,string& value)
{
    ifstream in(filename,ios::binary);

    string k;
    string v;

    while(true)
    {
        uint32_t keyLen;
        uint32_t valueLen;

        if(!in.read((char*)&keyLen,sizeof(keyLen)))break;

        if(!in.read((char*)&valueLen,sizeof(valueLen)))break;
        

        string storedKey(keyLen,'\0');

        string storedValue(valueLen,'\0');

        in.read(&storedKey[0],keyLen);

        in.read(&storedValue[0],valueLen);

        if(storedKey == key)
        {
            value = storedValue;
            return true;
        }
    }

    return false;
}


//sstable compaction

//helper function
void SSTable::loadTable(const string& filename,unordered_map<string,string>& map)
{
    ifstream in(filename,ios::binary);
        
    while(true)
    {
        uint32_t keyLen;
        uint32_t valueLen;

        if(!in.read((char*)&keyLen,sizeof(keyLen)))break;
        if(!in.read((char*)&valueLen,sizeof(valueLen)))break;

        string Key(keyLen,'\0');

        string Value(valueLen,'\0');

        in.read(&Key[0],keyLen);

        in.read(&Value[0],valueLen);

        map[Key] = Value;
        if(Value == TOMBSTONE)
        {
            map.erase(Key);
        }
    }
}

//helper function for min-max in sstable

pair<string,string>SSTable::getMinMaxKey(const string& filename){

    ifstream in(filename,ios::binary);
    string minKey;
    string maxKey;

    bool first = true;

    while(true)
    {
        uint32_t keyLen;
        uint32_t valueLen;

        if(!in.read((char*)&keyLen,sizeof(keyLen)))break;
        

        in.read((char*)&valueLen,sizeof(valueLen));

        string key(keyLen,'\0');
        string value(valueLen,'\0');

        in.read(&key[0],keyLen);
        in.read(&value[0],valueLen);

        if(first)
        {
            minKey = key;
            first = false;
        }

        maxKey = key;
    }

    return {
        minKey,
        maxKey
    };
}



vector<pair<string,uint64_t>>SSTable::buildSparseIndex(const string& filename){
    vector<pair<string,uint64_t>> index;

    ifstream in(filename,ios::binary);

    int count = 0;

    while(true){
        uint64_t offset =in.tellg();

        uint32_t keyLen;
        uint32_t valueLen;

        if(!in.read((char*)&keyLen,sizeof(keyLen)))break;
        if(!in.read((char*)&valueLen,sizeof(valueLen)))break;

        string key(keyLen,'\0');
        string value(valueLen,'\0');

        in.read(&key[0],keyLen);
        in.read(&value[0],valueLen);

        if(count % 100 == 0)index.push_back({key,offset});

        count++;
    }

    return index;
}

//finding first offset using binary search techique
uint64_t SSTable::findStartOffset(const vector<pair<string,uint64_t>>& sparseIndex,const string& key){
    if(sparseIndex.empty())
        return 0;

    int l = 0;
    int r = sparseIndex.size() - 1;

    uint64_t answer = 0;

    while(l <= r)
    {
        int mid = l + (r-l)/2;

        if(sparseIndex[mid].first <= key)
        {
            answer = sparseIndex[mid].second;
            l = mid + 1;
        }
        else
        {
            r = mid - 1;
        }
    }

    return answer;
}

bool SSTable::getUsingSparseIndex(const string& filename,const vector<pair<string,uint64_t>>& sparseIndex,const string& key,string& value)
{
    ifstream in(filename,ios::binary);

    uint64_t start =findStartOffset(sparseIndex,key);

    in.seekg(start);

    while(true)
    {
        uint32_t keyLen;
        uint32_t valueLen;

        if(!in.read((char*)&keyLen,sizeof(keyLen)))break;

        if(!in.read((char*)&valueLen,sizeof(valueLen)))break;
        

        string storedKey(keyLen,'\0');

        string storedValue(valueLen,'\0');

        in.read(&storedKey[0],keyLen);

        in.read(&storedValue[0],valueLen);

        if(storedKey == key)
        {
            value = storedValue;
            return true;
        }

        if(storedKey > key)
        {
            return false;
        }
    }

    return false;
}

// Scan every key in the SSTable file and add it to a fresh BloomFilter.
// expectedKeys is a hint for sizing (pass memTable.size() at flush time,
// or count keys during the scan when loading from disk).
BloomFilter SSTable::buildBloomFilter(const string& filename, size_t expectedKeys)
{
    // If the caller doesn't know the count, do a quick first pass to count,
    // then a second pass to insert. For simplicity we accept the hint and
    // tolerate a slightly over-loaded filter when the hint is off — bloom
    // filters degrade gracefully (higher FP rate, never incorrect).
    if (expectedKeys == 0) expectedKeys = 1000; // safe fallback

    BloomFilter bf(expectedKeys, 0.01); // 1% false-positive rate

    ifstream in(filename, ios::binary);
    if (!in) return bf;

    while (true)
    {
        uint32_t keyLen;
        uint32_t valueLen;

        if (!in.read((char*)&keyLen,   sizeof(keyLen)))   break;
        if (!in.read((char*)&valueLen, sizeof(valueLen))) break;

        string key(keyLen, '\0');
        string val(valueLen, '\0'); // must read even if we don't use it

        if (!in.read(&key[0], keyLen))   break;
        if (!in.read(&val[0], valueLen)) break;

        bf.add(key);
    }

    return bf;
}