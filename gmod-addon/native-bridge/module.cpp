#include "GarrysMod/Lua/Interface.h"

#include <windows.h>
#include <string>

using namespace GarrysMod::Lua;

namespace {
HANDLE pipeHandle = INVALID_HANDLE_VALUE;
std::string receiveBuffer;
constexpr const char *kPipeName = "\\\\.\\pipe\\SBGLBridge";

bool ensureConnected() {
    if (pipeHandle != INVALID_HANDLE_VALUE) return true;
    if (!WaitNamedPipeA(kPipeName, 0)) return false;
    pipeHandle = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipeHandle == INVALID_HANDLE_VALUE) return false;
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState(pipeHandle, &mode, nullptr, nullptr);
    return true;
}

void closePipe() {
    if (pipeHandle != INVALID_HANDLE_VALUE) CloseHandle(pipeHandle);
    pipeHandle = INVALID_HANDLE_VALUE;
    receiveBuffer.clear();
}

LUA_FUNCTION(BridgeConnect) {
    LUA->PushBool(ensureConnected());
    return 1;
}

LUA_FUNCTION(BridgeSend) {
    const char *message = LUA->CheckString(1);
    if (!ensureConnected()) {
        LUA->PushBool(false);
        return 1;
    }
    std::string line(message);
    line.push_back('\n');
    DWORD written = 0;
    const BOOL ok = WriteFile(pipeHandle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    if (!ok || written != line.size()) {
        closePipe();
        LUA->PushBool(false);
        return 1;
    }
    LUA->PushBool(true);
    return 1;
}

LUA_FUNCTION(BridgePoll) {
    if (!ensureConnected()) {
        LUA->PushString("");
        return 1;
    }
    char buffer[4096];
    DWORD available = 0;
    if (!PeekNamedPipe(pipeHandle, nullptr, 0, nullptr, &available, nullptr)) {
        closePipe();
        LUA->PushString("");
        return 1;
    }
    while (available > 0) {
        DWORD read = 0;
        const DWORD amount = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
        if (!ReadFile(pipeHandle, buffer, amount, &read, nullptr) || read == 0) break;
        receiveBuffer.append(buffer, read);
        if (!PeekNamedPipe(pipeHandle, nullptr, 0, nullptr, &available, nullptr)) break;
    }
    const auto newline = receiveBuffer.find('\n');
    if (newline == std::string::npos) {
        LUA->PushString("");
        return 1;
    }
    const std::string line = receiveBuffer.substr(0, newline);
    receiveBuffer.erase(0, newline + 1);
    LUA->PushString(line.c_str());
    return 1;
}
}

GMOD_MODULE_OPEN() {
    LUA->PushSpecial(SPECIAL_GLOB);
    LUA->PushString("connect");
    LUA->PushCFunction(BridgeConnect);
    LUA->SetTable(-3);
    LUA->PushString("send");
    LUA->PushCFunction(BridgeSend);
    LUA->SetTable(-3);
    LUA->PushString("poll");
    LUA->PushCFunction(BridgePoll);
    LUA->SetTable(-3);
    return 0;
}

GMOD_MODULE_CLOSE() {
    closePipe();
    return 0;
}
