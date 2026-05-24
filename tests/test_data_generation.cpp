#include <iostream>
#include <fstream>
#include <string>
#include "kvstore/log_manager.h"
#include "tests/test_data_generation.h"

void generate_test_data(fs::path &oLogFilePath, Storage &oStorage, bool ilog_to_storage, unsigned int num_entries)
{
    fs::path logfile = "data/data.log";
    fs::create_directories(logfile.parent_path());

    // Truncate or create the log file so LogManager starts fresh
    std::ofstream ofs(logfile.string(), std::ios::trunc);
    ofs.close();

    fs::path abspath = fs::absolute(logfile);
    if (!abspath.empty())
    {
        std::cout << "Using log file: " << abspath << std::endl;
        oLogFilePath = abspath;

        LogManager log_manager(abspath.string(), oStorage);

        for (unsigned int i = 0; i < num_entries; i++)
        {
            std::string key = std::to_string(i);
            std::string value = std::to_string(i);
            log_manager.log_put(key, value, ilog_to_storage);
            if (i > 0 && (i == num_entries / 4 || i == num_entries / 2 || i == 3 * num_entries / 4 || i == num_entries - 1))
            {
                int percentage = (i * 100) / num_entries;
                std::cout << "generated " << i << " entries (" << percentage << "%)\n";
            }
        }
    }
}
