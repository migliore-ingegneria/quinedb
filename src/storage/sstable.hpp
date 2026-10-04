#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bloom_filter.hpp"

namespace quine {
namespace storage {

struct SSTableRecord {
  std::string key;
  std::string value;
  bool is_tombstone = false;
  int64_t timestamp = 0;  // Milliseconds unix timestamp
};

struct SSTableMetaData {
  std::string id;
  size_t file_size = 0;     // Size in bytes
  size_t record_count = 0;  // Number of records
  uint32_t level = 0;       // Tier/Level index
  std::string min_key;
  std::string max_key;
  int64_t created_at = 0;
  std::vector<SSTableRecord> records;
  BloomFilter bloom_filter;

  /// @brief Fast check if key may exist in this SSTable.
  bool may_contain(std::string_view key) const {
    if (records.empty()) return false;
    if (key < min_key || key > max_key) return false;
    return bloom_filter.contains(key);
  }

  void calculate_bounds() {
    if (records.empty()) {
      min_key.clear();
      max_key.clear();
      file_size = 0;
      record_count = 0;
      bloom_filter.clear();
      return;
    }
    record_count = records.size();
    min_key = records.front().key;
    max_key = records.front().key;
    bloom_filter = BloomFilter(record_count, 0.01);
    size_t total_bytes = 0;
    for (const auto& r : records) {
      if (r.key < min_key) min_key = r.key;
      if (r.key > max_key) max_key = r.key;
      total_bytes += r.key.size() + r.value.size() + sizeof(SSTableRecord);
      bloom_filter.add(r.key);
    }
    file_size = total_bytes;
  }
};

}  // namespace storage
}  // namespace quine
