#include "log_manager.h"
#include "storage.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>

LogManager::LogManager(const std::string &log_file, Storage &storage) : log_file_(log_file), storage_(storage)
{
    log_stream_.open(log_file_, std::ios::out | std::ios::app);
    if (!log_stream_.is_open())
    {
        std::cerr << "Failed to open log file: " << log_file << std::endl;
        exit(1);
    }
    last_flush_ = std::chrono::steady_clock::now();
}

LogManager::~LogManager()
{
    if (log_stream_.is_open())
    {
        log_stream_.flush();
        log_stream_.close();
    }
}

void LogManager::log_put(const std::string &key, const std::string &value, bool log_to_storage)
{
    std::string record = "P " + key + " " + value + "\n";

    log_stream_ << record;
    if (!log_stream_)
    {
        std::perror("write");
        log_stream_.clear();
        return;
    }

    batch_count++;
    auto now = std::chrono::steady_clock::now();
    auto ms_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_).count();

    if (batch_count >= batch_size || ms_since_last >= flush_interval_ms)
    {
        log_stream_.flush();
        batch_count = 0;
        last_flush_ = now;
    }

    if (log_to_storage)
    {
        storage_.put(key, value);
    }
}

void LogManager::restore()
{
    std::ifstream replay_stream(log_file_);
    std::string operation, key, value;

    while (replay_stream >> operation)
    {
        if (operation == "P")
        {
            replay_stream >> key >> value;
            storage_.put(key, value);
        }
    }
}
