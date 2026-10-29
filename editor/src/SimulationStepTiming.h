#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

namespace station::editor {
struct SimulationStepTimingSample {
    double solverMicroseconds{},luaMicroseconds{};
    double totalMicroseconds() const noexcept {return solverMicroseconds+luaMicroseconds;}
};
// Fifty completed updates (5 s at 10 Hz), independent of simulated duration.
// Pausing freezes history; startup uses collected samples, not padded zeros.
class SimulationStepTiming {
public:
    static constexpr std::size_t windowSize=50,averageWindowSize=10;
    void recordMicroseconds(double solver,double lua) noexcept {
        samples_[next_]={solver,lua};next_=(next_+1)%windowSize;
        if (count_<windowSize) ++count_;
    }
    // Chronological indexing, oldest first, even after ring rollover.
    const SimulationStepTimingSample& sample(std::size_t index) const noexcept {
        assert(index<count_);
        return samples_[(next_+windowSize-count_+index)%windowSize];
    }
    SimulationStepTimingSample average() const noexcept {
        const auto count=std::min(count_,averageWindowSize);SimulationStepTimingSample sum;
        for (std::size_t i=count_-count;i<count_;++i) {
            sum.solverMicroseconds+=sample(i).solverMicroseconds;
            sum.luaMicroseconds+=sample(i).luaMicroseconds;
        }
        if (count) {sum.solverMicroseconds/=count;sum.luaMicroseconds/=count;}
        return sum;
    }
    double averageMicroseconds() const noexcept {return average().totalMicroseconds();}
    std::size_t sampleCount() const noexcept {return count_;}
private:
    std::array<SimulationStepTimingSample,windowSize> samples_{};
    std::size_t next_{},count_{};
};
} // namespace station::editor
