#include "storage.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

Storage::Storage(const std::string &data_dir, std::size_t memtable_flush_threshold, std::size_t compaction_trigger)
    : data_dir_(data_dir),
      memtable_flush_threshold_(memtable_flush_threshold),
      compaction_trigger_(compaction_trigger)
{
    fs::create_directories(data_dir_);
    load_existing_sstables();
}

Storage::~Storage()
{
    if (!memtable_.empty())
    {
        flush_memtable_to_sstable();
    }
}

void Storage::put(const std::string &key, const std::string &value)
{
    memtable_[key] = Entry{false, value};
    if (memtable_.size() >= memtable_flush_threshold_)
    {
        flush_memtable_to_sstable();
        maybe_compact();
    }
}

std::string Storage::get(const std::string &key)
{
    auto mem_it = memtable_.find(key);
    if (mem_it != memtable_.end())
    {
        return mem_it->second.deleted ? "" : mem_it->second.value;
    }

    Entry found;
    for (auto it = sstable_paths_.rbegin(); it != sstable_paths_.rend(); ++it)
    {
        if (lookup_in_sstable(*it, key, found))
        {
            return found.deleted ? "" : found.value;
        }
    }
    return "";
}

bool Storage::remove(const std::string &key)
{
    bool existed = exists(key);
    memtable_[key] = Entry{true, ""};
    if (memtable_.size() >= memtable_flush_threshold_)
    {
        flush_memtable_to_sstable();
        maybe_compact();
    }
    return existed;
}

bool Storage::exists(const std::string &key)
{
    auto mem_it = memtable_.find(key);
    if (mem_it != memtable_.end())
    {
        return !mem_it->second.deleted;
    }

    Entry found;
    for (auto it = sstable_paths_.rbegin(); it != sstable_paths_.rend(); ++it)
    {
        if (lookup_in_sstable(*it, key, found))
        {
            return !found.deleted;
        }
    }
    return false;
}

int Storage::size() const
{
    std::unordered_map<std::string, Entry> latest;

    for (const auto &table_path : sstable_paths_)
    {
        std::ifstream in(table_path);
        std::string op, key, value;
        while (in >> op >> key)
        {
            if (op == "P")
            {
                in >> value;
                latest[key] = Entry{false, value};
            }
            else if (op == "D")
            {
                latest[key] = Entry{true, ""};
            }
        }
    }

    for (const auto &kv : memtable_)
    {
        latest[kv.first] = kv.second;
    }

    int alive = 0;
    for (const auto &kv : latest)
    {
        if (!kv.second.deleted)
        {
            alive++;
        }
    }
    return alive;
}

void Storage::flush_memtable_to_sstable()
{
    if (memtable_.empty())
    {
        return;
    }

    fs::path file_path = data_dir_ / ("sstable_" + std::to_string(next_table_id_++) + ".sst");
    std::ofstream out(file_path, std::ios::trunc);
    for (const auto &kv : memtable_)
    {
        if (kv.second.deleted)
        {
            out << "D " << kv.first << "\n";
        }
        else
        {
            out << "P " << kv.first << " " << kv.second.value << "\n";
        }
    }
    out.flush();
    out.close();

    sstable_paths_.push_back(file_path);
    memtable_.clear();
}

void Storage::maybe_compact()
{
    if (sstable_paths_.size() >= compaction_trigger_)
    {
        compact_sstables();
    }
}

void Storage::compact_sstables()
{
    if (sstable_paths_.size() < 2)
    {
        return;
    }

    std::map<std::string, Entry> merged;
    for (const auto &table_path : sstable_paths_)
    {
        std::ifstream in(table_path);
        std::string op, key, value;
        while (in >> op >> key)
        {
            if (op == "P")
            {
                in >> value;
                merged[key] = Entry{false, value};
            }
            else if (op == "D")
            {
                merged[key] = Entry{true, ""};
            }
        }
    }

    fs::path compacted = data_dir_ / ("sstable_" + std::to_string(next_table_id_++) + "_compacted.sst");
    std::ofstream out(compacted, std::ios::trunc);
    for (const auto &kv : merged)
    {
        if (!kv.second.deleted)
        {
            out << "P " << kv.first << " " << kv.second.value << "\n";
        }
    }
    out.flush();
    out.close();

    for (const auto &old_table : sstable_paths_)
    {
        fs::remove(old_table);
    }

    sstable_paths_.clear();
    sstable_paths_.push_back(compacted);
}

void Storage::load_existing_sstables()
{
    sstable_paths_.clear();
    next_table_id_ = 0;

    if (!fs::exists(data_dir_))
    {
        return;
    }

    for (const auto &entry : fs::directory_iterator(data_dir_))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const fs::path &p = entry.path();
        if (p.extension() == ".sst" && p.filename().string().find("sstable_") == 0)
        {
            sstable_paths_.push_back(p);
            next_table_id_ = std::max(next_table_id_, extract_table_id(p) + 1);
        }
    }

    std::sort(sstable_paths_.begin(), sstable_paths_.end(), [this](const fs::path &a, const fs::path &b) {
        return extract_table_id(a) < extract_table_id(b);
    });
}

bool Storage::lookup_in_sstable(const fs::path &sstable_path, const std::string &key, Entry &out) const
{
    std::ifstream in(sstable_path);
    std::string op, k, value;
    while (in >> op >> k)
    {
        if (op == "P")
        {
            in >> value;
            if (k == key)
            {
                out = Entry{false, value};
                return true;
            }
        }
        else if (op == "D")
        {
            if (k == key)
            {
                out = Entry{true, ""};
                return true;
            }
        }
    }
    return false;
}

std::size_t Storage::extract_table_id(const fs::path &p) const
{
    const std::string filename = p.filename().string();
    const std::string prefix = "sstable_";
    if (filename.rfind(prefix, 0) != 0)
    {
        return 0;
    }
    std::size_t pos = prefix.size();
    std::size_t value = 0;
    while (pos < filename.size() && std::isdigit(static_cast<unsigned char>(filename[pos])))
    {
        value = value * 10 + (filename[pos] - '0');
        pos++;
    }
    return value;
}
