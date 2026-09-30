#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sstable.hpp"

namespace quine {
namespace storage {

struct CompactionOptions {
  double size_ratio = 0.5;         // Size similarity tolerance ratio for tier bucketing
  size_t min_threshold = 4;        // Minimum SSTables in tier to trigger compaction
  size_t max_threshold = 32;       // Maximum SSTables merged in single compaction pass
  double max_waf_limit = 5.0;      // Maximum write amplification factor allowed
  size_t min_sstable_size = 10;    // Minimum size in bytes for compaction grouping
  size_t max_sstable_size = 50 * 1024 * 1024;  // Max SSTable size (50MB)
};

struct CompactionPlan {
  std::vector<std::string> candidate_ids;
  uint32_t target_level = 0;
  size_t estimated_input_bytes = 0;
  double projected_waf = 1.0;
};

struct CompactionMetrics {
  size_t user_bytes_written = 0;
  size_t compacted_bytes_written = 0;
  size_t total_compactions_executed = 0;
  size_t total_records_merged = 0;
  size_t tombstones_purged = 0;
  double current_waf = 1.0;
};

class SizeTieredCompactionManager {
 public:
  explicit SizeTieredCompactionManager(CompactionOptions options = {});

  // SSTable management
  void add_sstable(SSTableMetaData table);
  bool remove_sstable(const std::string& id);
  const std::vector<SSTableMetaData>& get_sstables() const;
  std::optional<SSTableMetaData> get_sstable(const std::string& id) const;

  // Track write activity for WAF calculation
  void record_user_write(size_t bytes);

  // WAF calculation: (user_bytes + compacted_bytes) / user_bytes
  double calculate_waf() const;

  // Compaction planning
  std::optional<CompactionPlan> plan_compaction() const;

  // Execute compaction for a planned compaction candidate set
  std::optional<SSTableMetaData> execute_compaction(const CompactionPlan& plan);

  // Metrics
  const CompactionMetrics& get_metrics() const { return metrics_; }
  const CompactionOptions& get_options() const { return options_; }
  void set_options(CompactionOptions options) { options_ = std::move(options); }

 private:
  CompactionOptions options_;
  std::vector<SSTableMetaData> tables_;
  CompactionMetrics metrics_;

  // Helper to group tables by size tier
  std::vector<std::vector<SSTableMetaData>> group_into_tiers() const;
};

}  // namespace storage
}  // namespace quine
