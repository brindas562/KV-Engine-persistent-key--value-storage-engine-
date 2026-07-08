#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

// Bloom filter using double hashing (Kirsch-Mitzenmacher technique).
// Two base hashes h1, h2 generate k independent hash functions:
//   g_i(x) = h1(x) + i * h2(x)   for i in [0, k)
// This avoids computing k separate hash functions while maintaining
// the same false-positive probability as k truly independent hashes.
class BloomFilter {
public:
    // expectedKeys : how many keys will be inserted
    // falsePositiveRate : e.g. 0.01 for 1%
    BloomFilter(size_t expectedKeys, double falsePositiveRate = 0.01) {
        // Optimal bit count: m = -n * ln(p) / (ln2)^2
        size_t m = static_cast<size_t>(
            -static_cast<double>(expectedKeys) * std::log(falsePositiveRate)
            / (std::log(2.0) * std::log(2.0))
        );
        if (m == 0) m = 1;
        bits_.assign(m, false);

        // Optimal hash count: k = (m/n) * ln2
        numHashes_ = static_cast<size_t>(
            static_cast<double>(m) / static_cast<double>(expectedKeys) * std::log(2.0)
        );
        if (numHashes_ == 0) numHashes_ = 1;
    }

    void add(const std::string& key) {
        uint64_t h1 = hash1(key);
        uint64_t h2 = hash2(key);
        size_t m = bits_.size();
        for (size_t i = 0; i < numHashes_; ++i) {
            bits_[(h1 + i * h2) % m] = true;
        }
    }

    // Returns false  → key is DEFINITELY not present  (skip the SSTable)
    // Returns true   → key is PROBABLY present         (proceed to sparse index)
    bool mightContain(const std::string& key) const {
        uint64_t h1 = hash1(key);
        uint64_t h2 = hash2(key);
        size_t m = bits_.size();
        for (size_t i = 0; i < numHashes_; ++i) {
            if (!bits_[(h1 + i * h2) % m]) return false;
        }
        return true;
    }

    size_t bitCount()  const { return bits_.size(); }
    size_t hashCount() const { return numHashes_; }

private:
    std::vector<bool> bits_;
    size_t            numHashes_;

    // FNV-1a 64-bit — first base hash
    uint64_t hash1(const std::string& key) const {
        uint64_t h = 14695981039346656037ULL;
        for (unsigned char c : key) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    }

    // djb2 64-bit — second base hash (must be different family from h1)
    uint64_t hash2(const std::string& key) const {
        uint64_t h = 5381;
        for (unsigned char c : key) {
            h = ((h << 5) + h) ^ c;
        }
        // Make it odd so it's always coprime with a power-of-two table size,
        // ensuring all buckets are reachable. Doesn't matter for arbitrary m
        // but is a good habit.
        return h | 1;
    }
};
