load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "app",
    srcs = ["main.cpp"],
    deps = ["//kvstore"],
    copts = ["-std=c++17"],
)
