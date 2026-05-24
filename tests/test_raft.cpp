#include <filesystem>
#include <iostream>
#include <vector>

#include "raft/raft_node.h"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    const fs::path test_dir = "data/raft_test";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    raft::RaftNode node1("node1", {"node2", "node3"}, (test_dir / "node1").string());
    raft::RaftNode node2("node2", {"node1", "node3"}, (test_dir / "node2").string());
    raft::RaftNode node3("node3", {"node1", "node2"}, (test_dir / "node3").string());

    std::vector<raft::RaftNode *> cluster = {&node1, &node2, &node3};

    if (!node1.start_election(cluster))
    {
        std::cerr << "node1 failed to win election\n";
        return 1;
    }
    if (node1.role() != raft::Role::Leader)
    {
        std::cerr << "node1 should be leader after election\n";
        return 2;
    }

    if (!node1.propose_put("alpha", "1", cluster))
    {
        std::cerr << "failed to replicate put(alpha)\n";
        return 3;
    }
    if (!node1.propose_put("beta", "2", cluster))
    {
        std::cerr << "failed to replicate put(beta)\n";
        return 4;
    }
    if (!node1.propose_delete("alpha", cluster))
    {
        std::cerr << "failed to replicate delete(alpha)\n";
        return 5;
    }

    for (raft::RaftNode *node : cluster)
    {
        if (node->contains("alpha"))
        {
            std::cerr << node->node_id() << " should not contain alpha after delete\n";
            return 6;
        }
        if (node->get("beta") != "2")
        {
            std::cerr << node->node_id() << " missing replicated value for beta\n";
            return 7;
        }
    }

    if (!node1.create_snapshot())
    {
        std::cerr << "leader failed to create snapshot\n";
        return 8;
    }
    if (node1.snapshot_index() != node1.committed_index())
    {
        std::cerr << "snapshot index should match committed index\n";
        return 9;
    }

    fs::remove_all(test_dir / "node3");
    raft::RaftNode restarted_follower("node3", {"node1", "node2"}, (test_dir / "node3").string());
    cluster[2] = &restarted_follower;

    if (!node1.propose_put("gamma", "3", cluster))
    {
        std::cerr << "failed to replicate gamma after follower restart\n";
        return 10;
    }
    if (restarted_follower.get("beta") != "2" || restarted_follower.get("gamma") != "3")
    {
        std::cerr << "restarted follower did not recover via snapshot + log catch-up\n";
        return 11;
    }

    raft::RaftNode recovered_leader("node1", {"node2", "node3"}, (test_dir / "node1").string());
    if (recovered_leader.get("beta") != "2")
    {
        std::cerr << "recovered leader missing beta from snapshot recovery\n";
        return 12;
    }
    if (recovered_leader.get("gamma") != "3")
    {
        std::cerr << "recovered leader missing gamma from persisted log recovery\n";
        return 13;
    }
    if (recovered_leader.contains("alpha"))
    {
        std::cerr << "recovered leader should preserve delete(alpha)\n";
        return 14;
    }

    std::cout << "Raft election, replication, and snapshot recovery passed\n";
    return 0;
}
