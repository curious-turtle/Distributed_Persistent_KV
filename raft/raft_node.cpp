#include "raft/raft_node.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace raft
{

namespace
{
const char *kMetadataFile = "metadata.txt";
const char *kLogFile = "raft.log";
const char *kSnapshotFile = "snapshot.txt";
}

RaftNode::RaftNode(std::string node_id, std::vector<std::string> peer_ids, const std::string &data_dir)
    : node_id_(std::move(node_id)),
      peer_ids_(std::move(peer_ids)),
      data_dir_(data_dir)
{
    fs::create_directories(data_dir_);
    load_from_disk();
}

bool RaftNode::start_election(const std::vector<RaftNode *> &peers)
{
    role_ = Role::Candidate;
    current_term_++;
    voted_for_ = node_id_;
    persist_metadata();

    RequestVoteRequest request;
    request.term = current_term_;
    request.candidate_id = node_id_;
    request.last_log_index = last_log_index();
    request.last_log_term = last_log_term();

    std::size_t votes = 1;
    for (RaftNode *peer : peers)
    {
        if (peer == nullptr || peer->node_id() == node_id_)
        {
            continue;
        }

        RequestVoteResponse response = peer->handle_request_vote(request);
        if (response.term > current_term_)
        {
            become_follower(response.term);
            return false;
        }
        if (response.vote_granted)
        {
            votes++;
        }
    }

    if (votes >= majority_count())
    {
        become_leader();
        return true;
    }

    role_ = Role::Follower;
    persist_metadata();
    return false;
}

bool RaftNode::propose_put(const std::string &key, const std::string &value, const std::vector<RaftNode *> &peers)
{
    return replicate_command(Command{CommandType::Put, key, value}, peers);
}

bool RaftNode::propose_delete(const std::string &key, const std::vector<RaftNode *> &peers)
{
    return replicate_command(Command{CommandType::Delete, key, ""}, peers);
}

bool RaftNode::create_snapshot()
{
    if (commit_index_ <= snapshot_.last_included_index)
    {
        return false;
    }

    apply_committed_entries();
    snapshot_.last_included_index = commit_index_;
    snapshot_.last_included_term = term_at(commit_index_);
    snapshot_.state = state_machine_;

    log_.erase(
        std::remove_if(
            log_.begin(),
            log_.end(),
            [this](const LogEntry &entry)
            {
                return entry.index <= snapshot_.last_included_index;
            }),
        log_.end());

    persist_snapshot();
    persist_log();
    persist_metadata();
    return true;
}

RequestVoteResponse RaftNode::handle_request_vote(const RequestVoteRequest &request)
{
    if (request.term < current_term_)
    {
        return RequestVoteResponse{current_term_, false};
    }

    if (request.term > current_term_)
    {
        become_follower(request.term);
    }

    const bool can_vote = voted_for_.empty() || voted_for_ == request.candidate_id;
    const bool candidate_up_to_date = is_candidate_log_up_to_date(request.last_log_index, request.last_log_term);

    if (can_vote && candidate_up_to_date)
    {
        voted_for_ = request.candidate_id;
        role_ = Role::Follower;
        persist_metadata();
        return RequestVoteResponse{current_term_, true};
    }

    return RequestVoteResponse{current_term_, false};
}

AppendEntriesResponse RaftNode::handle_append_entries(const AppendEntriesRequest &request)
{
    if (request.term < current_term_)
    {
        return AppendEntriesResponse{current_term_, false, last_log_index()};
    }

    if (request.term > current_term_ || role_ != Role::Follower)
    {
        become_follower(request.term);
    }

    if (request.prev_log_index < snapshot_.last_included_index)
    {
        return AppendEntriesResponse{current_term_, false, snapshot_.last_included_index};
    }

    if (request.prev_log_index > last_log_index())
    {
        return AppendEntriesResponse{current_term_, false, last_log_index()};
    }

    if (request.prev_log_index > 0 && term_at(request.prev_log_index) != request.prev_log_term)
    {
        truncate_log_from(request.prev_log_index);
        persist_log();
        persist_metadata();
        return AppendEntriesResponse{current_term_, false, last_log_index()};
    }

    for (const LogEntry &entry : request.entries)
    {
        if (entry.index <= snapshot_.last_included_index)
        {
            continue;
        }

        if (entry.index <= last_log_index())
        {
            if (term_at(entry.index) != entry.term)
            {
                truncate_log_from(entry.index);
                append_entry(entry);
            }
        }
        else
        {
            append_entry(entry);
        }
    }

    if (request.leader_commit > commit_index_)
    {
        commit_index_ = std::min(request.leader_commit, last_log_index());
        apply_committed_entries();
    }

    persist_log();
    persist_metadata();
    return AppendEntriesResponse{current_term_, true, last_log_index()};
}

InstallSnapshotResponse RaftNode::handle_install_snapshot(const InstallSnapshotRequest &request)
{
    if (request.term < current_term_)
    {
        return InstallSnapshotResponse{current_term_, false};
    }

    if (request.term > current_term_ || role_ != Role::Follower)
    {
        become_follower(request.term);
    }

    if (request.snapshot.last_included_index <= snapshot_.last_included_index)
    {
        return InstallSnapshotResponse{current_term_, true};
    }

    snapshot_ = request.snapshot;
    state_machine_ = snapshot_.state;
    log_.erase(
        std::remove_if(
            log_.begin(),
            log_.end(),
            [this](const LogEntry &entry)
            {
                return entry.index <= snapshot_.last_included_index;
            }),
        log_.end());

    commit_index_ = std::max(commit_index_, snapshot_.last_included_index);
    last_applied_ = std::max(last_applied_, snapshot_.last_included_index);

    persist_snapshot();
    persist_log();
    persist_metadata();
    return InstallSnapshotResponse{current_term_, true};
}

std::string RaftNode::get(const std::string &key) const
{
    auto it = state_machine_.find(key);
    return it == state_machine_.end() ? "" : it->second;
}

bool RaftNode::contains(const std::string &key) const
{
    return state_machine_.find(key) != state_machine_.end();
}

bool RaftNode::replicate_command(const Command &command, const std::vector<RaftNode *> &peers)
{
    if (role_ != Role::Leader)
    {
        return false;
    }

    LogEntry entry;
    entry.index = last_log_index() + 1;
    entry.term = current_term_;
    entry.command = command;
    append_entry(entry);
    persist_log();

    std::size_t replicated = 1;
    for (RaftNode *peer : peers)
    {
        if (peer == nullptr || peer->node_id() == node_id_)
        {
            continue;
        }

        if (replicate_to_peer(*peer))
        {
            replicated++;
        }
        else if (role_ != Role::Leader)
        {
            return false;
        }
    }

    if (replicated < majority_count())
    {
        return false;
    }

    std::vector<std::size_t> match_indexes;
    match_indexes.push_back(last_log_index());
    for (const auto &kv : match_index_)
    {
        match_indexes.push_back(kv.second);
    }
    std::sort(match_indexes.begin(), match_indexes.end(), std::greater<std::size_t>());

    const std::size_t majority_match = match_indexes[majority_count() - 1];
    if (majority_match > commit_index_ && term_at(majority_match) == current_term_)
    {
        commit_index_ = majority_match;
        apply_committed_entries();
        persist_metadata();
    }

    for (RaftNode *peer : peers)
    {
        if (peer == nullptr || peer->node_id() == node_id_)
        {
            continue;
        }
        replicate_to_peer(*peer);
    }

    return commit_index_ >= entry.index;
}

bool RaftNode::replicate_to_peer(RaftNode &peer)
{
    const std::string &peer_id = peer.node_id();
    if (next_index_.find(peer_id) == next_index_.end())
    {
        next_index_[peer_id] = last_log_index() + 1;
        match_index_[peer_id] = 0;
    }

    while (true)
    {
        if (next_index_[peer_id] <= snapshot_.last_included_index)
        {
            if (!send_snapshot(peer))
            {
                return false;
            }
            next_index_[peer_id] = snapshot_.last_included_index + 1;
            match_index_[peer_id] = snapshot_.last_included_index;
        }

        AppendEntriesRequest request;
        request.term = current_term_;
        request.leader_id = node_id_;
        request.prev_log_index = next_index_[peer_id] == 0 ? 0 : next_index_[peer_id] - 1;
        request.prev_log_term = request.prev_log_index == 0 ? 0 : term_at(request.prev_log_index);
        request.entries = entries_from(next_index_[peer_id]);
        request.leader_commit = commit_index_;

        AppendEntriesResponse response = peer.handle_append_entries(request);
        if (response.term > current_term_)
        {
            become_follower(response.term);
            return false;
        }

        if (response.success)
        {
            match_index_[peer_id] = response.match_index;
            next_index_[peer_id] = response.match_index + 1;
            return true;
        }

        if (next_index_[peer_id] == 0)
        {
            return false;
        }
        next_index_[peer_id]--;
    }
}

bool RaftNode::send_snapshot(RaftNode &peer)
{
    InstallSnapshotRequest request;
    request.term = current_term_;
    request.leader_id = node_id_;
    request.snapshot = snapshot_;

    InstallSnapshotResponse response = peer.handle_install_snapshot(request);
    if (response.term > current_term_)
    {
        become_follower(response.term);
        return false;
    }
    return response.success;
}

void RaftNode::become_follower(int new_term)
{
    current_term_ = new_term;
    voted_for_.clear();
    role_ = Role::Follower;
    persist_metadata();
}

void RaftNode::become_leader()
{
    role_ = Role::Leader;
    for (const std::string &peer_id : peer_ids_)
    {
        next_index_[peer_id] = last_log_index() + 1;
        match_index_[peer_id] = 0;
    }
    persist_metadata();
}

void RaftNode::apply_committed_entries()
{
    while (last_applied_ < commit_index_)
    {
        last_applied_++;
        if (last_applied_ <= snapshot_.last_included_index)
        {
            continue;
        }

        const std::size_t offset = last_applied_ - snapshot_.last_included_index - 1;
        if (offset >= log_.size())
        {
            break;
        }
        apply_entry(log_[offset]);
    }
    persist_metadata();
}

void RaftNode::apply_entry(const LogEntry &entry)
{
    if (entry.command.type == CommandType::Delete)
    {
        state_machine_.erase(entry.command.key);
        return;
    }
    state_machine_[entry.command.key] = entry.command.value;
}

void RaftNode::append_entry(const LogEntry &entry)
{
    log_.push_back(entry);
}

void RaftNode::truncate_log_from(std::size_t index)
{
    log_.erase(
        std::remove_if(
            log_.begin(),
            log_.end(),
            [index](const LogEntry &entry)
            {
                return entry.index >= index;
            }),
        log_.end());

    if (commit_index_ >= index)
    {
        commit_index_ = index - 1;
    }
    if (last_applied_ >= index)
    {
        last_applied_ = index - 1;
        state_machine_ = snapshot_.state;
        for (const LogEntry &entry : log_)
        {
            if (entry.index <= commit_index_)
            {
                apply_entry(entry);
                last_applied_ = entry.index;
            }
        }
    }
}

bool RaftNode::is_candidate_log_up_to_date(std::size_t last_log_index, int last_log_term) const
{
    const int local_last_term = this->last_log_term();
    if (last_log_term != local_last_term)
    {
        return last_log_term > local_last_term;
    }
    return last_log_index >= this->last_log_index();
}

std::size_t RaftNode::last_log_index() const
{
    return log_.empty() ? snapshot_.last_included_index : log_.back().index;
}

int RaftNode::last_log_term() const
{
    return log_.empty() ? snapshot_.last_included_term : log_.back().term;
}

int RaftNode::term_at(std::size_t index) const
{
    if (index == 0)
    {
        return 0;
    }
    if (index == snapshot_.last_included_index)
    {
        return snapshot_.last_included_term;
    }
    if (index < snapshot_.last_included_index)
    {
        return 0;
    }

    const std::size_t offset = index - snapshot_.last_included_index - 1;
    if (offset >= log_.size())
    {
        return 0;
    }
    return log_[offset].term;
}

std::vector<LogEntry> RaftNode::entries_from(std::size_t index) const
{
    std::vector<LogEntry> entries;
    if (index <= snapshot_.last_included_index)
    {
        return entries;
    }

    const std::size_t offset = index - snapshot_.last_included_index - 1;
    if (offset >= log_.size())
    {
        return entries;
    }

    entries.insert(entries.end(), log_.begin() + static_cast<std::ptrdiff_t>(offset), log_.end());
    return entries;
}

std::size_t RaftNode::majority_count() const
{
    return (peer_ids_.size() + 1) / 2 + 1;
}

RaftNode *RaftNode::find_peer(const std::string &peer_id, const std::vector<RaftNode *> &peers) const
{
    for (RaftNode *peer : peers)
    {
        if (peer != nullptr && peer->node_id() == peer_id)
        {
            return peer;
        }
    }
    return nullptr;
}

void RaftNode::load_from_disk()
{
    snapshot_ = Snapshot{};
    log_.clear();
    state_machine_.clear();
    current_term_ = 0;
    voted_for_.clear();
    commit_index_ = 0;
    last_applied_ = 0;
    role_ = Role::Follower;

    std::ifstream snapshot_in(data_dir_ / kSnapshotFile);
    if (snapshot_in.is_open())
    {
        std::size_t entry_count = 0;
        snapshot_in >> snapshot_.last_included_index >> snapshot_.last_included_term >> entry_count;
        for (std::size_t i = 0; i < entry_count; ++i)
        {
            std::string key;
            std::string value;
            snapshot_in >> std::quoted(key) >> std::quoted(value);
            snapshot_.state[key] = value;
        }
        state_machine_ = snapshot_.state;
        commit_index_ = snapshot_.last_included_index;
        last_applied_ = snapshot_.last_included_index;
    }

    std::ifstream metadata_in(data_dir_ / kMetadataFile);
    if (metadata_in.is_open())
    {
        metadata_in >> current_term_ >> std::quoted(voted_for_) >> commit_index_ >> last_applied_;
    }

    std::ifstream log_in(data_dir_ / kLogFile);
    if (log_in.is_open())
    {
        while (log_in.good())
        {
            LogEntry entry;
            std::string command_type;
            if (!(log_in >> entry.index >> entry.term >> command_type))
            {
                break;
            }
            log_in >> std::quoted(entry.command.key) >> std::quoted(entry.command.value);
            entry.command.type = command_type == "DELETE" ? CommandType::Delete : CommandType::Put;
            log_.push_back(entry);
        }
    }

    commit_index_ = std::max(commit_index_, snapshot_.last_included_index);
    last_applied_ = snapshot_.last_included_index;
    apply_committed_entries();
}

void RaftNode::persist_metadata() const
{
    std::ofstream out(data_dir_ / kMetadataFile, std::ios::trunc);
    out << current_term_ << ' '
        << std::quoted(voted_for_) << ' '
        << commit_index_ << ' '
        << last_applied_ << '\n';
}

void RaftNode::persist_log() const
{
    std::ofstream out(data_dir_ / kLogFile, std::ios::trunc);
    for (const LogEntry &entry : log_)
    {
        out << entry.index << ' '
            << entry.term << ' '
            << (entry.command.type == CommandType::Delete ? "DELETE" : "PUT") << ' '
            << std::quoted(entry.command.key) << ' '
            << std::quoted(entry.command.value) << '\n';
    }
}

void RaftNode::persist_snapshot() const
{
    std::ofstream out(data_dir_ / kSnapshotFile, std::ios::trunc);
    out << snapshot_.last_included_index << ' '
        << snapshot_.last_included_term << ' '
        << snapshot_.state.size() << '\n';

    for (const auto &kv : snapshot_.state)
    {
        out << std::quoted(kv.first) << ' ' << std::quoted(kv.second) << '\n';
    }
}

} // namespace raft
