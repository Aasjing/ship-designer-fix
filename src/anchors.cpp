#include "anchors.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "hook.h"
#include "log.h"
#include "util.h"

namespace gse {

namespace {

constexpr AnchorsRva kAnchors;
Config g_config;

// 槽条目布局（从 0x11DC210 反汇编读出）
constexpr std::size_t kEntryStride = 0x20;
constexpr std::size_t kEntrySlot = 8;
constexpr std::size_t kEntryTemplate = 0x10;
// ctx 布局
constexpr std::size_t kCtxStage = 0;
constexpr std::size_t kCtxOwner = 8;

// —— “这个槽能造什么” 记录表 ——
// ComponentIsAllowedOnSlot 返回 true 时记下 (slot -> tpl)，后到的相同槽不覆盖：
// 选件循环是升序试，第一次成功的就是“最好的可造档”
constexpr std::size_t kMaxRecords = 2048;
struct Record {
  void* slot = nullptr;
  void* tmpl = nullptr;
};
Record g_records[kMaxRecords];
volatile long g_record_count = 0;
volatile long g_record_overflow = 0;

void record_allowed(void* slot, void* tmpl) {
  if (slot == nullptr || tmpl == nullptr) return;
  const long count = g_record_count;
  for (long i = 0; i < count; ++i) {
    if (g_records[i].slot == slot) {
      // 这个槽已经有可造档了：后面可能是更旧的候选，不覆盖
      return;
    }
  }
  if (count >= static_cast<long>(kMaxRecords)) {
    if (InterlockedIncrement(&g_record_overflow) == 1) {
      log_warn("记录表满了（%u），后续不再记新槽", static_cast<unsigned>(kMaxRecords));
    }
    return;
  }
  const long index = InterlockedIncrement(&g_record_count) - 1;
  if (index < 0 || index >= static_cast<long>(kMaxRecords)) return;
  g_records[index].slot = slot;
  g_records[index].tmpl = tmpl;
}

void* find_allowed(void* slot) {
  const long count = g_record_count;
  for (long i = 0; i < count; ++i) {
    if (g_records[i].slot == slot) return g_records[i].tmpl;
  }
  return nullptr;
}

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

// CanBuildComponent：只用来做自检（问“这个模板对这个槽能不能造”）
using CanBuildFn = bool (*)(const void* tmpl, int owner, const void* stage, const void* slot, void* reason);
CanBuildFn g_can_build = nullptr;

// ComponentIsAllowedOnSlot(designer, tpl, slot, reason)
using AllowedFn = bool (*)(void* designer, const void* tmpl, const void* slot, void* reason);
AllowedFn g_allowed_original = nullptr;
volatile long g_allowed_calls = 0;

bool allowed_detour(void* designer, const void* tmpl, const void* slot, void* reason) {
  const bool result = g_allowed_original != nullptr ? g_allowed_original(designer, tmpl, slot, reason) : false;
  const long n = InterlockedIncrement(&g_allowed_calls);
  if (result) record_allowed(const_cast<void*>(slot), const_cast<void*>(tmpl));
  if (n <= 10) {
    log_info("ALLOWED #%ld tpl=%p slot=%p reason=%p -> %d（记录表 %ld 条）", n, tmpl, slot, reason,
             result ? 1 : 0, g_record_count);
  }
  return result;
}

// validate(begin, end, ctx)
using ValidateFn = bool (*)(void* begin, void* end, void* ctx);
ValidateFn g_validate_original = nullptr;
volatile long g_validate_calls = 0;
volatile long g_repairs = 0;
volatile long g_missing_slot = 0;

// 遍历一个区段的槽条目：把“造不了的模板”换成记录表里“可造的最高档”
void repair_entries(unsigned char* begin, unsigned char* end, void* ctx) {
  if (begin == nullptr || end == nullptr || end <= begin || ctx == nullptr) return;
  unsigned char* fields = static_cast<unsigned char*>(ctx);
  const void* stage = *reinterpret_cast<void**>(fields + kCtxStage);
  const int owner = *reinterpret_cast<int*>(fields + kCtxOwner);

  for (unsigned char* entry = begin; entry + kEntryStride <= end; entry += kEntryStride) {
    void* slot = *reinterpret_cast<void**>(entry + kEntrySlot);
    void* tmpl = *reinterpret_cast<void**>(entry + kEntryTemplate);
    if (slot == nullptr || tmpl == nullptr) continue;
    if (g_can_build != nullptr && g_can_build(tmpl, owner, stage, slot, nullptr)) continue;

    void* better = find_allowed(slot);
    if (better == nullptr || better == tmpl) {
      if (InterlockedIncrement(&g_missing_slot) <= 12) {
        log_info("REPAIR: 槽 %p 里的 %p 造不了，记录表里没有可替代档，保持原样", slot, tmpl);
      }
      continue;
    }
    if (g_can_build != nullptr && !g_can_build(better, owner, stage, slot, nullptr)) {
      if (InterlockedIncrement(&g_missing_slot) <= 12) {
        log_info("REPAIR: 槽 %p 的候选 %p 复核也过不了，跳过", slot, better);
      }
      continue;
    }
    const long n = InterlockedIncrement(&g_repairs);
    if (n <= 40) {
      log_info("REPAIR #%ld: 槽 %p  %p -> %p%s", n, slot, tmpl, better,
               g_config.dry_run ? "（dry_run，未写入）" : "");
    }
    if (!g_config.dry_run) {
      *reinterpret_cast<void**>(entry + kEntryTemplate) = better;
    }
  }
}

bool validate_detour(void* begin, void* end, void* ctx) {
  const long n = InterlockedIncrement(&g_validate_calls);
  if (n <= 3) {
    log_info("VALIDATE #%ld begin=%p end=%p ctx=%p", n, begin, end, ctx);
    log_site("VALIDATE caller", reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
  }
  if (g_config.enabled) {
    repair_entries(static_cast<unsigned char*>(begin), static_cast<unsigned char*>(end), ctx);
  }
  return g_validate_original != nullptr ? g_validate_original(begin, end, ctx) : true;
}

// 读 ship_designer_fix.ini（不引第三方 ini 库，只认 key = value）
void load_config(const std::wstring& dir) {
  const std::wstring path = join(dir, L"ship_designer_fix.ini");
  std::string text;
  std::string raw;
  if (!path_exists(path)) {
    log_info("没有 %s，用默认值（dry_run = yes）", to_utf8(path).c_str());
    return;
  }
  FILE* file = _wfopen(path.c_str(), L"rb");
  if (file == nullptr) return;
  char buffer[4096];
  const std::size_t read = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  buffer[read] = 0;
  raw.assign(buffer, read);
  for (char& c : raw) {
    if (c == '\r') c = '\n';
  }
  const auto value_of = [&raw](const char* key) -> std::string {
    const std::string needle = std::string(key);
    std::size_t pos = 0;
    while ((pos = raw.find(needle, pos)) != std::string::npos) {
      std::size_t equals = raw.find('=', pos + needle.size());
      if (equals == std::string::npos) break;
      std::size_t eol = raw.find('\n', equals);
      std::string value = raw.substr(equals + 1, eol == std::string::npos ? std::string::npos : eol - equals - 1);
      // 去掉空白和注释
      std::size_t comment = value.find('#');
      if (comment != std::string::npos) value.resize(comment);
      std::string trimmed;
      for (char c : value) {
        if (c != ' ' && c != '\t') trimmed.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
      }
      return trimmed;
    }
    return std::string();
  };
  const std::string dry = value_of("dry_run");
  const std::string enabled = value_of("enabled");
  if (!dry.empty()) g_config.dry_run = (dry != "no" && dry != "false" && dry != "0");
  if (!enabled.empty()) g_config.enabled = (enabled != "no" && enabled != "false" && enabled != "0");
  log_info("config: dry_run=%d enabled=%d", g_config.dry_run ? 1 : 0, g_config.enabled ? 1 : 0);
}

bool executable_at(std::uint64_t address) {
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == 0) return false;
  if (info.State != MEM_COMMIT) return false;
  return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                          PAGE_EXECUTE_WRITECOPY)) != 0;
}

}  // namespace

const AnchorsRva& anchors() { return kAnchors; }
const Config& config() { return g_config; }

void log_anchor_table() {
  const std::uint64_t base = runtime_base();
  log_info("anchors: base=0x%llX", static_cast<unsigned long long>(base));
  log_info("  CanBuildComponent        +0x%X", kAnchors.can_build_component);
  log_info("  ComponentIsAllowedOnSlot +0x%X", kAnchors.component_is_allowed);
  log_info("  ValidateSlots(保存校验)  +0x%X", kAnchors.validate_slots);
  log_info("  Prereq::IsValidFor       +0x%X", kAnchors.prereq_is_valid);
  log_info("  g_CurrentGameState       +0x%X", kAnchors.game_state_global);
  log_info("  template prereq offset   +0x%X", static_cast<unsigned>(kAnchors.template_prereq_offset));
}

bool install_probes() {
  const std::wstring dir = dir_name(module_path());
  load_config(dir);

  if (hooks_initialize() != HookStatus::Ok) {
    log_warn("hook backend unavailable (%s)", hooks_backend());
    return false;
  }
  const std::uint64_t base = runtime_base();

  // CanBuildComponent：只取函数指针做自检用
  void* can_build = reinterpret_cast<void*>(base + kAnchors.can_build_component);
  if (executable_at(reinterpret_cast<std::uint64_t>(can_build))) {
    g_can_build = reinterpret_cast<CanBuildFn>(can_build);
  } else {
    log_error("CanBuildComponent 不可执行，版本可能变了");
    return false;
  }

  // ComponentIsAllowedOnSlot：记录“槽 -> 可造模板”
  const std::uint64_t allowed = base + kAnchors.component_is_allowed;
  if (!executable_at(allowed)) {
    log_error("ComponentIsAllowedOnSlot 0x%X 不可执行", kAnchors.component_is_allowed);
    return false;
  }
  if (hooks_install(allowed, reinterpret_cast<void*>(&allowed_detour),
                    reinterpret_cast<void**>(&g_allowed_original)) != HookStatus::Ok) {
    log_error("hook ComponentIsAllowedOnSlot 失败");
    return false;
  }
  log_info("hook ComponentIsAllowedOnSlot 已装（0x%X）", kAnchors.component_is_allowed);

  // 保存校验：进来先把非法槽换成可造档
  const std::uint64_t validate = base + kAnchors.validate_slots;
  if (!executable_at(validate)) {
    log_error("ValidateSlots 0x%X 不可执行", kAnchors.validate_slots);
    return false;
  }
  if (hooks_install(validate, reinterpret_cast<void*>(&validate_detour),
                    reinterpret_cast<void**>(&g_validate_original)) != HookStatus::Ok) {
    log_error("hook ValidateSlots 失败");
    return false;
  }
  log_info("hook ValidateSlots 已装（0x%X）", kAnchors.validate_slots);
  return true;
}

}  // namespace gse
