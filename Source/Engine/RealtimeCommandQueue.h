#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace opaline
{
template <typename Command, std::size_t Capacity>
class RealtimeCommandQueue
{
    static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0,
                  "Realtime command queue capacity must be a power of two");

public:
    RealtimeCommandQueue()
    {
        for (std::size_t index = 0; index < Capacity; ++index)
            slots[index].sequence.store(index, std::memory_order_relaxed);
    }

    bool push(const Command& command) noexcept
    {
        std::size_t position = enqueuePosition.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& slot = slots[position & (Capacity - 1)];
            const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position);
            if (difference == 0)
            {
                if (enqueuePosition.compare_exchange_weak(position, position + 1,
                                                          std::memory_order_relaxed))
                {
                    slot.command = command;
                    slot.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = enqueuePosition.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(Command& command) noexcept
    {
        auto& slot = slots[dequeuePosition & (Capacity - 1)];
        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::intptr_t>(sequence)
            - static_cast<std::intptr_t>(dequeuePosition + 1);
        if (difference != 0)
            return false;

        command = slot.command;
        slot.sequence.store(dequeuePosition + Capacity, std::memory_order_release);
        ++dequeuePosition;
        return true;
    }

private:
    struct Slot
    {
        std::atomic<std::size_t> sequence { 0 };
        Command command {};
    };

    std::array<Slot, Capacity> slots {};
    std::atomic<std::size_t> enqueuePosition { 0 };
    std::size_t dequeuePosition = 0;
};
} // namespace opaline
