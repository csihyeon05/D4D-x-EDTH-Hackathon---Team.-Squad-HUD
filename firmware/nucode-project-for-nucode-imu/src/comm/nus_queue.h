#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// One latest pending line per sensor; an active line is never interleaved.
class NusQueue {
public:
  enum { KINDS = 5, CAPACITY = 224, CHUNK = 20 };
  bool put(unsigned kind, const char* line, size_t length) {
    if (kind >= KINDS || !length || length >= CAPACITY || line[length-1] != '\n') {
      ++dropped_; return false;
    }
    if (lengths_[kind]) ++dropped_;
    memcpy(pending_[kind], line, length); lengths_[kind] = length;
    return true;
  }
  size_t front(char out[CHUNK + 1]) {
    if (!activeLength_) {
      for (unsigned n=0; n<KINDS; ++n) {
        const unsigned k = (next_ + n) % KINDS;
        if (!lengths_[k]) continue;
        activeLength_ = lengths_[k]; offset_ = 0;
        memcpy(active_, pending_[k], activeLength_); lengths_[k] = 0;
        next_ = (k + 1) % KINDS; break;
      }
    }
    size_t n = activeLength_ - offset_;
    if (n > CHUNK) n = CHUNK;
    memcpy(out, active_ + offset_, n); out[n] = 0; return n;
  }
  void commit(size_t n) {
    if (n > activeLength_ - offset_) return;
    offset_ += n;
    if (offset_ == activeLength_) { activeLength_ = 0; offset_ = 0; }
  }
  void clear() { memset(lengths_, 0, sizeof(lengths_)); activeLength_ = offset_ = next_ = 0; }
  uint32_t dropped() const { return dropped_; }
private:
  char pending_[KINDS][CAPACITY] = {}, active_[CAPACITY] = {};
  size_t lengths_[KINDS] = {}, activeLength_ = 0, offset_ = 0;
  unsigned next_ = 0;
  uint32_t dropped_ = 0;
};
