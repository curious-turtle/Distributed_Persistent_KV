#ifndef KVSTORE_LOG_MANAGER_H
#define KVSTORE_LOG_MANAGER_H

#include <string>
#include <fstream>
#include <chrono>

class Storage;

class LogManager
{
public:
    LogManager(const std::string &log_file, Storage &storage);
    ~LogManager();

    void log_put(const std::string &key, const std::string &value, bool log_to_storage = true);
    void restore();
    Storage &get_storage() { return storage_; }

private:
    int fd;
    std::string log_file_;
    Storage &storage_;
    int batch_count = 0;
    int batch_size = 200;
    std::chrono::steady_clock::time_point last_flush_;
    int flush_interval_ms = 5;
};

#endif // KVSTORE_LOG_MANAGER_H
