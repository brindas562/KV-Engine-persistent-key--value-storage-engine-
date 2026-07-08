#include "../include/kvstore.h"

#include <iostream>
#include <cassert>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

void clean()
{
    fs::remove_all("data");
}

int main()
{
    clean();

    cout << "\n========== TEST 1 ==========\n";
    cout << "Basic SET / GET\n";

    {
        KVStore kv("data.log");

        kv.set("name","chandan");

        string value;

        assert(
            kv.get("name",value)
        );

        assert(
            value == "chandan"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 2 ==========\n";
    cout << "DELETE\n";

    {
        KVStore kv("data.log");

        kv.set("temp","123");

        kv.del("temp");

        string value;

        assert(
            !kv.get("temp",value)
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 3 ==========\n";
    cout << "WAL Recovery\n";

    {
        KVStore kv("data.log");

        kv.set("recover","yes");
    }

    {
        KVStore kv("data.log");

        string value;

        assert(
            kv.get("recover",value)
        );

        assert(
            value == "yes"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 4 ==========\n";
    cout << "SSTable Flush\n";

    {
        KVStore kv("data.log");

        for(int i=0;i<2000;i++)
        {
            kv.set(
                "key"+to_string(i),
                "value"+to_string(i)
            );
        }

        string value;

        assert(
            kv.get(
                "key1500",
                value
            )
        );

        assert(
            value == "value1500"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 5 ==========\n";
    cout << "Restart After SSTable Flush\n";

    {
        KVStore kv("data.log");

        string value;

        assert(
            kv.get(
                "key1500",
                value
            )
        );

        assert(
            value == "value1500"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 6 ==========\n";
    cout << "Update Existing Key\n";

    {
        KVStore kv("data.log");

        kv.set(
            "key100",
            "UPDATED"
        );

        string value;

        assert(
            kv.get(
                "key100",
                value
            )
        );

        assert(
            value == "UPDATED"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 7 ==========\n";
    cout << "Delete Existing Key\n";

    {
        KVStore kv("data.log");

        kv.del("key200");

        string value;

        assert(
            !kv.get(
                "key200",
                value
            )
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 8 ==========\n";
    cout << "Newest SSTable Wins\n";

    {
        KVStore kv("data.log");

        kv.set("user","v1");

        for(int i=0;i<1000;i++)
        {
            kv.set(
                "dummyA"+to_string(i),
                "x"
            );
        }

        kv.set("user","v2");

        for(int i=0;i<1000;i++)
        {
            kv.set(
                "dummyB"+to_string(i),
                "x"
            );
        }

        kv.set("user","v3");

        string value;

        assert(
            kv.get(
                "user",
                value
            )
        );

        assert(
            value == "v3"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 9 ==========\n";
    cout << "Compaction Trigger\n";

    {
        KVStore kv("data.log");

        for(int i=0;i<10000;i++)
        {
            kv.set(
                "compact"+to_string(i),
                "value"
            );
        }

        string value;

        assert(
            kv.get(
                "compact5000",
                value
            )
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 10 ==========\n";
    cout << "Restart After Compaction\n";

    {
        KVStore kv("data.log");

        string value;

        assert(
            kv.get(
                "compact5000",
                value
            )
        );

        assert(
            value == "value"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 11 ==========\n";
    cout << "Massive Dataset\n";

    {
        KVStore kv("data.log");

        for(int i=0;i<50000;i++)
        {
            kv.set(
                "K"+to_string(i),
                "V"+to_string(i)
            );
        }

        string value;

        assert(
            kv.get(
                "K40000",
                value
            )
        );

        assert(
            value == "V40000"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 12 ==========\n";
    cout << "Restart After Massive Dataset\n";

    {
        KVStore kv("data.log");

        string value;

        assert(
            kv.get(
                "K40000",
                value
            )
        );

        assert(
            value == "V40000"
        );
    }

    cout << "PASS\n";



    cout << "\n========== TEST 13 ==========\n";
    cout << "Random Verification\n";

    {
        KVStore kv("data.log");

        string value;

        for(int i=0;i<1000;i++)
        {
            int idx = rand()%50000;

            assert(
                kv.get(
                    "K"+to_string(idx),
                    value
                )
            );

            assert(
                value ==
                "V"+to_string(idx)
            );
        }
    }

    cout << "PASS\n";



    cout << "\n=================================\n";
    cout << "ALL STORAGE ENGINE TESTS PASSED\n";
    cout << "=================================\n";

    return 0;
}