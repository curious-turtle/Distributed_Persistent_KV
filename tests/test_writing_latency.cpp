#include <iostream>
#include <chrono>
#include <filesystem>
#include "tests/test_data_generation.h"

int main(int argc, char **argv)
{
    fs::path logfile = "";
    Storage storage;
    unsigned int num_entries = 40000; // Adjust this number as needed for testing

    auto start = std::chrono::high_resolution_clock::now();
    generate_test_data(logfile, storage, true, num_entries);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Wrote " << num_entries << " entries in " << duration_ms << " ms" << std::endl;

    return 0;
}