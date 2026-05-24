load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "raft",
    srcs = ["raft_node.cpp"],
    hdrs = ["raft_node.h"],
    visibility = ["//visibility:public"],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/std:c++17"],
        "//conditions:default": ["-std=c++17"],
    }),
)
