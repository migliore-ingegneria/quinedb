#include <gtest/gtest.h>

#include <chrono>

#include "storage/compaction.hpp"
#include "storage/sstable.hpp"

using namespace quine::storage;

TEST(CompactionTest, AddRemoveSSTable) {
  SizeTieredCompactionManager manager;

  SSTableMetaData t1;
  t1.id = "sstable_1";
  t1.records = {{"key1", "val1", false, 100}, {"key2", "val2", false, 101}};
  manager.add_sstable(t1);

  EXPECT_EQ(manager.get_sstables().size(), 1);

  auto fetched = manager.get_sstable("sstable_1");
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->record_count, 2);
  EXPECT_EQ(fetched->min_key, "key1");
  EXPECT_EQ(fetched->max_key, "key2");

  EXPECT_TRUE(manager.remove_sstable("sstable_1"));
  EXPECT_EQ(manager.get_sstables().size(), 0);
}

TEST(CompactionTest, UserWriteWafTracking) {
  CompactionOptions opts;
  opts.max_waf_limit = 5.0;
  SizeTieredCompactionManager manager(opts);

  EXPECT_DOUBLE_EQ(manager.calculate_waf(), 1.0);

  manager.record_user_write(1000);
  EXPECT_DOUBLE_EQ(manager.calculate_waf(), 1.0);
  EXPECT_EQ(manager.get_metrics().user_bytes_written, 1000);
}

TEST(CompactionTest, PlanCompactionUnderMinThreshold) {
  CompactionOptions opts;
  opts.min_threshold = 4;
  SizeTieredCompactionManager manager(opts);

  for (int i = 0; i < 3; ++i) {
    SSTableMetaData t;
    t.id = "sstable_" + std::to_string(i);
    t.records = {{"k", "v", false, 100}};
    manager.add_sstable(t);
  }

  auto plan = manager.plan_compaction();
  EXPECT_FALSE(plan.has_value());
}

TEST(CompactionTest, PlanCompactionSizeTiering) {
  CompactionOptions opts;
  opts.min_threshold = 4;
  opts.size_ratio = 0.5;
  SizeTieredCompactionManager manager(opts);

  // Add 4 SSTables of ~100 bytes each
  for (int i = 0; i < 4; ++i) {
    SSTableMetaData t;
    t.id = "small_" + std::to_string(i);
    t.level = 0;
    t.records = {{"key" + std::to_string(i), "value_padding_string_12345", false, 100}};
    manager.add_sstable(t);
  }

  // Add 2 large SSTables of much larger size
  for (int i = 0; i < 2; ++i) {
    SSTableMetaData t;
    t.id = "large_" + std::to_string(i);
    t.level = 1;
    std::string large_val(500, 'x');
    t.records = {{"large_key" + std::to_string(i), large_val, false, 100}};
    manager.add_sstable(t);
  }

  auto plan = manager.plan_compaction();
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->candidate_ids.size(), 4);
  EXPECT_EQ(plan->target_level, 1);
}

TEST(CompactionTest, ExecuteCompactionMergesRecordsAndTombstones) {
  CompactionOptions opts;
  opts.min_threshold = 3;
  SizeTieredCompactionManager manager(opts);
  manager.record_user_write(5000);

  // Table 1: key1="v1_old" (t=100), key2="v2" (t=100)
  SSTableMetaData t1;
  t1.id = "sst_1";
  t1.created_at = 100;
  t1.records = {{"key1", "v1_old", false, 100}, {"key2", "v2", false, 100}};
  manager.add_sstable(t1);

  // Table 2: key1="v1_new" (t=200), key3="v3" (t=200)
  SSTableMetaData t2;
  t2.id = "sst_2";
  t2.created_at = 200;
  t2.records = {{"key1", "v1_new", false, 200}, {"key3", "v3", false, 200}};
  manager.add_sstable(t2);

  // Table 3: key2 deleted (tombstone t=300), key4="v4" (t=300)
  SSTableMetaData t3;
  t3.id = "sst_3";
  t3.created_at = 300;
  t3.records = {{"key2", "", true, 300}, {"key4", "v4", false, 300}};
  manager.add_sstable(t3);

  auto plan = manager.plan_compaction();
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->candidate_ids.size(), 3);

  auto merged_opt = manager.execute_compaction(plan.value());
  ASSERT_TRUE(merged_opt.has_value());

  const auto& merged = merged_opt.value();
  // key2 tombstone should be purged, key1 should have v1_new
  EXPECT_EQ(merged.records.size(), 3);  // key1, key3, key4

  bool found_key1_new = false;
  bool found_key2 = false;
  for (const auto& rec : merged.records) {
    if (rec.key == "key1" && rec.value == "v1_new") found_key1_new = true;
    if (rec.key == "key2") found_key2 = true;
  }
  EXPECT_TRUE(found_key1_new);
  EXPECT_FALSE(found_key2);

  // Manager should now hold 1 merged SSTable
  EXPECT_EQ(manager.get_sstables().size(), 1);
  EXPECT_EQ(manager.get_metrics().total_compactions_executed, 1);
  EXPECT_EQ(manager.get_metrics().tombstones_purged, 1);
}

TEST(CompactionTest, WafLimitEnforcement) {
  CompactionOptions opts;
  opts.min_threshold = 2;
  opts.max_waf_limit = 1.1;  // Very strict WAF limit
  SizeTieredCompactionManager manager(opts);

  // User wrote only 10 bytes
  manager.record_user_write(10);

  // Add 2 large tables (each ~500 bytes)
  for (int i = 0; i < 2; ++i) {
    SSTableMetaData t;
    t.id = "table_" + std::to_string(i);
    std::string big_val(500, 'y');
    t.records = {{"k" + std::to_string(i), big_val, false, 100}};
    manager.add_sstable(t);
  }

  // Compacting ~1000 bytes with only 10 bytes user write would result in WAF > 100 > max_waf_limit (1.1)
  auto plan = manager.plan_compaction();
  EXPECT_FALSE(plan.has_value());  // Should be blocked by WAF constraint
}
