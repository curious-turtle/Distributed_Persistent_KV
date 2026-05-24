#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#include "kvstore/storage.h"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    const fs::path test_dir = "data/test_sstables";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    // Small thresholds to force flushes and compaction in a short test.
    Storage storage(test_dir.string(), 2, 3);

    storage.put("a", "1");
    storage.put("b", "1"); // flush #1
    storage.put("a", "2");
    storage.put("c", "1"); // flush #2
    storage.remove("b");
    storage.put("d", "1"); // flush #3 -> compaction trigger

    if (storage.get("a") != "2")
    {
        std::cerr << "Expected latest value for a to be 2\n";
        return 1;
    }
    if (storage.exists("b"))
    {
        std::cerr << "Expected b to be deleted\n";
        return 2;
    }
    if (storage.get("b") != "")
    {
        std::cerr << "Expected tombstoned b to return empty string\n";
        return 3;
    }
    if (storage.get("c") != "1" || storage.get("d") != "1")
    {
        std::cerr << "Expected c and d to exist\n";
        return 4;
    }
    if (storage.size() != 3)
    {
        std::cerr << "Expected size to be 3, got " << storage.size() << "\n";
        return 5;
    }

    int sstable_count = 0;
    for (const auto &entry : fs::directory_iterator(test_dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sst")
        {
            sstable_count++;
        }
    }

    if (sstable_count != 1)
    {
        std::cerr << "Expected compaction to leave exactly 1 SSTable, got " << sstable_count << "\n";
        return 6;
    }

    std::cout << "Memtable flush and compaction test passed\n";
    return 0;
}
