#include "compaction.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

namespace quine {
namespace storage {

SizeTieredCompactionManager::SizeTieredCompactionManager(CompactionOptions options)
    : options_(std::move(options)) {}

void SizeTieredCompactionManager::add_sstable(SSTableMetaData table) {
  table.calculate_bounds();
  tables_.push_back(std::move(table));
}

bool SizeTieredCompactionManager::remove_sstable(const std::string& id) {
  auto it = std::remove_if(tables_.begin(), tables_.end(),
                           [&id](const SSTableMetaData& t) { return t.id == id; });
  if (it != tables_.end()) {
    tables_.erase(it, tables_.end());
    return true;
  }
  return false;
}

const std::vector<SSTableMetaData>& SizeTieredCompactionManager::get_sstables() const {
  return tables_;
}

std::optional<SSTableMetaData> SizeTieredCompactionManager::get_sstable(
    const std::string& id) const {
  for (const auto& t : tables_) {
    if (t.id == id) return t;
  }
  return std::nullopt;
}

void SizeTieredCompactionManager::record_user_write(size_t bytes) {
  metrics_.user_bytes_written += bytes;
  metrics_.current_waf = calculate_waf();
}

double SizeTieredCompactionManager::calculate_waf() const {
  if (metrics_.user_bytes_written == 0) return 1.0;
  return static_cast<double>(metrics_.user_bytes_written + metrics_.compacted_bytes_written) /
         static_cast<double>(metrics_.user_bytes_written);
}

std::vector<std::vector<SSTableMetaData>> SizeTieredCompactionManager::group_into_tiers() const {
  std::vector<std::vector<SSTableMetaData>> result;
  if (tables_.empty()) return result;

  // Filter and sort tables by file_size ascending
  std::vector<SSTableMetaData> sorted_tables = tables_;
  std::sort(sorted_tables.begin(), sorted_tables.end(),
            [](const SSTableMetaData& a, const SSTableMetaData& b) {
              return a.file_size < b.file_size;
            });

  for (const auto& table : sorted_tables) {
    if (table.file_size < options_.min_sstable_size) continue;

    bool added_to_tier = false;
    for (auto& tier : result) {
      if (tier.empty()) continue;

      // Check average size of tier
      size_t total_size = 0;
      for (const auto& t : tier) {
        total_size += t.file_size;
      }
      double avg_size = static_cast<double>(total_size) / tier.size();

      double lower_bound = avg_size * (1.0 - options_.size_ratio);
      double upper_bound = avg_size * (1.0 + options_.size_ratio);

      if (static_cast<double>(table.file_size) >= lower_bound &&
          static_cast<double>(table.file_size) <= upper_bound) {
        tier.push_back(table);
        added_to_tier = true;
        break;
      }
    }

    if (!added_to_tier) {
      result.push_back({table});
    }
  }

  return result;
}

std::optional<CompactionPlan> SizeTieredCompactionManager::plan_compaction() const {
  auto tiers = group_into_tiers();

  for (const auto& tier : tiers) {
    if (tier.size() >= options_.min_threshold) {
      CompactionPlan plan;
      size_t count = std::min(tier.size(), options_.max_threshold);

      uint32_t max_level = 0;
      size_t input_bytes = 0;

      for (size_t i = 0; i < count; ++i) {
        plan.candidate_ids.push_back(tier[i].id);
        input_bytes += tier[i].file_size;
        if (tier[i].level > max_level) {
          max_level = tier[i].level;
        }
      }

      plan.target_level = max_level + 1;
      plan.estimated_input_bytes = input_bytes;

      // Evaluate WAF constraint
      size_t user_bytes = metrics_.user_bytes_written == 0 ? input_bytes : metrics_.user_bytes_written;
      double projected_compacted = metrics_.compacted_bytes_written + input_bytes;
      plan.projected_waf = static_cast<double>(user_bytes + projected_compacted) / static_cast<double>(user_bytes);

      if (plan.projected_waf <= options_.max_waf_limit) {
        return plan;
      }
    }
  }

  return std::nullopt;
}

std::optional<SSTableMetaData> SizeTieredCompactionManager::execute_compaction(
    const CompactionPlan& plan) {
  if (plan.candidate_ids.empty()) return std::nullopt;

  // Gather candidate SSTables
  std::vector<SSTableMetaData> candidates;
  for (const auto& id : plan.candidate_ids) {
    auto opt = get_sstable(id);
    if (opt.has_value()) {
      candidates.push_back(opt.value());
    }
  }

  if (candidates.size() < 2) return std::nullopt;

  // Sort candidates by creation timestamp ascending (older first, newer later)
  std::sort(candidates.begin(), candidates.end(),
            [](const SSTableMetaData& a, const SSTableMetaData& b) {
              return a.created_at < b.created_at;
            });

  // Merge records. Newer record timestamps overwrite older ones.
  std::map<std::string, SSTableRecord> merged_records;
  size_t total_records_examined = 0;

  for (const auto& sstable : candidates) {
    for (const auto& rec : sstable.records) {
      total_records_examined++;
      auto it = merged_records.find(rec.key);
      if (it == merged_records.end()) {
        merged_records[rec.key] = rec;
      } else {
        // Overwrite if timestamp is newer or equal
        if (rec.timestamp >= it->second.timestamp) {
          it->second = rec;
        }
      }
    }
  }

  // Filter out tombstones and construct final records
  std::vector<SSTableRecord> final_records;
  size_t tombstones_purged = 0;

  for (auto& [key, rec] : merged_records) {
    if (rec.is_tombstone) {
      tombstones_purged++;
    } else {
      final_records.push_back(std::move(rec));
    }
  }

  // Create merged SSTable metadata
  SSTableMetaData merged_sstable;
  merged_sstable.id = "compacted_l" + std::to_string(plan.target_level) + "_" +
                      std::to_string(metrics_.total_compactions_executed + 1);
  merged_sstable.level = plan.target_level;
  merged_sstable.records = std::move(final_records);
  merged_sstable.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  merged_sstable.calculate_bounds();

  // Remove compacted input SSTables from manager
  for (const auto& id : plan.candidate_ids) {
    remove_sstable(id);
  }

  // Add newly merged SSTable to manager
  add_sstable(merged_sstable);

  // Update compaction metrics
  metrics_.compacted_bytes_written += merged_sstable.file_size;
  metrics_.total_compactions_executed++;
  metrics_.total_records_merged += total_records_examined;
  metrics_.tombstones_purged += tombstones_purged;
  metrics_.current_waf = calculate_waf();

  return merged_sstable;
}

}  // namespace storage
}  // namespace quine
