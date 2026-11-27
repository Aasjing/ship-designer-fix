#include <windows.h>

#include "plugin.h"

// 启动器用 LoadLibraryW 在远端线程里加载本 DLL，此时游戏已经在跑。
// DllMain 里只做最小的事：起一个自己的线程，剩下全部放在那个线程里做。
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(
        nullptr, 0,
        [](LPVOID) -> DWORD {
          gse::plugin_main();
          return 0;
        },
        nullptr, 0, nullptr);
    if (thread != nullptr) CloseHandle(thread);
  }
  return TRUE;
}
