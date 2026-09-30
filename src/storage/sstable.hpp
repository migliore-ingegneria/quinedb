#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

  void calculate_bounds() {
    if (records.empty()) {
      min_key.clear();
      max_key.clear();
      file_size = 0;
      record_count = 0;
      return;
    }
    record_count = records.size();
    min_key = records.front().key;
    max_key = records.front().key;
    size_t total_bytes = 0;
    for (const auto& r : records) {
      if (r.key < min_key) min_key = r.key;
      if (r.key > max_key) max_key = r.key;
      total_bytes += r.key.size() + r.value.size() + sizeof(SSTableRecord);
    }
    file_size = total_bytes;
  }
};

}  // namespace storage
}  // namespace quine
