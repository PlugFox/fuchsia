// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_metrics.h"

#include <lib/fdio/namespace.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

namespace {

class StorageMetricsTest : public ::testing::Test {
 public:
  static constexpr const char* kTestRoot = "/test_storage";
  static constexpr const char* kPersistentPath = "/test_storage/persistent";
  static constexpr const char* kCachePath = "/test_storage/cache";

  StorageMetricsTest() : loop_(async::Loop(&kAsyncLoopConfigAttachToCurrentThread)) {}

  void SetUp() override {
    testing::Test::SetUp();
    ASSERT_EQ(ZX_OK,
              memfs_create_filesystem(loop_.dispatcher(), &memfs_handle_, &memfs_root_handle_));
    ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&ns_));
    ASSERT_EQ(ZX_OK, fdio_ns_bind(ns_, kTestRoot, memfs_root_handle_));

    ASSERT_EQ(ZX_OK, loop_.StartThread());
    files::CreateDirectory(kPersistentPath);
    files::CreateDirectory(kCachePath);
    std::vector<std::string> watch = {kPersistentPath, kCachePath};
    // The dispatcher is used only for delaying calls. Setting it as null here since we don't
    // want any delayed actions and it would actually deadlock if we set it share the existing
    // loop_ dispatcher. Best to have it fail fast if anyone tries to do that.
    metrics_ = std::make_unique<StorageMetrics>(std::move(watch));
  }
  // Set up the async loop, create memfs, install memfs at /hippo_storage
  void TearDown() override {
    // Unbind memfs from our namespace, free memfs
    ASSERT_EQ(ZX_OK, fdio_ns_unbind(ns_, kTestRoot));

    sync_completion_t memfs_freed_signal;
    memfs_free_filesystem(memfs_handle_, &memfs_freed_signal);
    ASSERT_EQ(ZX_OK, sync_completion_wait(&memfs_freed_signal, ZX_SEC(5)));
  }

 protected:
  std::unique_ptr<StorageMetrics> metrics_;

 private:
  async::Loop loop_;
  memfs_filesystem_t* memfs_handle_;
  zx_handle_t memfs_root_handle_;
  fdio_ns_t* ns_;
};

// Basic test with two components.
TEST_F(StorageMetricsTest, TwoComponents) {
  // Two components each with a single file. One empty, one at the minimum size.
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "67890"));
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "1", 1), 1);
  }
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "67890/other").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }

  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();

  // Expect one file each.
  ASSERT_EQ(usage.map().at("12345").inodes, 1ul);
  ASSERT_EQ(usage.map().at("67890").inodes, 1ul);

  // Expect one file with non-zero size and one with zero size.
  ASSERT_GT(usage.map().at("12345").bytes, 0ul);
  ASSERT_EQ(usage.map().at("67890").bytes, 0ul);
}

// Verify that we recurse into subdirectories.
TEST_F(StorageMetricsTest, CountSubdirectories) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345/subdir"));
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "12345/subdir/other").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }

  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();

  // 3 Total files
  ASSERT_EQ(usage.map().at("12345").inodes, 3ul);
  // Specifically not testing byte count here. Memfs diverges from Minfs (and probably all normal
  // filesystems) in that it does not reserve blocks for directory listings, since it lives in
  // memory anyways, it just puts it on the heap.
}

// Ensure that we're counting reserved blocks and not just bytes usage.
TEST_F(StorageMetricsTest, IncrementByBlocks) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "1", 1), 1);
  }

  // Check block size, the one byte file will allocate an entire block.
  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();
  ASSERT_EQ(usage.map().at("12345").inodes, 1ul);
  ASSERT_GT(usage.map().at("12345").bytes, 0ul);
  size_t block_size = usage.map().at("12345").bytes;
  ASSERT_GT(block_size, 1ul) << "Memfs block size is 1, so we can't verify block increments.";

  // Reopen file and make it 1 byte longer, it should not change the size.
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "12", 2), 2);
  }
  usage = metrics_->GatherStorageUsage();
  ASSERT_EQ(usage.map().at("12345").bytes, block_size);

  // Reopen file and make it block_size + 1 to make the result 2 * block_size
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR));
    ASSERT_GE(fd.get(), 0);
    const std::string data = "1234567890";
    size_t length = block_size + 1;
    while (length > 0) {
      // Cast ssize_t to match types with write(). Both inputs should be relatively small.
      ssize_t to_write = static_cast<ssize_t>(std::min(data.length(), length));
      ASSERT_GT(to_write, 0) << "Attempting to write a negative size";
      ASSERT_EQ(write(fd.get(), data.c_str(), to_write), to_write);
      length -= to_write;
    }
  }
  usage = metrics_->GatherStorageUsage();
  ASSERT_EQ(usage.map().at("12345").bytes, block_size * 2);
}

// Empty component dir
TEST_F(StorageMetricsTest, EmptyComponent) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();
  ASSERT_EQ(usage.map().at("12345").inodes, 0ul);
  ASSERT_EQ(usage.map().at("12345").bytes, 0ul);
}

// Mix cache and persistent directories
TEST_F(StorageMetricsTest, MultipleWatchPaths) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kCachePath, "12345"));
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kCachePath, "12345/other").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }

  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();

  ASSERT_EQ(usage.map().at("12345").inodes, 2ul);
  ASSERT_EQ(usage.map().at("12345").bytes, 0ul);
}

// Nested Realm gets included.
TEST_F(StorageMetricsTest, RealmNesting) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/r"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/r/admin/67890"));
  {
    fbl::unique_fd fd(
        open(files::JoinPath(kPersistentPath, "r/sys/12345/afile").c_str(), O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "r/sys/r/admin/67890/other").c_str(),
                           O_RDWR | O_CREAT));
    ASSERT_GE(fd.get(), 0);
  }

  StorageMetrics::UsageMap usage = metrics_->GatherStorageUsage();

  ASSERT_EQ(usage.map().at("12345").inodes, 1ul);
  ASSERT_EQ(usage.map().at("67890").inodes, 1ul);
}

}  // namespace
