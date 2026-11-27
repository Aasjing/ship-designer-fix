#pragma once

#define GSE_VERSION "0.1.0"

namespace gse {

// 插件主流程。由 DllMain 在独立线程里调用（DllMain 里只做 CreateThread）。
void plugin_main();

}  // namespace gse
