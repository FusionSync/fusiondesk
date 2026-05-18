#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_LARGE_DATA_SCHEDULER_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_LARGE_DATA_SCHEDULER_H

#include <cstdint>
#include <string>

#include "fusiondesk/core/network/channel_registry.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

network::ChannelKey clipboardSmallDataChannelKey();
network::ChannelKey clipboardLargeDataChannelKey();

struct ClipboardLargeDataScheduleOptions
{
    std::uint64_t smallDataFallbackBytes = 1024 * 1024;
    std::uint64_t largeDataWindowBytes = 0;
    std::uint64_t largeDataInFlightBytes = 0;
};

struct ClipboardLargeDataScheduleResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    network::ChannelKey channel = clipboardSmallDataChannelKey();
    bool usesLargeData = false;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

struct ClipboardLargeDataWindowSnapshot
{
    std::uint64_t maxInFlightBytes = 0;
    std::uint64_t inFlightBytes = 0;
    std::uint64_t reservations = 0;
    std::uint64_t releasedBytes = 0;
};

struct ClipboardLargeDataWindowReserveResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::uint64_t inFlightBytes = 0;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

class ClipboardLargeDataWindow
{
public:
    explicit ClipboardLargeDataWindow(std::uint64_t maxInFlightBytes = 0);

    void setMaxInFlightBytes(std::uint64_t maxInFlightBytes);
    ClipboardLargeDataWindowReserveResult reserve(std::uint64_t bytes);
    void release(std::uint64_t bytes);
    void acknowledge(std::uint64_t bytes);
    ClipboardLargeDataWindowSnapshot snapshot() const;

private:
    std::uint64_t maxInFlightBytes_ = 0;
    std::uint64_t inFlightBytes_ = 0;
    std::uint64_t reservations_ = 0;
    std::uint64_t releasedBytes_ = 0;
};

ClipboardLargeDataScheduleResult scheduleClipboardBulkResponse(
    const network::ChannelRegistry* channels,
    const protocol::PacketEnvelope& packet,
    std::uint64_t payloadBytes,
    const ClipboardLargeDataScheduleOptions& options = {});

void applyClipboardLargeDataSchedule(
    const ClipboardLargeDataScheduleResult& schedule,
    protocol::PacketEnvelope& packet);

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_LARGE_DATA_SCHEDULER_H
