#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gse {

// 当前进程的 exe（游戏本体）路径
std::wstring exe_path();
// 本 DLL 的路径（按启动器插件规范的规则 1：从自身模块取，不用工作目录）
std::wstring module_path();

std::wstring dir_name(const std::wstring& path);
std::wstring file_name(const std::wstring& path);
std::wstring join(const std::wstring& a, const std::wstring& b);

bool path_exists(const std::wstring& path);
bool is_dir(const std::wstring& path);
bool make_dirs(const std::wstring& path);

// dirs=true 时只返回目录名，否则只返回匹配 pattern 的文件名（不含路径）
std::vector<std::wstring> list_dir(const std::wstring& dir, const wchar_t* pattern, bool dirs);

bool read_file(const std::wstring& path, std::string& out);
bool write_file(const std::wstring& path, const std::string& data);
// 先写 .tmp 再 MoveFileEx 替换：插件在游戏读取数据前后台写盘，避免读到半个文件
bool write_file_atomic(const std::wstring& path, const std::string& data);

std::wstring documents_dir();
// 当前进程里 exe 的实际加载基址（开 ASLR 时 != 文件里的首选基址 0x140000000）
std::uint64_t runtime_base();
// 从一段 JSON 文本里取 "key": "value" 的字符串值（够用即可，不引入 JSON 库）
bool json_string_value(const std::string& text, const char* key, std::wstring& out);
// 展开 %USER_DOCUMENTS% / %USERPROFILE% 这类前缀
std::wstring expand_known_folder(const std::wstring& path);

std::string to_utf8(const std::wstring& s);
std::wstring from_utf8(const std::string& s);

std::wstring trim(const std::wstring& s);
std::wstring lower(std::wstring s);
bool iequals(const std::wstring& a, const std::wstring& b);
bool ends_with_i(const std::wstring& s, const std::wstring& suffix);

}  // namespace gse
