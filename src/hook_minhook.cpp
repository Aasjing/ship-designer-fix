// MinHook 后端。third_party\minhook 存在并且 CMake 打开 GSE_WITH_MINHOOK 时编译进来。
#include "hook.h"

#if defined(GSE_WITH_MINHOOK)

#include <MinHook.h>

#include "log.h"

namespace gse {

HookStatus hooks_initialize() {
  const MH_STATUS status = MH_Initialize();
  if (status == MH_ERROR_ALREADY_INITIALIZED) return HookStatus::Ok;  // 第二次装 hook 不算失败
  if (status != MH_OK) {
    log_error("MinHook: MH_Initialize failed (%d)", static_cast<int>(status));
    return HookStatus::Error;
  }
  return HookStatus::Ok;
}

HookStatus hooks_install(std::uint64_t target, void* detour, void** original) {
  if (target == 0 || detour == nullptr) return HookStatus::Error;
  void* address = reinterpret_cast<void*>(static_cast<std::uintptr_t>(target));
  MH_STATUS status = MH_CreateHook(address, detour, original);
  if (status != MH_OK) {
    log_error("MinHook: MH_CreateHook(0x%llX) failed (%d)", static_cast<unsigned long long>(target),
              static_cast<int>(status));
    return HookStatus::Error;
  }
  status = MH_EnableHook(address);
  if (status != MH_OK) {
    log_error("MinHook: MH_EnableHook(0x%llX) failed (%d)", static_cast<unsigned long long>(target),
              static_cast<int>(status));
    return HookStatus::Error;
  }
  return HookStatus::Ok;
}

void hooks_shutdown() {
  MH_DisableHook(MH_ALL_HOOKS);
  MH_Uninitialize();
}

const char* hooks_backend() { return "MinHook"; }

}  // namespace gse

#endif  // GSE_WITH_MINHOOK
