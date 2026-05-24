#include <iostream>
#include <filesystem>
#include <chrono>
#include <string>
#include <cassert>
#include <fstream>

#include "kvstore/storage.h"
#include "kvstore/log_manager.h"
#include "tests/test_data_generation.h"

int main(int argc, char **argv)
{
    const fs::path test_dir = "data/restore_test";
    const fs::path sstable_dir = test_dir / "sstables";
    const fs::path logfile_path = test_dir / "data.log";
    const unsigned int num_entries = 10000;

    fs::remove_all(test_dir);
    fs::create_directories(sstable_dir);

    fs::path logfile = "";
    Storage storage(sstable_dir.string(), num_entries + 1);
    generate_test_data(logfile, storage, false, num_entries, logfile_path);

    std::ifstream f(logfile);
    if (!f.is_open())
    {
        std::cerr << "data.log not found at: " << logfile << std::endl;
        return 1;
    }

    LogManager log_manager(logfile.string(), storage);

    auto start = std::chrono::steady_clock::now();
    log_manager.restore();
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Restore took " << ms << " ms" << std::endl;

    for (unsigned int i = 0; i < num_entries; i++)
    {
        std::string key = std::to_string(i);
        std::string expected = std::to_string(i);
        std::string got = storage.get(key);
        if (got != expected)
        {
            std::cerr << "Mismatch at " << i << ": got '" << got << "' expected '" << expected << "'\n";
            return 2;
        }
    }

    if (storage.size() != static_cast<int>(num_entries))
    {
        std::cerr << "Expected size " << num_entries << ", got " << storage.size() << "\n";
        return 3;
    }

    std::cout << "Validation passed" << std::endl;
    return 0;
}
