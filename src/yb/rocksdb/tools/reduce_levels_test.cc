//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#ifndef ROCKSDB_LITE

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/tools/ldb_cmd.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/util/testharness.h"

#include "yb/util/test_util.h"

namespace rocksdb {

class ReduceLevelTest : public RocksDBTest {
 public:
  ReduceLevelTest() {
    dbname_ = test::TmpDir() + "/db_reduce_levels_test";
    CHECK_OK(DestroyDB(dbname_, Options()));
    db_ = nullptr;
  }

  Status OpenDB(bool create_if_missing, int levels);

  Status Put(const std::string& k, const std::string& v) {
    return db_->Put(WriteOptions(), k, v);
  }

  std::string Get(const std::string& k) {
    ReadOptions options;
    std::string result;
    Status s = db_->Get(options, k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  Status Flush() {
    if (db_ == nullptr) {
      return STATUS(InvalidArgument, "DB not opened.");
    }
    DBImpl* db_impl = reinterpret_cast<DBImpl*>(db_);
    return db_impl->TEST_FlushMemTable();
  }

  void MoveL0FileToLevel(int level) {
    DBImpl* db_impl = reinterpret_cast<DBImpl*>(db_);
    for (int i = 0; i < level; ++i) {
      ASSERT_OK(db_impl->TEST_CompactRange(i, nullptr, nullptr));
    }
  }

  void CloseDB() {
    if (db_ != nullptr) {
      delete db_;
      db_ = nullptr;
    }
  }

  bool ReduceLevels(int target_level);

  int FilesOnLevel(int level) {
    std::string property;
    EXPECT_TRUE(db_->GetProperty(
        "rocksdb.num-files-at-level" + NumberToString(level), &property));
    return atoi(property.c_str());
  }

 private:
  std::string dbname_;
  DB* db_;
};

Status ReduceLevelTest::OpenDB(bool create_if_missing, int num_levels) {
  rocksdb::Options opt;
  opt.num_levels = num_levels;
  opt.create_if_missing = create_if_missing;
  rocksdb::Status st = rocksdb::DB::Open(opt, dbname_, &db_);
  if (!st.ok()) {
    fprintf(stderr, "Can't open the db:%s\n", st.ToString().c_str());
  }
  return st;
}

bool ReduceLevelTest::ReduceLevels(int target_level) {
  std::vector<std::string> args = rocksdb::ReduceDBLevelsCommand::PrepareArgs(
      dbname_, target_level, false);
  LDBCommand* level_reducer =
      LDBCommand::InitFromCmdLineArgs(args, Options(), LDBOptions(), nullptr);
  level_reducer->Run();
  bool is_succeed = level_reducer->GetExecuteState().IsSucceed();
  delete level_reducer;
  return is_succeed;
}

TEST_F(ReduceLevelTest, Last_Level) {
  ASSERT_OK(OpenDB(true, 4));
  ASSERT_OK(Put("aaaa", "11111"));
  ASSERT_OK(Flush());
  MoveL0FileToLevel(3);
  ASSERT_EQ(FilesOnLevel(3), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3));
  ASSERT_EQ(FilesOnLevel(2), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2));
  ASSERT_EQ(FilesOnLevel(1), 1);
  CloseDB();
}

TEST_F(ReduceLevelTest, Top_Level) {
  ASSERT_OK(OpenDB(true, 5));
  ASSERT_OK(Put("aaaa", "11111"));
  ASSERT_OK(Flush());
  ASSERT_EQ(FilesOnLevel(0), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(4));
  ASSERT_OK(OpenDB(true, 4));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2));
  CloseDB();
}

TEST_F(ReduceLevelTest, All_Levels) {
  ASSERT_OK(OpenDB(true, 5));
  ASSERT_OK(Put("a", "a11111"));
  ASSERT_OK(Flush());
  MoveL0FileToLevel(4);
  ASSERT_EQ(FilesOnLevel(4), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5));
  ASSERT_OK(Put("b", "b11111"));
  ASSERT_OK(Flush());
  MoveL0FileToLevel(3);
  ASSERT_EQ(FilesOnLevel(3), 1);
  ASSERT_EQ(FilesOnLevel(4), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5));
  ASSERT_OK(Put("c", "c11111"));
  ASSERT_OK(Flush());
  MoveL0FileToLevel(2);
  ASSERT_EQ(FilesOnLevel(2), 1);
  ASSERT_EQ(FilesOnLevel(3), 1);
  ASSERT_EQ(FilesOnLevel(4), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5));
  ASSERT_OK(Put("d", "d11111"));
  ASSERT_OK(Flush());
  MoveL0FileToLevel(1);
  ASSERT_EQ(FilesOnLevel(1), 1);
  ASSERT_EQ(FilesOnLevel(2), 1);
  ASSERT_EQ(FilesOnLevel(3), 1);
  ASSERT_EQ(FilesOnLevel(4), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(4));
  ASSERT_OK(OpenDB(true, 4));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();
}

} // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else
#include <stdio.h>

int main(int argc, char** argv) {
  fprintf(stderr, "SKIPPED as LDBCommand is not supported in ROCKSDB_LITE\n");
  return 0;
}

#endif  // !ROCKSDB_LITE
