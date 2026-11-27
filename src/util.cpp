#include "util.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace gse {

namespace {

std::wstring module_path_impl() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&module_path_impl), &module)) {
    return std::wstring();
  }
  wchar_t buffer[MAX_PATH * 4];
  const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH * 4);
  return std::wstring(buffer, length);
}

}  // namespace

std::wstring exe_path() {
  wchar_t buffer[MAX_PATH * 4];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
  return std::wstring(buffer, length);
}

std::wstring module_path() {
  static const std::wstring cached = module_path_impl();
  return cached;
}

std::wstring dir_name(const std::wstring& path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return std::wstring();
  return path.substr(0, pos);
}

std::wstring file_name(const std::wstring& path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return path;
  return path.substr(pos + 1);
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  std::wstring out = a;
  if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
  out += b;
  return out;
}

bool path_exists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

bool is_dir(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool make_dirs(const std::wstring& path) {
  if (path.empty()) return false;
  if (is_dir(path)) return true;
  std::wstring parent = dir_name(path);
  if (!parent.empty() && parent != path && !is_dir(parent)) {
    if (!make_dirs(parent)) return false;
  }
  if (CreateDirectoryW(path.c_str(), nullptr) != 0) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<std::wstring> list_dir(const std::wstring& dir, const wchar_t* pattern, bool dirs) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW data;
  HANDLE handle = FindFirstFileW(join(dir, pattern).c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return out;
  do {
    const bool is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (is_directory == dirs && wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
      out.push_back(data.cFileName);
    }
  } while (FindNextFileW(handle, &data) != 0);
  FindClose(handle);
  std::sort(out.begin(), out.end());
  return out;
}

bool read_file(const std::wstring& path, std::string& out) {
  out.clear();
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  char buffer[64 * 1024];
  DWORD read = 0;
  while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) != 0 && read > 0) {
    out.append(buffer, read);
  }
  CloseHandle(handle);
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
    out.erase(0, 3);
  }
  return true;
}

bool write_file(const std::wstring& path, const std::string& data) {
  make_dirs(dir_name(path));
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != 0 &&
                  written == data.size();
  CloseHandle(handle);
  return ok;
}

bool write_file_atomic(const std::wstring& path, const std::string& data) {
  const std::wstring tmp = path + L".tmp";
  if (!write_file(tmp, data)) return false;
  if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
    return true;
  }
  DeleteFileW(tmp.c_str());
  return false;
}

std::wstring documents_dir() {
  PWSTR raw = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw)) && raw != nullptr) {
    std::wstring out(raw);
    CoTaskMemFree(raw);
    return out;
  }
  wchar_t buffer[MAX_PATH * 4];
  const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH * 4);
  if (length > 0) return join(std::wstring(buffer, length), L"Documents");
  return std::wstring();
}

std::uint64_t runtime_base() {
  const HMODULE module = GetModuleHandleW(nullptr);
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(module));
}

bool json_string_value(const std::string& text, const char* key, std::wstring& out) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string::npos) return false;
  pos = text.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  pos = text.find('"', pos);
  if (pos == std::string::npos) return false;
  const size_t end = text.find('"', pos + 1);
  if (end == std::string::npos) return false;
  out = from_utf8(text.substr(pos + 1, end - pos - 1));
  return true;
}

std::wstring expand_known_folder(const std::wstring& path) {
  const std::wstring docs_token = L"%USER_DOCUMENTS%";
  const std::wstring profile_token = L"%USERPROFILE%";
  if (path.rfind(docs_token, 0) == 0) return documents_dir() + path.substr(docs_token.size());
  if (path.rfind(profile_token, 0) == 0) {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH * 4);
    if (length > 0) return std::wstring(buffer, length) + path.substr(profile_token.size());
  }
  return path;
}

std::string to_utf8(const std::wstring& s) {  if (s.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring from_utf8(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (size <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size);
  return out;
}

std::wstring trim(const std::wstring& s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && iswspace(s[begin])) ++begin;
  while (end > begin && iswspace(s[end - 1])) --end;
  return s.substr(begin, end - begin);
}

std::wstring lower(std::wstring s) {
  for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
  return s;
}

bool iequals(const std::wstring& a, const std::wstring& b) { return lower(a) == lower(b); }

bool ends_with_i(const std::wstring& s, const std::wstring& suffix) {
  if (s.size() < suffix.size()) return false;
  return iequals(s.substr(s.size() - suffix.size()), suffix);
}

}  // namespace gse
