#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quine {
namespace storage {

/// @brief Bit-array Bloom Filter for fast probabilistic set membership checks.
class BloomFilter {
 public:
  /// @brief Construct a BloomFilter given expected number of entries and target false positive rate.
  /// @param expected_entries Estimated number of keys (n)
  /// @param fp_rate Desired false positive probability p (e.g., 0.01 for 1%)
  explicit BloomFilter(size_t expected_entries = 1000, double fp_rate = 0.01)
      : expected_entries_(expected_entries), fp_rate_(fp_rate) {
    if (expected_entries == 0) expected_entries_ = 1;
    if (fp_rate <= 0.0 || fp_rate >= 1.0) fp_rate_ = 0.01;

    // m = - (n * ln(p)) / (ln(2)^2)
    double m = -static_cast<double>(expected_entries_) * std::log(fp_rate_) / (std::log(2.0) * std::log(2.0));
    num_bits_ = std::max<size_t>(64, static_cast<size_t>(std::ceil(m)));

    // k = (m / n) * ln(2)
    double k = (static_cast<double>(num_bits_) / static_cast<double>(expected_entries_)) * std::log(2.0);
    num_hashes_ = std::max<size_t>(1, static_cast<size_t>(std::round(k)));

    bits_.resize(num_bits_, false);
  }

  /// @brief Insert a key into the Bloom filter.
  void add(std::string_view key) {
    auto [h1, h2] = hash_pair(key);
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t bit_index = (h1 + i * h2) % num_bits_;
      bits_[bit_index] = true;
    }
    ++entry_count_;
  }

  /// @brief Query if a key might be in the set.
  /// @return false if key is DEFINITELY NOT in the set; true if key MAY BE in the set.
  bool contains(std::string_view key) const {
    if (bits_.empty()) return true;
    auto [h1, h2] = hash_pair(key);
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t bit_index = (h1 + i * h2) % num_bits_;
      if (!bits_[bit_index]) {
        return false;  // Definitely not present
      }
    }
    return true;  // Possibly present
  }

  /// @brief Clear the filter.
  void clear() {
    std::fill(bits_.begin(), bits_.end(), false);
    entry_count_ = 0;
  }

  size_t num_bits() const { return num_bits_; }
  size_t num_hashes() const { return num_hashes_; }
  size_t entry_count() const { return entry_count_; }
  size_t expected_entries() const { return expected_entries_; }
  double fp_rate() const { return fp_rate_; }

 private:
  size_t expected_entries_;
  double fp_rate_;
  size_t num_bits_ = 64;
  size_t num_hashes_ = 1;
  size_t entry_count_ = 0;
  std::vector<bool> bits_;

  /// @brief FNV-1a based double hashing implementation.
  std::pair<uint64_t, uint64_t> hash_pair(std::string_view key) const {
    // Primary FNV-1a hash
    uint64_t h1 = 14695981039346656037ULL;
    for (char c : key) {
      h1 ^= static_cast<uint64_t>(c);
      h1 *= 1099511628211ULL;
    }

    // Secondary Murmur-like hash mix
    uint64_t h2 = h1 ^ 0xc6a4a7935bd1e995ULL;
    h2 *= 0x5bd1e995ULL;
    h2 ^= h2 >> 47;
    h2 *= 0x5bd1e995ULL;

    if (h2 == 0) h2 = 1;  // Ensure non-zero step
    return {h1, h2};
  }
};

}  // namespace storage
}  // namespace quine
