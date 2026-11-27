#pragma once

#include <cstdint>

namespace gse {

enum class HookStatus { Ok, Unavailable, Error };

// 后端初始化（MinHook 的 MH_Initialize）
HookStatus hooks_initialize();
// 在 target（模块基址+RVA 的绝对值）上装一个 detour，original 收到 trampoline
HookStatus hooks_install(std::uint64_t target, void* detour, void** original);
void hooks_shutdown();
const char* hooks_backend();

}  // namespace gse
