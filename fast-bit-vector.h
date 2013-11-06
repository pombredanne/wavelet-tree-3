#ifndef FASTBITVECTOR_H
#define FASTBITVECTOR_H // Small and simple bitvector.
// Designed to be fast and compact.
// Supports select and rank queries.

#include <cstddef>
#include <vector>
#include <cassert>
#include <stdint.h>

using std::size_t;

// Select for single word.
//  v: Input value to find position with rank r.
//  r: bit's desired rank [1-64].
// Returns: First index i so that r == rank(v,i)
int FBVInitTables();
extern uint8_t BytePop[256];
extern uint8_t ByteSelect[256][8];


// Select for single word.
//  v: Input value to find position with rank r.
//  r: bit's desired rank [1-64].
// Returns: First index i so that r == rank(v,i)
static inline int WordSelect(unsigned long v, int r) {
  static int init = FBVInitTables();
  (void) init;
  for (size_t b = 0; b < sizeof(long); ++b) {
    int c = BytePop[v&255];
    if (c >= r) {
      return b * 8 + ByteSelect[v&255][r-1];
    }
    r -= c;
    v >>= 8;
  }
  assert(false);
  return -1;
}

class FastBitVector {
  // These two CANNOT be modified.
  static const unsigned RankSample = 2048;
  static const unsigned RankSubSample = 384;
  // This CAN be tweaked.
  static const unsigned SelectSample = 8 * 1024;
  static const unsigned WordBits = 8 * sizeof(long);
 public:
  // Empty constructor.
  FastBitVector();
  explicit FastBitVector(const std::vector<bool>& data);
  FastBitVector(const FastBitVector& other) = delete;
  FastBitVector(FastBitVector&& other);
  const FastBitVector& operator=(FastBitVector&& other);

  bool operator[](size_t pos) const {
    size_t i = pos / WordBits;
    int offset = pos % WordBits;
    return (bits_[i] >> offset) & 1;
  }

  // Number of positions < pos set with bit_value.
  size_t rank(size_t pos, bool bit_value) const {
    size_t block = pos / RankSample;
    size_t remaining = pos % RankSample;
    size_t sum = rank_samples_[block].abs;
    int sub_block = remaining / RankSubSample;
    sum += subBlockRank(block, sub_block);
    remaining -= RankSubSample * sub_block;
    size_t word = (block * RankSample + sub_block * RankSubSample) / WordBits;
    for (;(word + 1) * WordBits <= pos; ++word) {
      sum += __builtin_popcountll(bits_[word]);
      remaining -= WordBits;
    }
    // Add first bits from the last word.
    unsigned long mask = (1LL << remaining) - 1LL;
    sum += __builtin_popcountll(bits_[word] & mask);
    if (bit_value == 0) return pos - sum;
    return sum;
  }

  // Returns smallest position pos so that rank(pos,bit) == idx
  size_t select(size_t idx, bool bit) const {
    assert(idx <= count(bit));
    // Start from sampling.
    size_t block = idx / SelectSample;
    size_t left = select_samples_[bit][block];
    size_t right = select_samples_[bit][block + 1];

    size_t word_start = left * RankSample / WordBits;
    size_t word_rank = bit ? rank_samples_[left].abs : left * RankSample - rank_samples_[left].abs;
    assert(word_rank <= idx);
    while (left + 1 < right) {
      size_t c = (left + right) / 2;
      size_t r = bit ? rank_samples_[c].abs : c * RankSample - rank_samples_[c].abs;
      if (r >= idx) {
        right = c;
      } else {
        left = c;
        word_rank = r;
        word_start = left * RankSample / WordBits;
      }
    }

    assert(word_rank <= idx);
    const size_t total_words = (size_ + WordBits - 1) / WordBits;
    for (int sub_block = 5; sub_block > 0; --sub_block) {
      size_t r = subBlockRank(left, sub_block);
      r = bit ? r : (sub_block * RankSubSample - r);
      if (r + word_rank < idx) {
        size_t new_start = word_start + sub_block * RankSubSample / WordBits;
        if (new_start < total_words) {
          word_start = new_start;
          word_rank += r;
          break;
        }
      }
    }
    assert(word_rank <= idx);

    // Scan words
    for (size_t w = word_start; w < total_words; ++w) {
      size_t pop = __builtin_popcountll(bits_[w]);
      size_t r = word_rank + (bit ? pop : WordBits - pop);
      if (r >= idx) break;
      word_rank = r;
      word_start = w + 1;
    }

    // Scan bits
    size_t id = idx - word_rank;
    assert(id >= 0 && id <= WordBits);
    if (id == 0) return word_start * WordBits;
    size_t w = bits_[word_start];
    if (!bit) w = ~w;
    return word_start * WordBits + WordSelect(w, id);
  }

  size_t size() const {
    return size_;
  }
  size_t count(bool bit) const {
    if (bit) return popcount_;
    return size() - popcount_;
  }
  size_t extra_bits() const;
  size_t bitSize() const {
    return size() + extra_bits();
  }

  ~FastBitVector();
  friend void swap(FastBitVector& a, FastBitVector& b);
 private:
  size_t subBlockRank(size_t block, int sub_block) const {
      return (rank_samples_[block].rel >> (11ull * (5 - sub_block))) & 0x7FFull;
  }

  size_t size_;
  size_t popcount_;

  unsigned long* bits_;
  struct RankBlock {
    uint64_t abs;
    uint64_t rel;
  };

  RankBlock* rank_samples_;
  size_t* select_samples_[2];
};

#endif
