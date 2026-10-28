#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace station
{
	// Fixed clients retain count-derived time exactly. Variable clients accumulate
	// committed durations with compensation; a pending step never rewrites time.
	class StationClock
	{
	public:
		StationClock(uint32_t ratio, float baseSeconds, bool variable = false)
			: mDuration(static_cast<double>(baseSeconds) * ratio), mVariable(variable)
		{
			if (!std::isfinite(mDuration) || mDuration <= 0
				|| mDuration > std::numeric_limits<float>::max())
				throw std::invalid_argument("Station clock requires a finite positive step duration");
		}
		bool variable() const { return mVariable; }
		double stepSeconds() const { return mPending > 0 ? mPending : mDuration; }
		uint64_t steps() const { return mSteps; }
		double elapsedSeconds() const { return mVariable ? mElapsed : static_cast<double>(mSteps) * mDuration; }
		double nextSeconds() const {
			return mVariable ? mElapsed + (mPending - mCompensation)
				: static_cast<double>(mSteps + 1) * mDuration;
		}
		bool prepare(double duration) {
			if (!mVariable || !std::isfinite(duration) || duration <= 0
				|| duration > std::numeric_limits<float>::max()
				|| mSteps == std::numeric_limits<uint64_t>::max()) return false;
			auto const end = mElapsed + (duration - mCompensation);
			if (!std::isfinite(end) || end <= mElapsed) return false;
			mPending = duration;
			return true;
		}
		void cancelStep() { mPending = 0; }
		void commitStep() {
			if (mVariable) {
				auto const end = nextSeconds();
				mCompensation = (end - mElapsed) - (mPending - mCompensation);
				mElapsed = end;
				mDuration = mPending;
				mPending = 0;
			}
			++mSteps;
		}
	private:
		double mDuration;
		bool mVariable;
		double mElapsed{0}, mCompensation{0}, mPending{0};
		uint64_t mSteps{0};
	};
}
