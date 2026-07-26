#include "GameStreamClientInternal.h"

#include <string>

void GameStreamClient::Impl::HandleControl(
    uint8_t opcode,
    const std::vector<uint8_t>& payload)
{
    if (opcode != 0x1) return;
    const std::string text(payload.begin(), payload.end());
    GameStreamEvent event{};
    {
        std::lock_guard lock(mutex_);
        if (text.find("\"command\"") == std::string::npos)
        {
            return;
        }
        if (text.find("\"pause\"") != std::string::npos)
        {
            ++pauseCommands_;
            event = GameStreamEvent::Pause;
            if (text.find("\"reason\":\"source_preempted\"") !=
                std::string::npos)
            {
                sourceClaimed_ = false;
                sourcePlaying_ = false;
                claimPending_ = false;
                packets_.clear();
                textMessages_.clear();
                pendingPcm_.clear();
            }
        }
        else if (text.find("\"resume\"") != std::string::npos)
        {
            ++resumeCommands_;
            event = GameStreamEvent::Resume;
        }
        else
        {
            return;
        }
    }
    Notify(event);
}
