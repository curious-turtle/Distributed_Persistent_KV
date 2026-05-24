#include <iostream>
#include <filesystem>
#include <chrono>
#include <string>
#include <cassert>
#include <vector>
#include <numeric>
#include <random>

#include "kvstore/storage.h"
#include "kvstore/log_manager.h"
#include "tests/test_data_generation.h"

// TO DO: Use google benchmark for this test instead of rolling our own timing and reporting.

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

    log_manager.restore();

    int sz = storage.size();
    if (sz <= 0)
    {
        std::cerr << "Storage empty after restore" << std::endl;
        return 2;
    }

    int ops = std::max(1, sz / 10); // 10% of values

    std::mt19937 rng(123456);
    std::uniform_int_distribution<int> dist(0, sz - 1);

    std::vector<std::string> keys;
    std::vector<std::string> expected;
    keys.reserve(ops);
    expected.reserve(ops);

    for (int i = 0; i < ops; ++i)
    {
        int idx = dist(rng);
        keys.push_back(std::to_string(idx));
        expected.push_back(std::to_string(idx));
    }

    int misses = 0;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ops; ++i)
    {
        std::string val = storage.get(keys[i]);
        if (val != expected[i])
            ++misses;
    }
    auto end = std::chrono::steady_clock::now();

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double avg_ns = double(total_ns) / ops;

    std::cout << "Average latency: " << avg_ns << " ns\n";

    return 0;
}
// get storage restored from log_manager
// Check it's size
// and do random get operations for 10% of values and keep track how much time it took
// take average of that and print that average