// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_set.h"

#include <algorithm>
#include <string>
#include <vector>

#include "helpers/memenv/memenv.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace leveldb {

class FindFileTest {
 public:
  FindFileTest() : disjoint_sorted_files_(true) {}

  ~FindFileTest() {
    for (int i = 0; i < files_.size(); i++) {
      delete files_[i];
    }
  }

  void Add(const char* smallest, const char* largest,
           SequenceNumber smallest_seq = 100,
           SequenceNumber largest_seq = 100) {
    FileMetaData* f = new FileMetaData;
    f->number = files_.size() + 1;
    f->smallest = InternalKey(smallest, smallest_seq, kTypeValue);
    f->largest = InternalKey(largest, largest_seq, kTypeValue);
    files_.push_back(f);
  }

  int Find(const char* key) {
    InternalKey target(key, 100, kTypeValue);
    InternalKeyComparator cmp(BytewiseComparator());
    return FindFile(cmp, files_, target.Encode());
  }

  bool Overlaps(const char* smallest, const char* largest) {
    InternalKeyComparator cmp(BytewiseComparator());
    Slice s(smallest != nullptr ? smallest : "");
    Slice l(largest != nullptr ? largest : "");
    return SomeFileOverlapsRange(cmp, disjoint_sorted_files_, files_,
                                 (smallest != nullptr ? &s : nullptr),
                                 (largest != nullptr ? &l : nullptr));
  }

  bool disjoint_sorted_files_;

 private:
  std::vector<FileMetaData*> files_;
};

TEST(FindFileTest, Empty) {
  ASSERT_EQ(0, Find("foo"));
  ASSERT_TRUE(!Overlaps("a", "z"));
  ASSERT_TRUE(!Overlaps(nullptr, "z"));
  ASSERT_TRUE(!Overlaps("a", nullptr));
  ASSERT_TRUE(!Overlaps(nullptr, nullptr));
}

TEST(FindFileTest, Single) {
  Add("p", "q");
  ASSERT_EQ(0, Find("a"));
  ASSERT_EQ(0, Find("p"));
  ASSERT_EQ(0, Find("p1"));
  ASSERT_EQ(0, Find("q"));
  ASSERT_EQ(1, Find("q1"));
  ASSERT_EQ(1, Find("z"));

  ASSERT_TRUE(!Overlaps("a", "b"));
  ASSERT_TRUE(!Overlaps("z1", "z2"));
  ASSERT_TRUE(Overlaps("a", "p"));
  ASSERT_TRUE(Overlaps("a", "q"));
  ASSERT_TRUE(Overlaps("a", "z"));
  ASSERT_TRUE(Overlaps("p", "p1"));
  ASSERT_TRUE(Overlaps("p", "q"));
  ASSERT_TRUE(Overlaps("p", "z"));
  ASSERT_TRUE(Overlaps("p1", "p2"));
  ASSERT_TRUE(Overlaps("p1", "z"));
  ASSERT_TRUE(Overlaps("q", "q"));
  ASSERT_TRUE(Overlaps("q", "q1"));

  ASSERT_TRUE(!Overlaps(nullptr, "j"));
  ASSERT_TRUE(!Overlaps("r", nullptr));
  ASSERT_TRUE(Overlaps(nullptr, "p"));
  ASSERT_TRUE(Overlaps(nullptr, "p1"));
  ASSERT_TRUE(Overlaps("q", nullptr));
  ASSERT_TRUE(Overlaps(nullptr, nullptr));
}

TEST(FindFileTest, Multiple) {
  Add("150", "200");
  Add("200", "250");
  Add("300", "350");
  Add("400", "450");
  ASSERT_EQ(0, Find("100"));
  ASSERT_EQ(0, Find("150"));
  ASSERT_EQ(0, Find("151"));
  ASSERT_EQ(0, Find("199"));
  ASSERT_EQ(0, Find("200"));
  ASSERT_EQ(1, Find("201"));
  ASSERT_EQ(1, Find("249"));
  ASSERT_EQ(1, Find("250"));
  ASSERT_EQ(2, Find("251"));
  ASSERT_EQ(2, Find("299"));
  ASSERT_EQ(2, Find("300"));
  ASSERT_EQ(2, Find("349"));
  ASSERT_EQ(2, Find("350"));
  ASSERT_EQ(3, Find("351"));
  ASSERT_EQ(3, Find("400"));
  ASSERT_EQ(3, Find("450"));
  ASSERT_EQ(4, Find("451"));

  ASSERT_TRUE(!Overlaps("100", "149"));
  ASSERT_TRUE(!Overlaps("251", "299"));
  ASSERT_TRUE(!Overlaps("451", "500"));
  ASSERT_TRUE(!Overlaps("351", "399"));

  ASSERT_TRUE(Overlaps("100", "150"));
  ASSERT_TRUE(Overlaps("100", "200"));
  ASSERT_TRUE(Overlaps("100", "300"));
  ASSERT_TRUE(Overlaps("100", "400"));
  ASSERT_TRUE(Overlaps("100", "500"));
  ASSERT_TRUE(Overlaps("375", "400"));
  ASSERT_TRUE(Overlaps("450", "450"));
  ASSERT_TRUE(Overlaps("450", "500"));
}

TEST(FindFileTest, MultipleNullBoundaries) {
  Add("150", "200");
  Add("200", "250");
  Add("300", "350");
  Add("400", "450");
  ASSERT_TRUE(!Overlaps(nullptr, "149"));
  ASSERT_TRUE(!Overlaps("451", nullptr));
  ASSERT_TRUE(Overlaps(nullptr, nullptr));
  ASSERT_TRUE(Overlaps(nullptr, "150"));
  ASSERT_TRUE(Overlaps(nullptr, "199"));
  ASSERT_TRUE(Overlaps(nullptr, "200"));
  ASSERT_TRUE(Overlaps(nullptr, "201"));
  ASSERT_TRUE(Overlaps(nullptr, "400"));
  ASSERT_TRUE(Overlaps(nullptr, "800"));
  ASSERT_TRUE(Overlaps("100", nullptr));
  ASSERT_TRUE(Overlaps("200", nullptr));
  ASSERT_TRUE(Overlaps("449", nullptr));
  ASSERT_TRUE(Overlaps("450", nullptr));
}

TEST(FindFileTest, OverlapSequenceChecks) {
  Add("200", "200", 5000, 3000);
  ASSERT_TRUE(!Overlaps("199", "199"));
  ASSERT_TRUE(!Overlaps("201", "300"));
  ASSERT_TRUE(Overlaps("200", "200"));
  ASSERT_TRUE(Overlaps("190", "200"));
  ASSERT_TRUE(Overlaps("200", "210"));
}

TEST(FindFileTest, OverlappingFiles) {
  Add("150", "600");
  Add("400", "500");
  disjoint_sorted_files_ = false;
  ASSERT_TRUE(!Overlaps("100", "149"));
  ASSERT_TRUE(!Overlaps("601", "700"));
  ASSERT_TRUE(Overlaps("100", "150"));
  ASSERT_TRUE(Overlaps("100", "200"));
  ASSERT_TRUE(Overlaps("100", "300"));
  ASSERT_TRUE(Overlaps("100", "400"));
  ASSERT_TRUE(Overlaps("100", "500"));
  ASSERT_TRUE(Overlaps("375", "400"));
  ASSERT_TRUE(Overlaps("450", "450"));
  ASSERT_TRUE(Overlaps("450", "500"));
  ASSERT_TRUE(Overlaps("450", "700"));
  ASSERT_TRUE(Overlaps("600", "700"));
}

void AddBoundaryInputs(const InternalKeyComparator& icmp,
                       const std::vector<FileMetaData*>& level_files,
                       std::vector<FileMetaData*>* compaction_files);

class AddBoundaryInputsTest {
 public:
  std::vector<FileMetaData*> level_files_;
  std::vector<FileMetaData*> compaction_files_;
  std::vector<FileMetaData*> all_files_;
  InternalKeyComparator icmp_;

  AddBoundaryInputsTest() : icmp_(BytewiseComparator()){};

  ~AddBoundaryInputsTest() {
    for (size_t i = 0; i < all_files_.size(); ++i) {
      delete all_files_[i];
    }
    all_files_.clear();
  };

  FileMetaData* CreateFileMetaData(uint64_t number, InternalKey smallest,
                                   InternalKey largest) {
    FileMetaData* f = new FileMetaData();
    f->number = number;
    f->smallest = smallest;
    f->largest = largest;
    all_files_.push_back(f);
    return f;
  }
};

TEST(AddBoundaryInputsTest, TestEmptyFileSets) {
  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_TRUE(compaction_files_.empty());
  ASSERT_TRUE(level_files_.empty());
}

TEST(AddBoundaryInputsTest, TestEmptyLevelFiles) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 2, kTypeValue),
                         InternalKey(InternalKey("100", 1, kTypeValue)));
  compaction_files_.push_back(f1);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_EQ(1, compaction_files_.size());
  ASSERT_EQ(f1, compaction_files_[0]);
  ASSERT_TRUE(level_files_.empty());
}

TEST(AddBoundaryInputsTest, TestEmptyCompactionFiles) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 2, kTypeValue),
                         InternalKey(InternalKey("100", 1, kTypeValue)));
  level_files_.push_back(f1);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_TRUE(compaction_files_.empty());
  ASSERT_EQ(1, level_files_.size());
  ASSERT_EQ(f1, level_files_[0]);
}

TEST(AddBoundaryInputsTest, TestNoBoundaryFiles) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 2, kTypeValue),
                         InternalKey(InternalKey("100", 1, kTypeValue)));
  FileMetaData* f2 =
      CreateFileMetaData(1, InternalKey("200", 2, kTypeValue),
                         InternalKey(InternalKey("200", 1, kTypeValue)));
  FileMetaData* f3 =
      CreateFileMetaData(1, InternalKey("300", 2, kTypeValue),
                         InternalKey(InternalKey("300", 1, kTypeValue)));

  level_files_.push_back(f3);
  level_files_.push_back(f2);
  level_files_.push_back(f1);
  compaction_files_.push_back(f2);
  compaction_files_.push_back(f3);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_EQ(2, compaction_files_.size());
}

TEST(AddBoundaryInputsTest, TestOneBoundaryFiles) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 3, kTypeValue),
                         InternalKey(InternalKey("100", 2, kTypeValue)));
  FileMetaData* f2 =
      CreateFileMetaData(1, InternalKey("100", 1, kTypeValue),
                         InternalKey(InternalKey("200", 3, kTypeValue)));
  FileMetaData* f3 =
      CreateFileMetaData(1, InternalKey("300", 2, kTypeValue),
                         InternalKey(InternalKey("300", 1, kTypeValue)));

  level_files_.push_back(f3);
  level_files_.push_back(f2);
  level_files_.push_back(f1);
  compaction_files_.push_back(f1);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_EQ(2, compaction_files_.size());
  ASSERT_EQ(f1, compaction_files_[0]);
  ASSERT_EQ(f2, compaction_files_[1]);
}

TEST(AddBoundaryInputsTest, TestTwoBoundaryFiles) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 6, kTypeValue),
                         InternalKey(InternalKey("100", 5, kTypeValue)));
  FileMetaData* f2 =
      CreateFileMetaData(1, InternalKey("100", 2, kTypeValue),
                         InternalKey(InternalKey("300", 1, kTypeValue)));
  FileMetaData* f3 =
      CreateFileMetaData(1, InternalKey("100", 4, kTypeValue),
                         InternalKey(InternalKey("100", 3, kTypeValue)));

  level_files_.push_back(f2);
  level_files_.push_back(f3);
  level_files_.push_back(f1);
  compaction_files_.push_back(f1);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_EQ(3, compaction_files_.size());
  ASSERT_EQ(f1, compaction_files_[0]);
  ASSERT_EQ(f3, compaction_files_[1]);
  ASSERT_EQ(f2, compaction_files_[2]);
}

TEST(AddBoundaryInputsTest, TestDisjoinFilePointers) {
  FileMetaData* f1 =
      CreateFileMetaData(1, InternalKey("100", 6, kTypeValue),
                         InternalKey(InternalKey("100", 5, kTypeValue)));
  FileMetaData* f2 =
      CreateFileMetaData(1, InternalKey("100", 6, kTypeValue),
                         InternalKey(InternalKey("100", 5, kTypeValue)));
  FileMetaData* f3 =
      CreateFileMetaData(1, InternalKey("100", 2, kTypeValue),
                         InternalKey(InternalKey("300", 1, kTypeValue)));
  FileMetaData* f4 =
      CreateFileMetaData(1, InternalKey("100", 4, kTypeValue),
                         InternalKey(InternalKey("100", 3, kTypeValue)));

  level_files_.push_back(f2);
  level_files_.push_back(f3);
  level_files_.push_back(f4);

  compaction_files_.push_back(f1);

  AddBoundaryInputs(icmp_, level_files_, &compaction_files_);
  ASSERT_EQ(3, compaction_files_.size());
  ASSERT_EQ(f1, compaction_files_[0]);
  ASSERT_EQ(f4, compaction_files_[1]);
  ASSERT_EQ(f3, compaction_files_[2]);
}

// Checks which files VersionSet picks for a compaction: CompactRange() goes
// through SetupOtherInputs().  Picking only looks at file metadata, so the
// files are described with AddFile() and never written.
class CompactionInputsTest {
 public:
  CompactionInputsTest()
      : env_(NewMemEnv(Env::Default())),
        dbname_("/compaction_inputs_test"),
        icmp_(BytewiseComparator()),
        versions_(nullptr) {
    // Recover() needs an existing database, so create an empty one first.
    Options open_options;
    open_options.env = env_;
    open_options.create_if_missing = true;
    DB* db = nullptr;
    ASSERT_OK(DB::Open(open_options, dbname_, &db));
    delete db;

    options_.env = env_;
    versions_ = new VersionSet(dbname_, &options_, nullptr, &icmp_);
    bool save_manifest;
    ASSERT_OK(versions_->Recover(&save_manifest));
  }

  ~CompactionInputsTest() {
    delete versions_;
    delete env_;
  }

  // Describes a file in |level| whose keys range from |smallest| to |largest|.
  // It becomes part of the current version with the next Install().
  void AddFile(int level, uint64_t number, const InternalKey& smallest,
               const InternalKey& largest) {
    versions_->MarkFileNumberUsed(number);
    edit_.AddFile(level, number, 1000 /* file size */, smallest, largest);
  }

  // Makes the files described so far the current version.
  void Install() {
    MutexLock l(&mu_);
    ASSERT_OK(versions_->LogAndApply(&edit_, &mu_));
  }

  // Returns the compaction that a manual compaction of the user keys
  // [begin, end] in |level| starts with.  The caller deletes it.
  Compaction* PickRange(int level, const char* begin, const char* end) {
    InternalKey begin_key(begin, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey end_key(end, 0, static_cast<ValueType>(0));
    return versions_->CompactRange(level, &begin_key, &end_key);
  }

  // Returns the numbers of the files that |c| takes from its first level
  // (which == 0) or from the level below it (which == 1), in ascending order,
  // for example "21,22".
  std::string Inputs(Compaction* c, int which) {
    std::vector<uint64_t> numbers;
    for (int i = 0; i < c->num_input_files(which); i++) {
      numbers.push_back(c->input(which, i)->number);
    }
    std::sort(numbers.begin(), numbers.end());
    std::string result;
    for (size_t i = 0; i < numbers.size(); i++) {
      if (i > 0) {
        result += ",";
      }
      result += NumberToString(numbers[i]);
    }
    return result;
  }

 private:
  Env* env_;
  std::string dbname_;
  InternalKeyComparator icmp_;
  Options options_;
  port::Mutex mu_;
  VersionEdit edit_;
  VersionSet* versions_;
};

TEST(CompactionInputsTest, IncludesBoundaryFileOfNextLevel) {
  // Level 1 holds one file.
  AddFile(1, 11, InternalKey("100", 20, kTypeValue),
          InternalKey("150", 21, kTypeValue));
  // In level 2 the two versions of user key "200" are in different files: file
  // 21 ends with the newer one, a deletion marker, and file 22 starts with the
  // older one, a value.
  AddFile(2, 21, InternalKey("100", 3, kTypeValue),
          InternalKey("200", 9, kTypeDeletion));
  AddFile(2, 22, InternalKey("200", 5, kTypeValue),
          InternalKey("300", 4, kTypeValue));
  Install();

  Compaction* c = PickRange(1, "100", "150");
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ("11", Inputs(c, 0));
  // Only file 21 overlaps the range.  But a compaction of file 21 without file
  // 22 could drop the deletion marker of "200", and the value in file 22 would
  // be readable again.
  ASSERT_EQ("21,22", Inputs(c, 1));
  delete c;
}

TEST(CompactionInputsTest, ExpansionKeepsBoundaryFileOfNextLevel) {
  AddFile(1, 11, InternalKey("050", 30, kTypeValue),
          InternalKey("100", 31, kTypeValue));
  AddFile(1, 12, InternalKey("120", 32, kTypeValue),
          InternalKey("150", 33, kTypeValue));
  AddFile(2, 21, InternalKey("010", 1, kTypeValue),
          InternalKey("060", 2, kTypeValue));
  // The two versions of user key "200" are split across files 22 and 23.
  AddFile(2, 22, InternalKey("080", 3, kTypeValue),
          InternalKey("200", 9, kTypeDeletion));
  AddFile(2, 23, InternalKey("200", 5, kTypeValue),
          InternalKey("300", 4, kTypeValue));
  Install();

  // File 12 overlaps file 22, and file 23 comes with it as its boundary file.
  // The keys of files 22 and 23 also overlap file 11, so taking file 11 into
  // the compaction is considered.  That is allowed only if it leaves the
  // level-2 inputs as they are, which is checked by counting them.  With file
  // 11 they would be 21, 22 and, as boundary file, 23: three files instead of
  // two, so file 11 has to stay out.  If file 23 is not counted, {21, 22}
  // passes for {22, 23} and file 23 is left behind.
  Compaction* c = PickRange(1, "120", "150");
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ("22,23", Inputs(c, 1));
  ASSERT_EQ("12", Inputs(c, 0));
  delete c;
}

}  // namespace leveldb

int main(int argc, char** argv) { return leveldb::test::RunAllTests(); }
