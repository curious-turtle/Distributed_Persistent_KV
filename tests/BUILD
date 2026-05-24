load("@rules_cc//cc:defs.bzl", "cc_test")

filegroup(
    name = "test_data_generation_files",
    srcs = ["test_data_generation.cpp", "test_data_generation.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "restore_test",
    srcs = ["test_kv_restore.cpp", ":test_data_generation_files"],
    deps = ["//kvstore"],

)

cc_test(
    name = "read_latency_test",
    srcs = ["test_read_latency.cpp", ":test_data_generation_files"],
    deps = ["//kvstore"],
)


cc_test(
    name = "write_latency_test",
    srcs = ["test_writing_latency.cpp", ":test_data_generation_files"],
    deps = ["//kvstore"],
)

cc_test(
    name = "memtable_flush_compaction_test",
    srcs = ["test_memtable_flush_and_compaction.cpp"],
    deps = ["//kvstore"],
)
