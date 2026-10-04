#include <gtest/gtest.h>

#include "storage/bloom_filter.hpp"
#include "storage/sstable.hpp"

using namespace quine::storage;

TEST(BloomFilterTest, NoFalseNegatives) {
  BloomFilter filter(500, 0.01);

  std::vector<std::string> keys;
  for (int i = 0; i < 500; ++i) {
    keys.push_back("key_" + std::to_string(i));
    filter.add(keys.back());
  }

  // Bloom filter MUST NEVER return false for an inserted key (Zero False Negatives)
  for (const auto& k : keys) {
    EXPECT_TRUE(filter.contains(k));
  }
}

TEST(BloomFilterTest, LowFalsePositiveRate) {
  size_t n = 1000;
  BloomFilter filter(n, 0.01);

  for (size_t i = 0; i < n; ++i) {
    filter.add("inserted_key_" + std::to_string(i));
  }

  // Test false positive rate on keys that were never inserted
  size_t false_positives = 0;
  size_t total_queries = 2000;
  for (size_t i = 0; i < total_queries; ++i) {
    if (filter.contains("non_existent_key_" + std::to_string(i))) {
      false_positives++;
    }
  }

  double fp_rate = static_cast<double>(false_positives) / static_cast<double>(total_queries);
  // FP rate should be low (< 3% given 1% target)
  EXPECT_LT(fp_rate, 0.03);
}

TEST(BloomFilterTest, ClearAndReuse) {
  BloomFilter filter(100, 0.01);
  filter.add("test_key");
  EXPECT_TRUE(filter.contains("test_key"));

  filter.clear();
  EXPECT_EQ(filter.entry_count(), 0);
  EXPECT_FALSE(filter.contains("test_key"));
}

TEST(BloomFilterTest, SSTableMayContainIntegration) {
  SSTableMetaData table;
  table.id = "sst_bloom_1";
  table.records = {
      {"user:100", "alice", false, 1000},
      {"user:200", "bob", false, 1001},
      {"user:300", "charlie", false, 1002},
  };
  table.calculate_bounds();

  // Present keys
  EXPECT_TRUE(table.may_contain("user:100"));
  EXPECT_TRUE(table.may_contain("user:200"));
  EXPECT_TRUE(table.may_contain("user:300"));

  // Keys outside bounds
  EXPECT_FALSE(table.may_contain("user:050"));  // < min_key
  EXPECT_FALSE(table.may_contain("user:999"));  // > max_key

  // Key inside bounds but absent
  EXPECT_FALSE(table.may_contain("user:250"));
}
