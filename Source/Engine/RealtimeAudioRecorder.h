#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace opaline
{
class RealtimeAudioRecorder
{
public:
    RealtimeAudioRecorder() = default;
    ~RealtimeAudioRecorder();

    void start(double sampleRate);
    void stop();
    void push(const float* left, const float* right, int numFrames) noexcept;

    bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }
    double sampleRate() const noexcept { return recordingSampleRate; }
    std::size_t recordedFrameCount() const noexcept
    {
        return recordedSampleCount.load(std::memory_order_acquire) / 2;
    }
    std::uint64_t droppedFrameCount() const noexcept { return droppedFrames.load(std::memory_order_relaxed); }
    std::vector<float> takeRecordedSamples();

private:
    std::size_t drainAvailable();
    void collectorLoop();

    std::vector<float> ringBuffer;
    std::vector<float> recordedInterleaved;
    std::thread collectorThread;
    std::atomic<std::size_t> writePosition { 0 };
    std::atomic<std::size_t> readPosition { 0 };
    std::atomic<std::uint64_t> sessionGeneration { 0 };
    std::atomic<std::uint32_t> activeWriters { 0 };
    std::atomic<std::uint64_t> droppedFrames { 0 };
    std::atomic<std::size_t> recordedSampleCount { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> collectorRunning { false };
    double recordingSampleRate = 44100.0;
};
} // namespace opaline
