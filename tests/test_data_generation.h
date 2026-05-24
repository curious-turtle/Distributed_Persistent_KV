#ifndef TEST_DATA_GENERATION_H
#define TEST_DATA_GENERATION_H

#include <filesystem>
#include "kvstore/storage.h"

namespace fs = std::filesystem;
void generate_test_data(fs::path &oLogFilePath, Storage &oStorage, bool ilog_to_storage = true, unsigned int num_entries = 1000000);

#endif // TEST_DATA_GENERATION_H
