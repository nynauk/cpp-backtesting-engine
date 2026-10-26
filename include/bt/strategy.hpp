#pragma once
// Strategies only express intent. After bar t closes, a strategy sees bars[0..t] and returns a target position
// in units; the engine decides when and at what price that target is filled. Strategies never touch cash.

#include <cstddef>
#include <stdexcept>
#include <string>

#include "bt/types.hpp"

namespace bt {

// Read-only view of the bars that have closed so far. The engine builds it with size t + 1, so bar t + 1 is
// simply not in it: indexing past the end throws instead of quietly reading a future price.
class History {
public:
    History(const Bar* data, std::size_t size) : data_(data), size_(size) {
        if (size == 0) throw std::invalid_argument("History needs at least one closed bar");
    }
    std::size_t size() const { return size_; }
    // Index of the bar that has just closed.
    std::size_t last_index() const { return size_ - 1; }
    const Bar& back() const { return data_[size_ - 1]; }
    const Bar& operator[](std::size_t i) const {
        if (i >= size_)
            throw std::out_of_range("look-ahead: bar " + std::to_string(i) + " requested after bar " +
                                    std::to_string(size_ - 1) + " closed");
        return data_[i];
    }

private:
    const Bar* data_;
    std::size_t size_;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    // Called once per bar, in order, right after history.back() closes.
    virtual double target_position(const History& history) = 0;
    // Clears internal state so the same object can be reused for another run.
    virtual void reset() {}
};

// Long `units` while the short moving average of closes is above the long one, short `units` while it is
// below, flat until the long window is full. A difference within floating-point noise (1e-12 relative) keeps
// the previous target, so exact ties do not depend on summation order.
class MovingAverageCrossover : public Strategy {
public:
    MovingAverageCrossover(std::size_t short_window, std::size_t long_window, double units);
    double target_position(const History& history) override;
    void reset() override;

private:
    std::size_t short_window_;
    std::size_t long_window_;
    double units_;
    double short_sum_ = 0.0;
    double long_sum_ = 0.0;
    std::size_t next_t_ = 0;
    double last_target_ = 0.0;
};

// Holds `units` from the first bar onwards: the benchmark the crossover is compared with.
class BuyAndHold : public Strategy {
public:
    explicit BuyAndHold(double units) : units_(units) {}
    double target_position(const History&) override { return units_; }

private:
    double units_;
};

// Never trades: equity must stay exactly at the starting cash.
class NoTrade : public Strategy {
public:
    double target_position(const History&) override { return 0.0; }
};

}  // namespace bt
