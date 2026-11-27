#include <windows.h>

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
  if (!gse::log_open(gse::join(dir, L"logs\\ship_designer_fix.log"))) return;
  run();
  // 钩子活整个进程，日志句柄不能关
}

}  // namespace gse
