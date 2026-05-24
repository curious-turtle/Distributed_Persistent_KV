#ifndef KVSTORE_STORAGE_H
#define KVSTORE_STORAGE_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class Storage
{
public:
    Storage(const std::string &data_dir = "data/sstables",
            std::size_t memtable_flush_threshold = 10000,
            std::size_t compaction_trigger = 4);
    ~Storage();

    void put(const std::string &key, const std::string &value);
    std::string get(const std::string &key);
    bool remove(const std::string &key);
    bool exists(const std::string &key);
    int size() const;

private:
    struct Entry
    {
        bool deleted = false;
        std::string value;
    };

    fs::path data_dir_;
    std::size_t memtable_flush_threshold_;
    std::size_t compaction_trigger_;
    std::size_t next_table_id_ = 0;

    std::map<std::string, Entry> memtable_;
    std::vector<fs::path> sstable_paths_;

    void flush_memtable_to_sstable();
    void maybe_compact();
    void compact_sstables();
    void load_existing_sstables();
    bool lookup_in_sstable(const fs::path &sstable_path, const std::string &key, Entry &out) const;
    std::size_t extract_table_id(const fs::path &p) const;
};

#endif // KVSTORE_STORAGE_H
