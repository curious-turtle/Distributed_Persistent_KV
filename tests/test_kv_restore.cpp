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
    fs::path logfile = "";
    Storage storage;
    generate_test_data(logfile, storage, false);

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

    int sz = storage.size();
    for (int i = 0; i < sz; i++)
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

    std::cout << "Validation passed" << std::endl;
    return 0;
}