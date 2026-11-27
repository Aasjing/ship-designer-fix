#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gse {

// 4.6 build 0x6AF6BDEC 里定位到的锚点（定位方法见 README）
struct AnchorsRva {
  // NShipDesignUtil::CanBuildComponent(CComponentTemplate*, EDesignOwner, CShipGrowthStage const&,
  //                                    CComponentSlot const*, CString*)
  // 用字符串 "COMPONENT_CANT_BUILD_MISSING_TECH" 的唯一引用者定位
  std::uint32_t can_build_component = 0xFA5330;
  // CShipDesignerBase::ComponentIsAllowedOnSlot(designer, tpl, slot, reason)
  // 运行期实测：选件循环对同一槽逐个候选调用它，返回 true 的那个就是“能造”的
  std::uint32_t component_is_allowed = 0x11CB900;
  // 保存/提示用的逐槽校验：validate(beginEntry, endEntry, ctx)，
  // entry 步长 0x20，[entry+8]=槽，[entry+0x10]=模板；ctx: +0=growthStage +8=ownerType +0x10=reason
  // （引用 MISSING_PREREQUISITES，就是这个函数里的循环拼出“缺科技”）
  std::uint32_t validate_slots = 0x11DC210;

  // 同一函数里出现的目标（核对用，不 hook）
  std::uint32_t get_local_observed = 0x302220;      // CGameState::GetLocalObserved
  std::uint32_t prereq_is_valid = 0xB1B1B0;         // CTechnologyPrereqNodeAnd::IsValidFor
  std::uint32_t collect_missing_techs = 0xB1B6E0;   // CTechnologyPrereqNodeAnd::CollectAllMissingTechs
  std::uint32_t game_state_global = 0x3340908;      // g_CurrentGameState
  std::size_t template_prereq_offset = 0x2D0;       // 模板里 prerequisites 编译结果
};

const AnchorsRva& anchors();

// 配置（ship_designer_fix.ini，缺省 dry_run = yes）
struct Config {
  bool dry_run = false;       // yes = 只记录“本该换成什么”，不真写
  bool enabled = true;
  bool fallback_clear = true; // 记录表里没有可造档时清空该槽（否则保存仍会被拒）
};
const Config& config();

void log_anchor_table();
bool install_probes();

}  // namespace gse
