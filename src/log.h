#pragma once

#include <string>

namespace gse {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

bool log_open(const std::wstring& path);
void log_close();
void log_set_level(LogLevel level);
// 崩溃路径专用：无锁、无分配
void log_raw(const char* text);
// 内容按 UTF-8 写入；fmt 只接受 ASCII，中文请先在调用方转成 UTF-8 再传 %s
void logf(LogLevel level, const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_debug(const char* fmt, ...);

}  // namespace gse
