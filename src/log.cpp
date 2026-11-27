#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "util.h"

namespace gse {

namespace {

CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
HANDLE g_file = INVALID_HANDLE_VALUE;
LogLevel g_level = LogLevel::Info;
std::wstring g_path;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return "ERR ";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Info: return "INFO";
    default: return "DBG ";
  }
}

void write_line(const char* line) {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_lock);
  if (g_file != INVALID_HANDLE_VALUE) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[64];
    const int n = snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond,
                           st.wMilliseconds);
    DWORD written = 0;
    if (n > 0) WriteFile(g_file, stamp, static_cast<DWORD>(n), &written, nullptr);
    WriteFile(g_file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    WriteFile(g_file, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(g_file);
  }
  LeaveCriticalSection(&g_lock);
}

}  // namespace

bool log_open(const std::wstring& path) {
  if (!g_lock_ready) {
    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;
  }
  make_dirs(dir_name(path));
  // 上一次运行的日志留一份：崩溃后重启会把 gamesetup_ext.log 覆盖掉，排查就没证据了
  if (path_exists(path)) {
    std::wstring backup = path;
    const std::size_t dot = backup.rfind(L'.');
    if (dot == std::wstring::npos) {
      backup += L".prev";
    } else {
      backup.insert(dot, L".prev");
    }
    MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
  }
  EnterCriticalSection(&g_lock);
  if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
  g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) {
    LeaveCriticalSection(&g_lock);
    return false;
  }
  g_path = path;
  // BOM，方便用记事本/VSCode 直接看
  DWORD written = 0;
  WriteFile(g_file, "\xEF\xBB\xBF", 3, &written, nullptr);
  LeaveCriticalSection(&g_lock);
  return true;
}

void log_close() {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_lock);
  if (g_file != INVALID_HANDLE_VALUE) {
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
  }
  LeaveCriticalSection(&g_lock);
}

void log_set_level(LogLevel level) { g_level = level; }

// 崩溃处理专用：不拿锁、不分配，直接往日志句柄写。若崩在持有日志锁的线程里，
// 走 logf 会死锁，所以崩溃路径必须用这个。
void log_raw(const char* text) {
  if (!g_lock_ready || g_file == INVALID_HANDLE_VALUE || text == nullptr) return;
  DWORD written = 0;
  WriteFile(g_file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
  WriteFile(g_file, "\r\n", 2, &written, nullptr);
  FlushFileBuffers(g_file);
}

void logf(LogLevel level, const char* fmt, ...) {
  if (static_cast<int>(level) > static_cast<int>(g_level)) return;
  char buffer[4096];
  const int prefix = snprintf(buffer, sizeof(buffer), "[%s] ", level_name(level));
  if (prefix <= 0) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer + prefix, sizeof(buffer) - static_cast<size_t>(prefix), fmt, args);
  va_end(args);
  write_line(buffer);
}

void log_error(const char* fmt, ...) {
  if (LogLevel::Error > g_level) return;
  char text[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  logf(LogLevel::Error, "%s", text);
}

void log_warn(const char* fmt, ...) {
  if (LogLevel::Warn > g_level) return;
  char text[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  logf(LogLevel::Warn, "%s", text);
}

void log_info(const char* fmt, ...) {
  if (LogLevel::Info > g_level) return;
  char text[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  logf(LogLevel::Info, "%s", text);
}

void log_debug(const char* fmt, ...) {
  if (LogLevel::Debug > g_level) return;
  char text[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  logf(LogLevel::Debug, "%s", text);
}

}  // namespace gse
