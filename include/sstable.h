#pragma once
#include<iostream>
#include <string>
#include <unordered_map>
#include "bloom_filter.h"
using namespace std;


class SSTable
{
public:

    static pair<string,string> writeTable(const string& filename,const unordered_map<string,string>& memTable);

    static bool get(const string& filename,const string& key,string& value);

    static void loadTable(const string& filename,unordered_map<string,string>& map);//helper function for sstable compaction

    static pair<string,string>getMinMaxKey(const string& filename); //helper function to get min and max key

    static vector<pair<string,uint64_t>>buildSparseIndex(const string& filename);//for creating sparse index for each sstable

    static uint64_t findStartOffset(const vector<pair<string,uint64_t>>& sparseIndex,const string& key);//finding first offset using sparse index

    static bool getUsingSparseIndex(const string& filename,const vector<pair<string,uint64_t>>& sparseIndex,const string& key,string& value);
    //binary search for nearest offset then linear search

    // Scan the SSTable file and insert every key into a BloomFilter.
    // Called once after writeTable() and once during loadSSTables() on restart.
    static BloomFilter buildBloomFilter(const string& filename, size_t expectedKeys);
};