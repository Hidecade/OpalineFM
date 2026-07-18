#include "Engine/RealtimeAudioRecorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace opaline
{
namespace
{
constexpr double kRingBufferSeconds = 2.0;
constexpr double kInitialRecordedSeconds = 60.0;
constexpr std::size_t kMinimumRingSamples = 16384;
}

RealtimeAudioRecorder::~RealtimeAudioRecorder()
{
    stop();
}

void RealtimeAudioRecorder::start(const double sampleRate)
{
    stop();

    recordingSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    auto ringSamples = static_cast<std::size_t>(std::ceil(recordingSampleRate * 2.0 * kRingBufferSeconds));
    ringSamples = std::max(kMinimumRingSamples, ringSamples + (ringSamples & 1U));
    ringBuffer.assign(ringSamples, 0.0f);

    recordedInterleaved.clear();
    recordedInterleaved.reserve(static_cast<std::size_t>(recordingSampleRate * 2.0 * kInitialRecordedSeconds));
    writePosition.store(0, std::memory_order_relaxed);
    readPosition.store(0, std::memory_order_relaxed);
    droppedFrames.store(0, std::memory_order_relaxed);
    recordedSampleCount.store(0, std::memory_order_relaxed);
    sessionGeneration.fetch_add(1, std::memory_order_acq_rel);

    collectorRunning.store(true, std::memory_order_release);
    collectorThread = std::thread([this] { collectorLoop(); });
    recording.store(true, std::memory_order_release);
}

void RealtimeAudioRecorder::stop()
{
    recording.store(false, std::memory_order_release);
    sessionGeneration.fetch_add(1, std::memory_order_acq_rel);

    while (activeWriters.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    collectorRunning.store(false, std::memory_order_release);
    if (collectorThread.joinable())
        collectorThread.join();
}

void RealtimeAudioRecorder::push(const float* const left,
                                 const float* const right,
                                 const int numFrames) noexcept
{
    if (left == nullptr || numFrames <= 0
        || !recording.load(std::memory_order_acquire))
    {
        return;
    }

    const auto generation = sessionGeneration.load(std::memory_order_acquire);
    activeWriters.fetch_add(1, std::memory_order_acq_rel);
    if (!recording.load(std::memory_order_acquire)
        || generation != sessionGeneration.load(std::memory_order_acquire))
    {
        activeWriters.fetch_sub(1, std::memory_order_release);
        return;
    }

    const auto write = writePosition.load(std::memory_order_relaxed);
    const auto read = readPosition.load(std::memory_order_acquire);
    const auto capacity = ringBuffer.size();
    const auto requestedSamples = static_cast<std::size_t>(numFrames) * 2;
    const auto usedSamples = write - read;
    auto writableSamples = std::min(requestedSamples, capacity - std::min(capacity, usedSamples));
    writableSamples &= ~std::size_t { 1 };

    auto ringIndex = write % capacity;
    const auto writableFrames = writableSamples / 2;
    for (std::size_t frame = 0; frame < writableFrames; ++frame)
    {
        ringBuffer[ringIndex] = left[frame];
        ringIndex = (ringIndex + 1 == capacity) ? 0 : ringIndex + 1;
        ringBuffer[ringIndex] = right != nullptr ? right[frame] : left[frame];
        ringIndex = (ringIndex + 1 == capacity) ? 0 : ringIndex + 1;
    }

    writePosition.store(write + writableSamples, std::memory_order_release);
    if (writableFrames < static_cast<std::size_t>(numFrames))
    {
        droppedFrames.fetch_add(static_cast<std::uint64_t>(numFrames) - writableFrames,
                                std::memory_order_relaxed);
    }

    activeWriters.fetch_sub(1, std::memory_order_release);
}

std::vector<float> RealtimeAudioRecorder::takeRecordedSamples()
{
    stop();
    auto result = std::move(recordedInterleaved);
    recordedInterleaved.clear();
    recordedSampleCount.store(0, std::memory_order_release);
    return result;
}

std::size_t RealtimeAudioRecorder::drainAvailable()
{
    const auto read = readPosition.load(std::memory_order_relaxed);
    const auto write = writePosition.load(std::memory_order_acquire);
    const auto available = write - read;
    if (available == 0 || ringBuffer.empty())
        return 0;

    const auto capacity = ringBuffer.size();
    const auto ringIndex = read % capacity;
    const auto firstBlock = std::min(available, capacity - ringIndex);
    recordedInterleaved.insert(recordedInterleaved.end(),
                               ringBuffer.begin() + static_cast<std::ptrdiff_t>(ringIndex),
                               ringBuffer.begin() + static_cast<std::ptrdiff_t>(ringIndex + firstBlock));

    const auto secondBlock = available - firstBlock;
    if (secondBlock > 0)
    {
        recordedInterleaved.insert(recordedInterleaved.end(),
                                   ringBuffer.begin(),
                                   ringBuffer.begin() + static_cast<std::ptrdiff_t>(secondBlock));
    }

    readPosition.store(read + available, std::memory_order_release);
    recordedSampleCount.store(recordedInterleaved.size(), std::memory_order_release);
    return available;
}

void RealtimeAudioRecorder::collectorLoop()
{
    while (collectorRunning.load(std::memory_order_acquire)
           || readPosition.load(std::memory_order_relaxed) != writePosition.load(std::memory_order_acquire))
    {
        if (drainAvailable() == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace opaline
