#include <windows.h>

#include <cstdio>

#include <string>

#include "anchors.h"
#include "log.h"
#include "plugin.h"
#include "util.h"

namespace {

void run() {
  std::wstring dir = gse::dir_name(gse::module_path());
  gse::log_info("ship_designer_fix start, dll dir=%s", gse::to_utf8(dir).c_str());
  gse::log_info("game=%s", gse::to_utf8(gse::exe_path()).c_str());
  gse::log_anchor_table();
  gse::install_probes();
  gse::log_info("done");
}

}  // namespace

namespace gse {

void plugin_main() {
  const std::wstring dir = gse::dir_name(gse::module_path());
  if (dir.empty()) return;
  // 主日志放 DLL 旁边（--link 安装时就是工程目录）；再兜底写一份到 %TEMP%，
  // 这样即使启动器把插件复制到别处、没开 --link，也能找到日志确认到底跑没跑
  bool opened = gse::log_open(gse::join(dir, L"logs\\ship_designer_fix.log"));
  if (!opened) {
    wchar_t temp[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, temp) != 0) {
      opened = gse::log_open(gse::join(temp, L"ship_designer_fix.log"));
    }
  }
  if (!opened) return;
  gse::log_info("dll 路径: %s", gse::to_utf8(gse::module_path()).c_str());
  gse::log_info("日志目录: %s", gse::to_utf8(dir).c_str());
  run();
  // 钩子活整个进程，日志句柄不能关
}

}  // namespace gse
