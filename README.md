# ship_designer_fix

修 Stellaris 舰船设计器里「自动装配 / 填充空槽」把**没研究出来的部件**放进槽里，
然后槽位 tooltip 弹 `§R缺少必要的科技：…§!` 的问题。

状态：**第一版是定位版（只打日志，不改行为）**。跑一次就能把放置路径的真实调用点抓全，
下一版再改成真正拦截。

## 结论（4.5 反编译，4.6 已核到对应代码）

提示文本 `COMPONENT_CANT_BUILD_MISSING_TECH`（`main_3_l_simp_chinese.yml:898` =
`§R缺少必要的科技：$TECHS$§!`）在整个反编译里**只有一处**会拼出来：

`NShipDesignUtil::CanBuildComponent(...)`，条件是该函数第 5 参数（原因 CString）非空且
`CTechnologyPrereqNodeAnd::IsValidFor(tpl->prerequisites, country, 0, 0)` 为假。
而给它传原因串的只有一个地方：**槽位 tooltip**（`TOOLTIP_REQUIRED_COMP_SLOT_INSTANT`）。

所以现象的本质是：**按钮把「你造不了的部件」放进了槽**，鼠标划过去才弹这句。

两处标准不一致：

| 路径 | 选件函数 | 查科技？ |
|---|---|---|
| 手动点列表里的部件 | `CShipDesigner::GetTemplateForSlot` | **查**（逐个 `ComponentIsAllowedOnSlot` → `CanBuildComponent`） |
| 填充空槽 `ApplySelectedElementOnAllEmptySlots` | `CComponentSet::GetTemplateForSlot` | **不查**，只有 `slot_size == tpl.size && slot_type == tpl.type` 就返回**数组里第一个匹配** |
| 自动装配 `CShipDesignerBase::FinishShipDesign` | required 部件走带国家的 `CComponentSet::FindBestRequiredComponent`；武器/设施走 `AddWeaponsToGrowthStage` / `AddUtilities` / `UpgradeComponents`(`FindBestUpgrade`) | required 那步查，武器/设施那步不查 |

一个 `component_set` 里堆着几十上百个科技档（游戏数据实测：`power_core` 104 个、
`combat_computers` 111 个、`thruster_components` 61 个，每档带
`prerequisites = { "tech_xxx" }` + `potential = {...}`），所以「第一个匹配」撞上没研究的档
纯看加载顺序 —— 这就是「有时」；改 mod（往同一个 set 加模板）会改变顺序，所以更常见。

附带一个次要 bug：`CanBuildComponent` 取的国家是 `CGameState::GetLocalObserved()`
（本机观察国），不是设计图归属国，只有 `EDesignOwner == 1`（联邦）分支才枚举成员。
联邦设计 / 观察者视角下可能误报。

## 4.6 (build 0x6AF6BDEC) 地址表

定位方法：唯一的字符串 + RIP 引用反查（`tools/locate.py` 可复现）。

| 用途 | RVA | 依据 |
|---|---|---|
| `NShipDesignUtil::CanBuildComponent` | **0xFA5330** | 引用 `COMPONENT_CANT_BUILD_MISSING_TECH` |
| `CGameState::GetLocalObserved` | 0x302220 | 该函数里 `mov rcx,[g_CurrentGameState]; call` |
| `CComponentTemplate::IsAllowed` | 0x7208A0 | 同上，第 2 个 call |
| `CComponentTemplate::IsValidForCountry` | 0x71EF00 | `param_2==0` 分支 |
| `CTechnologyPrereqNodeAnd::IsValidFor` | 0xB1B1B0 | 科技校验本体 |
| `CTechnologyPrereqNodeAnd::CollectAllMissingTechs` | 0xB1B6E0 | 收集缺哪些科技 |
| `g_CurrentGameState` | 0x3340908 | `mov r13,[rip+0x239b595]` |
| 模板里 prerequisites 节点偏移 | **+0x2D0** | `lea r12,[rdi+0x2d0]`（4.5 是 +0x288） |
| `CCountry::AddRequiredComponentsToShipDesign` | 0x7DEFF0 | 引用 `invalid component template for ship design` |

## 第一版做了什么

`src/anchors.cpp`：detour `CanBuildComponent`。
- 前 40 次调用打参数 + **调用者地址**（`stellaris.exe+偏移`）
- 只对 `结果=false 且有原因串` 的情况额外打一行 caller

进游戏 → 舰船设计器 → 点「填充空槽」「自动装配」→ 退出，把
`logs\ship_designer_fix.log` 发我，就能确认：
1. 放置路径的真实调用点（`ApplySelectedElementOnAllEmptySlots` / `SetComponentOnSlot` 的 RVA）；
2. 被放进槽里的模板到底缺哪几个科技。

## 下一版（真正的补丁）

TODO：hook 放置函数（`CShipDesignSection::SetComponentOnSlot` 等价物），
进入前用已知的两样东西自检：

```c
country = ((CCountry* (*)(void*)) (base + 0x302220))(*((void**)(base + 0x3340908)));
ok      = ((bool (*)(void*, void*, void*, void*)) (base + 0xB1B1B0))(
              (char*)tpl + 0x2D0, country, nullptr, nullptr);
if (!ok) 跳过这次放置;
```

不需要 `growthStage`/`slot`，因为这里只管「缺科技」这一类（也就是你看到的那句提示）。

## 构建

```bash
# MinHook 的 C 文件只需编一次（复用 gamesetup_ext 里的 third_party/minhook）
GXX="$LOCALAPPDATA/gse-toolchain/mingw64/bin/g++.exe"
mkdir -p build-mingw/objhook
for f in ../gamesetup_ext/third_party/minhook/src/*.c ../gamesetup_ext/third_party/minhook/src/hde/*.c; do
  "$GXX" -O2 -I../gamesetup_ext/third_party/minhook/include -I../gamesetup_ext/third_party/minhook/src \
         -c "$f" -o "build-mingw/objhook/$(basename "$f" .c).o"
done
"$GXX" -std=c++17 -O2 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE \
  -DGSE_WITH_MINHOOK -Isrc -I../gamesetup_ext/third_party/minhook/include \
  -shared -static -static-libgcc -static-libstdc++ \
  src/dllmain.cpp src/plugin.cpp src/anchors.cpp src/log.cpp src/util.cpp src/hook_minhook.cpp \
  build-mingw/objhook/*.o -o ship_designer_fix.dll -lole32 -luuid
```

安装/调试：

```bat
stl plugin install "C:\...\反编译\ship_designer_fix" --link
stl launch
```
日志：`logs\ship_designer_fix.log`（`--link` 时在工程目录）。
