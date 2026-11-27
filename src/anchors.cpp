#include "anchors.h"

#include <windows.h>

#include "hook.h"
#include "log.h"
#include "util.h"

namespace gse {

namespace {

constexpr AnchorsRva kAnchors;

// NShipDesignUtil::CanBuildComponent 的签名（4.6）
//   rcx = CComponentTemplate*
//   edx = EDesignOwner（0 = 本地/玩家, 1 = 联邦, 2 = 其他, 3 = ?）
//   r8  = CShipGrowthStage const&
//   r9  = CComponentSlot const*
//   [rsp+0x20] = CString* 原因（非空才会收集缺哪些科技）
using CanBuildFn = bool (*)(const void* tmpl, int owner, const void* stage, const void* slot,
                            void* reason);

CanBuildFn g_can_build_original = nullptr;
volatile long g_calls = 0;

// 把地址写成 模块+偏移，方便对着反汇编找
void log_site(const char* tag, std::uintptr_t address) {
  HMODULE module = nullptr;
  if (address >= 0x10000 &&
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module) != 0 &&
      module != nullptr) {
    log_info("%s 0x%llX = stellaris.exe+0x%llX", tag, static_cast<unsigned long long>(address),
             static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
  } else {
    log_info("%s 0x%llX（不在模块里）", tag, static_cast<unsigned long long>(address));
  }
}

bool can_build_detour(const void* tmpl, int owner, const void* stage, const void* slot, void* reason) {
  const long n = InterlockedIncrement(&g_calls);
  const bool result = g_can_build_original != nullptr
                          ? g_can_build_original(tmpl, owner, stage, slot, reason)
                          : false;
  // 只记“会被报缺科技”的那些（result=false 且有原因串），以及前 40 次调用看调用点
  if (n <= 40) {
    log_info("CANBUILD #%ld tmpl=%p owner=%d stage=%p slot=%p reason=%p -> %d", n, tmpl, owner, stage,
             slot, reason, result ? 1 : 0);
    log_site("CANBUILD caller", reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
  }
  if (!result && reason != nullptr) {
    log_info("CANBUILD 缺科技: tmpl=%p slot=%p（调用点见下一行）", tmpl, slot);
    log_site("CANBUILD missing-tech caller",
             reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
  }
  return result;
}

}  // namespace

const AnchorsRva& anchors() { return kAnchors; }

void log_anchor_table() {
  const std::uint64_t base = runtime_base();
  log_info("anchors: base=0x%llX build-timestamp=0x%X", static_cast<unsigned long long>(base),
           0x6AF6BDEC);
  log_info("  CanBuildComponent      +0x%X", kAnchors.can_build_component);
  log_info("  GetLocalObserved       +0x%X", kAnchors.get_local_observed);
  log_info("  Template::IsAllowed    +0x%X", kAnchors.template_is_allowed);
  log_info("  Prereq::IsValidFor     +0x%X", kAnchors.prereq_is_valid);
  log_info("  CollectAllMissingTechs +0x%X", kAnchors.collect_missing_techs);
  log_info("  g_CurrentGameState     +0x%X", kAnchors.game_state_global);
  log_info("  template prereq offset +0x%X", static_cast<unsigned>(kAnchors.template_prereq_offset));
}

bool install_probes() {
  if (hooks_initialize() != HookStatus::Ok) {
    log_warn("hook backend unavailable (%s)", hooks_backend());
    return false;
  }
  const std::uint64_t target = runtime_base() + kAnchors.can_build_component;
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(reinterpret_cast<void*>(target), &info, sizeof(info)) == 0 ||
      (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY)) == 0) {
    log_error("CanBuildComponent 0x%llX 不可执行，版本可能变了，跳过",
              static_cast<unsigned long long>(target));
    return false;
  }
  if (hooks_install(target, reinterpret_cast<void*>(&can_build_detour),
                    reinterpret_cast<void**>(&g_can_build_original)) != HookStatus::Ok) {
    log_error("hook CanBuildComponent 失败");
    return false;
  }
  log_info("hook CanBuildComponent 已装（0x%X）", kAnchors.can_build_component);
  return true;
}

}  // namespace gse
