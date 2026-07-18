#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace opaline
{
class RealtimeScopeBuffer
{
public:
    static constexpr std::size_t historySize = 4096;

    void push(const float* samples, int numSamples) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return;

        auto remaining = static_cast<std::size_t>(numSamples);
        auto inputOffset = std::size_t { 0 };
        while (remaining > 0)
        {
            const auto write = writePosition.load(std::memory_order_relaxed);
            const auto read = readPosition.load(std::memory_order_acquire);
            if (write - read >= blockCount)
            {
                droppedSampleCount.fetch_add(remaining, std::memory_order_relaxed);
                return;
            }

            auto& block = blocks[write & (blockCount - 1)];
            block.sampleCount = std::min(remaining, samplesPerBlock);
            for (std::size_t i = 0; i < block.sampleCount; ++i)
                block.samples[i] = std::clamp(samples[inputOffset + i], -1.0f, 1.0f);

            writePosition.store(write + 1, std::memory_order_release);
            inputOffset += block.sampleCount;
            remaining -= block.sampleCount;
        }
    }

    bool drain(std::array<float, historySize>& history, std::size_t& historyWriteIndex) noexcept
    {
        bool consumed = false;
        const auto availableWrite = writePosition.load(std::memory_order_acquire);
        while (consumerPosition < availableWrite)
        {
            const auto& block = blocks[consumerPosition & (blockCount - 1)];
            for (std::size_t i = 0; i < block.sampleCount; ++i)
            {
                history[historyWriteIndex] = block.samples[i];
                historyWriteIndex = (historyWriteIndex + 1) & (historySize - 1);
            }
            ++consumerPosition;
            consumed = true;
        }

        if (consumed)
            readPosition.store(consumerPosition, std::memory_order_release);
        return consumed;
    }

    std::uint64_t droppedSamples() const noexcept
    {
        return droppedSampleCount.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t samplesPerBlock = 1024;
    static constexpr std::size_t blockCount = 16;

    struct Block
    {
        std::array<float, samplesPerBlock> samples {};
        std::size_t sampleCount = 0;
    };

    std::array<Block, blockCount> blocks {};
    std::atomic<std::size_t> writePosition { 0 };
    std::atomic<std::size_t> readPosition { 0 };
    std::atomic<std::uint64_t> droppedSampleCount { 0 };
    std::size_t consumerPosition = 0;
};
} // namespace opaline
