#pragma once
#include "Core/Application.hpp"
#include "Scripting/Bolt/NativeComponentLayout.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include <map>
#include <mutex>
#include <thread>

namespace Funke::BoltScriptHost::Commands {
    enum class Operation : uint64_t {
        Open,
        Release,
        Create,
        Instantiate,
        Destroy,
        Enable,
        Add,
        DefaultAdd,
        Remove,
        Playback,
        Created,
        Clear,
        Count,
        Commands,
        PlaybackInto
    };
    struct Request {
        Operation Op;
        uint64_t Ticket, Live, Argument;
        uint32_t Size, NameLength;
        const char* Name;
    };
    struct Record {
        uint8_t Op;
        uint32_t Index;
        uint64_t Live, Argument;
        std::string Type;
        uint32_t Size;
        std::vector<uint8_t> Value;
    };
    struct Stream {
        uint32_t Id = 0, Count = 0;
        std::vector<Record> Records;
    };
    struct Buffer {
        std::mutex Mutex;
        std::thread::id Main = std::this_thread::get_id();
        std::map<std::thread::id, Stream> Streams;
        std::vector<uint64_t> Created;
        uint32_t NextId = 1;
    };
    inline std::mutex s_Mutex;
    inline std::map<uint64_t, std::shared_ptr<Buffer>> s_Buffers;
    inline uint64_t s_Next = 1;
    constexpr uint64_t Invalid = UINT64_MAX;

    template <class T> void Append(std::vector<uint8_t>& to, T value) {
        auto bytes = reinterpret_cast<const uint8_t*>(&value);
        to.insert(to.end(), bytes, bytes + sizeof(value));
    }

    inline uint64_t Playback(Buffer& buffer, int mode, bool external, uint64_t* destination, uint32_t capacity) {
        if (!Application::IsMainThread() || mode < 0 || mode > 2)
            return Invalid;
        std::vector<uint8_t> wire;
        std::vector<uint64_t> ids;
        {
            std::lock_guard lock(buffer.Mutex);
            uint64_t entities = 0, records = 0;
            for (const auto& [id, stream] : buffer.Streams) {
                entities += stream.Count;
                records += stream.Records.size();
            }
            if (entities > INT_MAX || records > INT_MAX || sizeof(EcbHeader) + entities * 4 > INT_MAX)
                return Invalid;
            Append(wire, EcbHeader{static_cast<uint32_t>(entities), static_cast<uint32_t>(records)});
            for (uint64_t i = 0; i < entities; i++)
                Append(wire, kEcbNoName);
            uint32_t base = 0;
            auto write = [&](const Stream& stream) {
                for (const auto& rec : stream.Records) {
                    uint32_t type = 0;
                    if (!rec.Type.empty()) {
                        const auto* builtin = BuiltInLayoutName(rec.Type);
                        const auto* info = NativeLayout(builtin ? builtin : "bolt:" + rec.Type, rec.Size);
                        if (!info && !builtin)
                            info = NativeLayout("type:" + rec.Type, rec.Size);
                        if (info) type = info->typeIdU32;
                        if (!type)
                            return false;
                    }
                    size_t size = rec.Value.size() + (rec.Live ? 8 : 0) + (rec.Op == Ecb_SetEntityEnabled ? 1 : 0) +
                                  (rec.Op == Ecb_InstantiatePrefab ? 8 : 0);
                    if (size > UINT16_MAX || wire.size() + size + 11 > INT_MAX)
                        return false;
                    Append(wire, rec.Op);
                    Append(wire, rec.Live ? kEcbNoName : base + rec.Index);
                    Append(wire, type);
                    Append(wire, static_cast<uint16_t>(size));
                    if (rec.Op == Ecb_SetEntityEnabled)
                        Append(wire, static_cast<uint8_t>(rec.Argument != 0));
                    if (rec.Live)
                        Append(wire, rec.Live);
                    if (rec.Op == Ecb_InstantiatePrefab)
                        Append(wire, rec.Argument);
                    wire.insert(wire.end(), rec.Value.begin(), rec.Value.end());
                }
                base += stream.Count;
                return true;
            };
            if (auto main = buffer.Streams.find(buffer.Main); main != buffer.Streams.end() && !write(main->second))
                return Invalid;
            for (const auto& [id, stream] : buffer.Streams)
                if (id != buffer.Main && !write(stream))
                    return Invalid;
            if (external) {
                if (entities > capacity || (entities && !destination) || capacity > INT_MAX) return Invalid;
            } else {
                ids.resize(static_cast<size_t>(entities));
                destination=ids.data();
                capacity=static_cast<uint32_t>(entities);
            }
        }
        int count = ScriptApi().Ecb_PlaybackWithMode(wire.data(),static_cast<int>(wire.size()),destination,static_cast<int>(capacity),mode);
        if (count < 0)
            return Invalid;
        std::lock_guard lock(buffer.Mutex);
        if(!external)buffer.Created = std::move(ids);
        return static_cast<uint64_t>(count);
    }

    inline uint64_t Call(uint64_t handle, void* packet, void* value) noexcept {
        if (!packet)
            return Invalid;
        try {
            const auto& req = *static_cast<const Request*>(packet);
            std::shared_ptr<Buffer> buffer;
            {
                std::lock_guard lock(s_Mutex);
                if (req.Op == Operation::Open) {
                    auto id = s_Next++;
                    s_Buffers.emplace(id, std::make_shared<Buffer>());
                    return id;
                }
                auto it = s_Buffers.find(handle);
                if (it == s_Buffers.end())
                    return Invalid;
                buffer = it->second;
                if (req.Op == Operation::Release) {
                    s_Buffers.erase(it);
                    return 0;
                }
            }
            if (req.Op == Operation::Playback || req.Op == Operation::PlaybackInto)
                return Playback(*buffer,static_cast<int>(req.Argument),req.Op==Operation::PlaybackInto,static_cast<uint64_t*>(value),req.Size);
            std::lock_guard lock(buffer->Mutex);
            if (req.Op == Operation::Created)
                return req.Argument < buffer->Created.size() ? buffer->Created[static_cast<size_t>(req.Argument)] : Invalid;
            if (req.Op == Operation::Clear) {
                buffer->Streams.clear();
                buffer->Created.clear();
                return 0;
            }
            if (req.Op == Operation::Count || req.Op == Operation::Commands) {
                uint64_t total = 0;
                for (auto& [key, stream] : buffer->Streams)
                    total += req.Op == Operation::Count ? stream.Count : stream.Records.size();
                return total;
            }
            auto& stream = buffer->Streams[std::this_thread::get_id()];
            if (!stream.Id) {
                if (!buffer->NextId)
                    return Invalid;
                stream.Id = buffer->NextId++;
            }
            if (req.Op == Operation::Create || req.Op == Operation::Instantiate) {
                if (stream.Count == UINT32_MAX || (req.Op == Operation::Instantiate && !req.Argument))
                    return Invalid;
                if (req.Op == Operation::Instantiate)
                    stream.Records.push_back({Ecb_InstantiatePrefab, stream.Count, 0, req.Argument});
                return (uint64_t(stream.Id) << 32) | stream.Count++;
            }
            const uint32_t index = static_cast<uint32_t>(req.Ticket);
            if (!req.Live && (req.Ticket >> 32 != stream.Id || index >= stream.Count))
                return Invalid;
            Record cmd{0, index, req.Live, req.Argument};
            switch (req.Op) {
            case Operation::Destroy:
                cmd.Op = Ecb_DestroyEntity;
                break;
            case Operation::Enable:
                cmd.Op = Ecb_SetEntityEnabled;
                break;
            case Operation::Add:
            case Operation::DefaultAdd:
            case Operation::Remove:
                if (!req.Name || !req.NameLength || req.NameLength>4096 || req.Size > UINT16_MAX - 8)
                    return Invalid;
                cmd.Type.assign(req.Name, req.NameLength);
                cmd.Size = req.Size;
                cmd.Op = req.Op == Operation::Remove ? Ecb_RemoveComponent
                         : req.Op == Operation::Add  ? Ecb_AddComponent
                                                     : Ecb_DefaultConstructComponent;
                if (req.Op == Operation::Add) {
                    if (!value)
                        return Invalid;
                    auto bytes = static_cast<const uint8_t*>(value);
                    cmd.Value.assign(bytes, bytes + req.Size);
                }
                break;
            default:
                return Invalid;
            }
            stream.Records.push_back(std::move(cmd));
            return 0;
        } catch (...) {
            return Invalid;
        }
    }
} // namespace Funke::BoltScriptHost::Commands
