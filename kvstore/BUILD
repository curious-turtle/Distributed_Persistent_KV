load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "kvstore",
    srcs = [
        "log_manager.cpp",
        "storage.cpp",
    ],
    hdrs = [
        "log_manager.h",
        "storage.h",
    ],
    visibility = ["//visibility:public"],
    copts = ["-std=c++17"],
)
