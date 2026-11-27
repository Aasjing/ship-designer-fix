#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gse {

// 4.6 build 0x6AF6BDEC 里定位到的锚点（见 README 的定位方法）
struct AnchorsRva {
  // NShipDesignUtil::CanBuildComponent(CComponentTemplate*, EDesignOwner, CShipGrowthStage const&,
  //                                    CComponentSlot const*, CString*)
  // 用字符串 "COMPONENT_CANT_BUILD_MISSING_TECH" 的唯一引用者定位
  std::uint32_t can_build_component = 0xFA5330;
  // 同一函数里出现的几个目标（只用来核对，不 hook）
  std::uint32_t get_local_observed = 0x302220;          // CGameState::GetLocalObserved
  std::uint32_t template_is_allowed = 0x7208A0;         // CComponentTemplate::IsAllowed
  std::uint32_t template_valid_country = 0x71EF00;      // CComponentTemplate::IsValidForCountry
  std::uint32_t prereq_is_valid = 0xB1B1B0;             // CTechnologyPrereqNodeAnd::IsValidFor
  std::uint32_t collect_missing_techs = 0xB1B6E0;       // CTechnologyPrereqNodeAnd::CollectAllMissingTechs
  // 全局：g_CurrentGameState（CanBuildComponent 里 `mov r13,[rip+0x239b595]`）
  std::uint32_t game_state_global = 0x3340908;
  // 模板对象里 prerequisites 编译结果（CTechnologyPrereqNodeAnd）的偏移
  // CanBuildComponent 里 `lea r12, [rdi + 0x2d0]`
  std::size_t template_prereq_offset = 0x2D0;
};

const AnchorsRva& anchors();

void log_anchor_table();
// 装 detour：只打日志，收集“谁在什么时候问过能否建造”以及调用者地址
bool install_probes();

}  // namespace gse
