#include "GameStreamClientInternal.h"

#include "GameAudioProtocol.h"

#include <algorithm>
#include <span>
#include <sstream>

namespace
{
constexpr size_t kMaxQueuedPackets = 20;
constexpr size_t kMaxQueuedTextMessages = 12;
constexpr size_t kMaxTextMessageBytes = 4 * 1024 * 1024;

std::string Utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), output.data(), required,
            nullptr, nullptr) != required)
    {
        return {};
    }
    return output;
}

std::wstring JsonText(std::wstring_view text)
{
    std::wstring escaped;
    escaped.reserve(text.size());
    for (const wchar_t character : text)
    {
        if (character == L'\\' || character == L'"')
        {
            escaped.push_back(L'\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}
}

void GameStreamClient::Impl::Start(HWND notifyWindow)
{
    std::lock_guard lock(mutex_);
    if (started_) return;
    notifyWindow_ = notifyWindow;
    stop_ = false;
    started_ = true;
    worker_ = std::thread([this] { Run(); });
}

void GameStreamClient::Impl::Stop()
{
    {
        std::lock_guard lock(mutex_);
        if (!started_) return;
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    started_ = false;
    connected_ = false;
    packets_.clear();
    textMessages_.clear();
    latestMetadata_.clear();
    latestJacket_.clear();
    pendingPcm_.clear();
}

void GameStreamClient::Impl::Push(const DecodedPcmChunk& chunk)
{
    const uint64_t expectedBytes =
        static_cast<uint64_t>(chunk.frames) *
        chunk.channels * sizeof(int16_t);
    std::lock_guard lock(mutex_);
    if (chunk.sampleRate != GameAudioProtocol::kSampleRate ||
        chunk.channels != GameAudioProtocol::kChannels ||
        expectedBytes != chunk.bytes.size())
    {
        ++invalidChunks_;
        lastError_ = L"unsupported source PCM format";
        return;
    }
    sourceStreamId_ = chunk.streamId;
    pendingPcm_.insert(
        pendingPcm_.end(),
        chunk.bytes.begin(),
        chunk.bytes.end());
    size_t consumed = 0;
    while (pendingPcm_.size() - consumed >=
        GameAudioProtocol::kPcm16PayloadBytes)
    {
        const auto payload = std::span(
            pendingPcm_.data() + consumed,
            GameAudioProtocol::kPcm16PayloadBytes);
        auto packet = GameAudioProtocol::EncodePcm16(
            payload, nextSequence_++);
        if (packets_.size() >= kMaxQueuedPackets)
        {
            packets_.pop_front();
            ++droppedPackets_;
        }
        packets_.push_back(std::move(packet));
        maxQueueDepth_ = std::max(
            maxQueueDepth_,
            static_cast<uint64_t>(packets_.size()));
        consumed += GameAudioProtocol::kPcm16PayloadBytes;
    }
    if (consumed > 0)
    {
        pendingPcm_.erase(
            pendingPcm_.begin(),
            pendingPcm_.begin() + consumed);
    }
    wake_.notify_one();
}

void GameStreamClient::Impl::PushText(std::wstring_view json)
{
    std::string message = Utf8(json);
    std::lock_guard lock(mutex_);
    if (message.empty() || message.size() > kMaxTextMessageBytes)
    {
        ++invalidChunks_;
        lastError_ = L"invalid game text message";
        return;
    }

    if (message.find("\"type\":\"metadata\"") != std::string::npos)
    {
        latestMetadata_ = message;
        latestJacket_.clear();
        textMessages_.clear();
        ++metadataMessagesQueued_;
    }
    else if (message.find("\"type\":\"jacket\"") != std::string::npos)
    {
        latestJacket_ = message;
        ++jacketMessagesQueued_;
    }
    if (textMessages_.size() >= kMaxQueuedTextMessages)
    {
        textMessages_.pop_front();
        ++droppedTextMessages_;
    }
    textMessages_.push_back(std::move(message));
    wake_.notify_one();
}

void GameStreamClient::Impl::RequestProbeControl(
    std::wstring_view command)
{
    if (command != L"pause" && command != L"resume")
    {
        return;
    }
    std::lock_guard lock(mutex_);
    diagnosticControls_.emplace_back(
        command == L"pause" ? "pause" : "resume");
    wake_.notify_one();
}

std::wstring GameStreamClient::Impl::MetricsJson() const
{
    std::lock_guard lock(mutex_);
    std::wostringstream json;
    json << L"{\"type\":\"game-stream-metrics\""
         << L",\"endpoint\":\"ws://127.0.0.1:47832\""
         << L",\"connected\":"
         << (connected_ ? L"true" : L"false")
         << L",\"sourceStreamId\":\""
         << JsonText(sourceStreamId_) << L"\""
         << L",\"connectAttempts\":" << connectAttempts_
         << L",\"connections\":" << connections_
         << L",\"packetsSent\":" << packetsSent_
         << L",\"framesSent\":" << framesSent_
         << L",\"pcmBytesSent\":" << pcmBytesSent_
         << L",\"queueDepth\":" << packets_.size()
         << L",\"maxQueueDepth\":" << maxQueueDepth_
         << L",\"droppedPackets\":" << droppedPackets_
         << L",\"sendErrors\":" << sendErrors_
         << L",\"invalidChunks\":" << invalidChunks_
         << L",\"textQueueDepth\":" << textMessages_.size()
         << L",\"textMessagesSent\":" << textMessagesSent_
         << L",\"metadataMessagesQueued\":" << metadataMessagesQueued_
         << L",\"jacketMessagesQueued\":" << jacketMessagesQueued_
         << L",\"droppedTextMessages\":" << droppedTextMessages_
         << L",\"pauseCommands\":" << pauseCommands_
         << L",\"resumeCommands\":" << resumeCommands_
         << L",\"error\":\"" << JsonText(lastError_) << L"\"}";
    return json.str();
}

GameStreamClient::GameStreamClient()
    : impl_(std::make_unique<Impl>())
{
}

GameStreamClient::~GameStreamClient() = default;

void GameStreamClient::Start(HWND notifyWindow)
{
    impl_->Start(notifyWindow);
}

void GameStreamClient::Stop()
{
    impl_->Stop();
}

void GameStreamClient::Push(const DecodedPcmChunk& chunk)
{
    impl_->Push(chunk);
}

void GameStreamClient::PushText(std::wstring_view json)
{
    impl_->PushText(json);
}

void GameStreamClient::RequestProbeControl(
    std::wstring_view command)
{
    impl_->RequestProbeControl(command);
}

std::wstring GameStreamClient::MetricsJson() const
{
    return impl_->MetricsJson();
}
