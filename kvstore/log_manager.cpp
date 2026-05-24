#include "log_manager.h"
#include "storage.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>

LogManager::LogManager(const std::string &log_file, Storage &storage) : log_file_(log_file), storage_(storage)
{
    fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        std::cerr << "Failed to open log file: " << log_file << std::endl;
        exit(1);
    }
    last_flush_ = std::chrono::steady_clock::now();
}

LogManager::~LogManager()
{
    if (fd >= 0)
    {
        fsync(fd);
        close(fd);
    }
}

void LogManager::log_put(const std::string &key, const std::string &value, bool log_to_storage)
{
    std::string record = "P " + key + " " + value + "\n";

    ssize_t written = write(fd, record.c_str(), record.size());
    if (written < 0)
    {
        perror("write");
        return;
    }

    batch_count++;
    auto now = std::chrono::steady_clock::now();
    auto ms_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_).count();

    if (batch_count >= batch_size || ms_since_last >= flush_interval_ms)
    {
        fsync(fd);
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
