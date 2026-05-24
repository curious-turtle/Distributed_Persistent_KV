#ifndef RAFT_RAFT_NODE_H
#define RAFT_RAFT_NODE_H

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace raft
{
namespace fs = std::filesystem;

enum class Role
{
    Follower,
    Candidate,
    Leader,
};

enum class CommandType
{
    Put,
    Delete,
};

struct Command
{
    CommandType type = CommandType::Put;
    std::string key;
    std::string value;
};

struct LogEntry
{
    std::size_t index = 0;
    int term = 0;
    Command command;
};

struct Snapshot
{
    std::size_t last_included_index = 0;
    int last_included_term = 0;
    std::map<std::string, std::string> state;
};

struct RequestVoteRequest
{
    int term = 0;
    std::string candidate_id;
    std::size_t last_log_index = 0;
    int last_log_term = 0;
};

struct RequestVoteResponse
{
    int term = 0;
    bool vote_granted = false;
};

struct AppendEntriesRequest
{
    int term = 0;
    std::string leader_id;
    std::size_t prev_log_index = 0;
    int prev_log_term = 0;
    std::vector<LogEntry> entries;
    std::size_t leader_commit = 0;
};

struct AppendEntriesResponse
{
    int term = 0;
    bool success = false;
    std::size_t match_index = 0;
};

struct InstallSnapshotRequest
{
    int term = 0;
    std::string leader_id;
    Snapshot snapshot;
};

struct InstallSnapshotResponse
{
    int term = 0;
    bool success = false;
};

class RaftNode
{
public:
    RaftNode(std::string node_id, std::vector<std::string> peer_ids, const std::string &data_dir);

    bool start_election(const std::vector<RaftNode *> &peers);
    bool propose_put(const std::string &key, const std::string &value, const std::vector<RaftNode *> &peers);
    bool propose_delete(const std::string &key, const std::vector<RaftNode *> &peers);
    bool create_snapshot();

    RequestVoteResponse handle_request_vote(const RequestVoteRequest &request);
    AppendEntriesResponse handle_append_entries(const AppendEntriesRequest &request);
    InstallSnapshotResponse handle_install_snapshot(const InstallSnapshotRequest &request);

    std::string get(const std::string &key) const;
    bool contains(const std::string &key) const;
    std::size_t committed_index() const { return commit_index_; }
    std::size_t snapshot_index() const { return snapshot_.last_included_index; }
    std::size_t log_size() const { return log_.size(); }
    int current_term() const { return current_term_; }
    Role role() const { return role_; }
    const std::string &node_id() const { return node_id_; }

private:
    std::string node_id_;
    std::vector<std::string> peer_ids_;
    fs::path data_dir_;

    int current_term_ = 0;
    std::string voted_for_;
    Role role_ = Role::Follower;

    std::vector<LogEntry> log_;
    std::size_t commit_index_ = 0;
    std::size_t last_applied_ = 0;
    Snapshot snapshot_;
    std::map<std::string, std::string> state_machine_;

    std::map<std::string, std::size_t> next_index_;
    std::map<std::string, std::size_t> match_index_;

    bool replicate_command(const Command &command, const std::vector<RaftNode *> &peers);
    bool replicate_to_peer(RaftNode &peer);
    bool send_snapshot(RaftNode &peer);
    void become_follower(int new_term);
    void become_leader();
    void apply_committed_entries();
    void apply_entry(const LogEntry &entry);
    void append_entry(const LogEntry &entry);
    void truncate_log_from(std::size_t index);
    bool is_candidate_log_up_to_date(std::size_t last_log_index, int last_log_term) const;
    std::size_t last_log_index() const;
    int last_log_term() const;
    int term_at(std::size_t index) const;
    std::vector<LogEntry> entries_from(std::size_t index) const;
    std::size_t majority_count() const;
    RaftNode *find_peer(const std::string &peer_id, const std::vector<RaftNode *> &peers) const;

    void load_from_disk();
    void persist_metadata() const;
    void persist_log() const;
    void persist_snapshot() const;
};

} // namespace raft

#endif // RAFT_RAFT_NODE_H
