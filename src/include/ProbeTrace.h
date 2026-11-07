#pragma once

#include <cstddef>
#include <optional>
#include <vector>

// A probed point's value in each frame of a time series, filled a frame at a time. Frames
// come coarse to fine (the first and last, the middle, then the quarters and so on), so a
// partial trace already spans the whole timeline and sharpens as the rest arrive.
class ProbeTrace {
public:
  // frames unread frames; 0 empties the trace.
  void reset(int frames) {
    const auto count = static_cast<size_t>(frames > 0 ? frames : 0);
    values_.assign(count, std::nullopt);
    order_.clear();
    order_.reserve(count);
    next_ = 0;
    if (count == 0) {
      return;
    }
    const int last = frames - 1;
    order_.push_back(0);
    if (last > 0) {
      order_.push_back(last);
    }
    int span = 1;
    while (span < last) {
      span *= 2;
    }
    // Each frame between the ends is an odd multiple of exactly one stride.
    for (int stride = span / 2; stride >= 1; stride /= 2) {
      for (int frame = stride; frame < last; frame += 2 * stride) {
        order_.push_back(frame);
      }
    }
  }

  int frames() const {
    return static_cast<int>(values_.size());
  }

  // The frame to read next, or std::nullopt once every frame has a value.
  std::optional<int> next() const {
    return complete() ? std::nullopt : std::optional<int>(order_[next_]);
  }

  // The value of the frame next() names: NaN where the field has none there.
  void fill(double value) {
    if (!complete()) {
      values_[static_cast<size_t>(order_[next_++])] = value;
    }
  }

  bool complete() const {
    return next_ >= order_.size();
  }

  // Each frame's value, std::nullopt while unread.
  const std::vector<std::optional<double>>& values() const {
    return values_;
  }

private:
  std::vector<std::optional<double>> values_;
  std::vector<int> order_;
  size_t next_ = 0;
};
