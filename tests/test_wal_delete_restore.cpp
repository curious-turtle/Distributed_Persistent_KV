#include <filesystem>
#include <fstream>
#include <iostream>

#include "kvstore/log_manager.h"
#include "kvstore/storage.h"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    const fs::path test_dir = "data/wal_delete_restore";
    const fs::path sstable_dir = test_dir / "sstables";
    const fs::path logfile = test_dir / "wal.log";

    fs::remove_all(test_dir);
    fs::create_directories(sstable_dir);

    {
        Storage storage(sstable_dir.string(), 100);
        LogManager log_manager(logfile.string(), storage);
        log_manager.log_put("a", "1");
        log_manager.log_put("b", "2");
        log_manager.log_delete("a");
    }

    Storage restored_storage(sstable_dir.string(), 100);
    LogManager restored_log_manager(logfile.string(), restored_storage);
    restored_log_manager.restore();

    if (restored_storage.exists("a"))
    {
        std::cerr << "Deleted key a should not reappear after WAL restore\n";
        return 1;
    }
    if (restored_storage.get("b") != "2")
    {
        std::cerr << "Expected b=2 after WAL restore\n";
        return 2;
    }

    std::cout << "WAL delete restore passed\n";
    return 0;
}
