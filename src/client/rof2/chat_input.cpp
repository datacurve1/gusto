#include "gusto/client/rof2/chat_input.hpp"

#include "gusto/client/rof2/addresses.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/hook_utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace gusto::client::rof2 {
namespace {

constexpr uintptr_t kHandleKeyboardMsg = 0x876530;
constexpr uintptr_t kSetSelection = 0x88c0c0;
constexpr uintptr_t kReplaceSelection = 0x87cd30;
constexpr uintptr_t kCxStrConstruct = 0x805c20;

constexpr size_t kFocusWindowOffset = 0x64;
constexpr size_t kKeyboardFlagsOffset = 0x9d;
constexpr size_t kChatWindowInputOffset = 0x224;
constexpr size_t kInputTextOffset = 0x1ec;
constexpr size_t kStartPositionOffset = 0x1dc;
constexpr size_t kEndPositionOffset = 0x1e0;
constexpr size_t kMaxBytesOffset = 0x1e8;
constexpr size_t kTagPrintableStartsOffset = 0x1f0;
constexpr size_t kTagPrintableEndsOffset = 0x218;
constexpr size_t kTagOriginalStartsOffset = 0x240;
constexpr size_t kTagOriginalEndsOffset = 0x268;
constexpr size_t kTagCountOffset = 0x308;
constexpr size_t kEditableOffset = 0x313;
constexpr size_t kCaretAtEndOffset = 0x311;
constexpr size_t kAnchorAtStartOffset = 0x310;
constexpr size_t kChatWindowCount = 32;
constexpr size_t kMaxTextBytes = 4096;
constexpr size_t kCxStrTextOffset = 0x14;

struct CxStrRep {
  LONG references;
  uint32_t capacity;
  uint32_t length;
  uint32_t encoding; // 0: UTF-8, 1: UTF-16.
  void* freeList;
};

struct NativeCxStr {
  void* representation = nullptr;
};

using KeyboardFn = int(__thiscall*)(void*, uint32_t, bool);
using CxStrConstructorFn = void(__thiscall*)(NativeCxStr*, const char*);
using SetSelectionFn = void(__thiscall*)(void*, int, int);
using ReplaceSelectionFn = bool(__thiscall*)(void*, NativeCxStr, bool);

HMODULE g_module = nullptr;
ValidatedClient g_validation;
KeyboardFn g_originalKeyboard = nullptr;
CxStrConstructorFn g_construct = nullptr;
SetSelectionFn g_setSelection = nullptr;
ReplaceSelectionFn g_replaceSelection = nullptr;
volatile LONG g_installed = 0;

void Trace(const char* message) {
  if (auto* file = std::fopen("gusto-chat-input.log", "a")) {
    std::fprintf(file, "%lu %s\n", GetTickCount(), message);
    std::fclose(file);
  }
}

bool Readable(const void* address, size_t length) {
  auto current = reinterpret_cast<uintptr_t>(address);
  if (!current || !length || length > UINTPTR_MAX - current) return false;
  const auto end = current + length;
  while (current < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(current), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
      return false;
    const DWORD protection = info.Protect & 0xff;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
      return false;
    const auto regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (regionEnd <= current) return false;
    current = (std::min<uintptr_t>)(regionEnd, end);
  }
  return true;
}

template <typename T>
bool Read(const void* object, size_t offset, T& value) {
  const auto base = reinterpret_cast<uintptr_t>(object);
  if (!base || offset > UINTPTR_MAX - base) return false;
  const auto* source = reinterpret_cast<const void*>(base + offset);
  if (!Readable(source, sizeof(value))) return false;
  std::memcpy(&value, source, sizeof(value));
  return true;
}

template <typename T>
bool Write(void* object, size_t offset, const T& value) {
  const auto base = reinterpret_cast<uintptr_t>(object);
  if (!base || offset > UINTPTR_MAX - base) return false;
  auto* target = reinterpret_cast<void*>(base + offset);
  if (!Readable(target, sizeof(value))) return false;
  std::memcpy(target, &value, sizeof(value));
  return true;
}

bool IsChatInput(void* manager, void* focused) {
  if (!focused || manager != ResolvePointer(g_module, g_validation,
          addresses::WindowManager, 0x178))
    return false;
  void* chatManager = ResolvePointer(g_module, g_validation,
      addresses::ChatWindowManager, kChatWindowCount * sizeof(void*));
  if (!chatManager) return false;
  for (size_t i = 0; i < kChatWindowCount; ++i) {
    void* chatWindow = nullptr;
    void* input = nullptr;
    if (Read(chatManager, i * sizeof(void*), chatWindow) && chatWindow &&
        Read(chatWindow, kChatWindowInputOffset, input) && input == focused)
      return true;
  }
  return false;
}

bool GetFocusedInput(void* manager, void*& input) {
  input = nullptr;
  void* focused = nullptr;
  BYTE editable = 0;
  return Read(manager, kFocusWindowOffset, focused) && IsChatInput(manager, focused) &&
         Read(focused, kEditableOffset, editable) && editable != 0 &&
         Readable(focused, 0x314) && (input = focused) != nullptr;
}

bool GetText(void* edit, CxStrRep*& rep, const char*& bytes, uint32_t& length,
             uint32_t& encoding) {
  rep = nullptr;
  bytes = nullptr;
  length = 0;
  encoding = 0;
  if (!Read(edit, kInputTextOffset, rep) || !rep) return true;
  if (!Readable(rep, sizeof(CxStrRep)) || !Read(rep, 8, length) ||
      !Read(rep, 0x0c, encoding) || length > kMaxTextBytes || encoding > 1)
    return false;
  const size_t unitSize = encoding ? sizeof(wchar_t) : sizeof(char);
  if (length > (kMaxTextBytes / unitSize)) return false;
  bytes = reinterpret_cast<const char*>(rep) + kCxStrTextOffset;
  return Readable(bytes, (static_cast<size_t>(length) + 1) * unitSize);
}

bool GetTagCount(void* edit, int& count) {
  return Read(edit, kTagCountOffset, count) && count >= 0 && count <= 10;
}

bool MapToRawIndex(void* edit, int printable, bool isEnd, int rawLength, int& result) {
  int count = 0;
  if (!GetTagCount(edit, count)) return false;
  int adjustment = 0;
  for (int i = 0; i < count; ++i) {
    int printableStart = 0, printableEnd = 0, originalStart = 0, originalEnd = 0;
    if (!Read(edit, kTagPrintableStartsOffset + i * sizeof(int), printableStart) ||
        !Read(edit, kTagPrintableEndsOffset + i * sizeof(int), printableEnd) ||
        !Read(edit, kTagOriginalStartsOffset + i * sizeof(int), originalStart) ||
        !Read(edit, kTagOriginalEndsOffset + i * sizeof(int), originalEnd) ||
        printableStart < 0 || printableEnd < printableStart || originalStart < 0 ||
        originalEnd < originalStart || originalEnd > rawLength)
      return false;
    if (printable < printableStart) break;
    if (printable <= printableEnd) {
      if (printable == printableStart) result = originalStart;
      else if (printable == printableEnd || isEnd) result = originalEnd;
      else result = originalStart;
      return true;
    }
    adjustment = originalEnd - printableEnd;
  }
  if (printable < 0 || printable > rawLength || adjustment > rawLength - printable)
    return false;
  result = printable + adjustment;
  return true;
}

int PrintableLength(void* edit, int rawLength) {
  int count = 0;
  if (!GetTagCount(edit, count)) return -1;
  int removed = 0;
  for (int i = 0; i < count; ++i) {
    int printableStart = 0, printableEnd = 0, originalStart = 0, originalEnd = 0;
    if (!Read(edit, kTagPrintableStartsOffset + i * sizeof(int), printableStart) ||
        !Read(edit, kTagPrintableEndsOffset + i * sizeof(int), printableEnd) ||
        !Read(edit, kTagOriginalStartsOffset + i * sizeof(int), originalStart) ||
        !Read(edit, kTagOriginalEndsOffset + i * sizeof(int), originalEnd) ||
        printableEnd < printableStart || originalEnd < originalStart)
      return -1;
    removed += (originalEnd - originalStart) - (printableEnd - printableStart);
  }
  const int length = rawLength - removed;
  return length >= 0 && length <= static_cast<int>(kMaxTextBytes) ? length : -1;
}

std::wstring Utf8ToWide(std::string_view input) {
  if (input.empty()) return {};
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (required <= 0) return {};
  std::wstring result(static_cast<size_t>(required), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
          static_cast<int>(input.size()), result.data(), required))
    return {};
  return result;
}

std::string WideToUtf8(std::wstring_view input) {
  if (input.empty()) return {};
  const int required = WideCharToMultiByte(CP_UTF8, 0, input.data(),
      static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string result(static_cast<size_t>(required), '\0');
  if (!WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
          result.data(), required, nullptr, nullptr))
    return {};
  return result;
}

bool SetClipboard(std::string_view utf8) {
  if (utf8.size() > kMaxTextBytes || !OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  bool okay = false;
  auto wide = Utf8ToWide(utf8);
  if (!wide.empty() || utf8.empty()) {
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (data) {
      auto* text = static_cast<wchar_t*>(GlobalLock(data));
      if (text) {
        if (!wide.empty()) std::memcpy(text, wide.data(), wide.size() * sizeof(wchar_t));
        text[wide.size()] = L'\0';
        GlobalUnlock(data);
        if (SetClipboardData(CF_UNICODETEXT, data)) okay = true;
        else GlobalFree(data);
      } else GlobalFree(data);
    }
  }
  HGLOBAL ansiData = GlobalAlloc(GMEM_MOVEABLE, utf8.size() + 1);
  if (ansiData) {
    auto* text = static_cast<char*>(GlobalLock(ansiData));
    if (text) {
      if (!utf8.empty()) std::memcpy(text, utf8.data(), utf8.size());
      text[utf8.size()] = '\0';
      GlobalUnlock(ansiData);
      if (SetClipboardData(CF_TEXT, ansiData)) okay = true;
      else GlobalFree(ansiData);
    } else GlobalFree(ansiData);
  }
  CloseClipboard();
  return okay;
}

std::string ReadClipboard() {
  if (!OpenClipboard(nullptr)) return {};
  std::string result;
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const auto* wide = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
    if (wide) {
      const size_t maxUnits = (std::min<size_t>)(
          static_cast<size_t>(GlobalSize(data)) / sizeof(wchar_t), kMaxTextBytes);
      size_t length = 0;
      while (length < maxUnits && wide[length]) ++length;
      if (length == maxUnits && length != 0) result.clear();
      else result = WideToUtf8(std::wstring_view(wide, length));
      GlobalUnlock(data);
    }
  } else if (IsClipboardFormatAvailable(CF_TEXT)) {
    HANDLE data = GetClipboardData(CF_TEXT);
    const auto* text = data ? static_cast<const char*>(GlobalLock(data)) : nullptr;
    if (text) {
      const size_t maxBytes = (std::min<size_t>)(
          static_cast<size_t>(GlobalSize(data)), kMaxTextBytes);
      size_t length = 0;
      while (length < maxBytes && text[length]) ++length;
      if (length < maxBytes) {
        result.assign(text, length);
        if (!result.empty() && Utf8ToWide(result).empty()) {
          const int count = MultiByteToWideChar(CP_ACP, 0, result.data(),
              static_cast<int>(result.size()), nullptr, 0);
          if (count > 0) {
            std::wstring wide(static_cast<size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_ACP, 0, result.data(), static_cast<int>(result.size()),
                    wide.data(), count))
              result = WideToUtf8(wide);
          }
        }
      }
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
  result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
  return result;
}

bool CopySelection(void* edit, bool cut) {
  CxStrRep* rep = nullptr;
  const char* bytes = nullptr;
  uint32_t length = 0, encoding = 0;
  if (!GetText(edit, rep, bytes, length, encoding)) return false;
  int start = 0, end = 0;
  if (!Read(edit, kStartPositionOffset, start) || !Read(edit, kEndPositionOffset, end))
    return false;
  if (start > end) std::swap(start, end);
  if (start == end) return true;
  int rawStart = 0, rawEnd = 0;
  if (!MapToRawIndex(edit, start, false, static_cast<int>(length), rawStart) ||
      !MapToRawIndex(edit, end, true, static_cast<int>(length), rawEnd) ||
      rawStart < 0 || rawEnd < rawStart || rawEnd > static_cast<int>(length))
    return false;
  const size_t unit = encoding ? sizeof(wchar_t) : sizeof(char);
  const size_t byteStart = static_cast<size_t>(rawStart) * unit;
  const size_t byteLength = static_cast<size_t>(rawEnd - rawStart) * unit;
  if (byteLength > kMaxTextBytes || !Readable(bytes + byteStart, byteLength)) return false;
  std::string selected(bytes + byteStart, byteLength);
  if (encoding) {
    if (selected.size() % sizeof(wchar_t)) return false;
    std::wstring_view wide(reinterpret_cast<const wchar_t*>(selected.data()),
                           selected.size() / sizeof(wchar_t));
    selected = WideToUtf8(wide);
  }
  if (!SetClipboard(selected)) return false;
  if (cut) {
    NativeCxStr empty{};
    g_construct(&empty, "");
    g_replaceSelection(edit, empty, false);
  }
  return true;
}

bool Paste(void* edit) {
  CxStrRep* rep = nullptr;
  const char* bytes = nullptr;
  uint32_t length = 0, encoding = 0;
  if (!GetText(edit, rep, bytes, length, encoding)) return false;
  std::string text = ReadClipboard();
  if (text.empty()) return true;
  if (text.size() > kMaxTextBytes) return false;
  int start = 0, end = 0, maxBytes = -1;
  if (!Read(edit, kStartPositionOffset, start) || !Read(edit, kEndPositionOffset, end) ||
      !Read(edit, kMaxBytesOffset, maxBytes)) return false;
  if (start > end) std::swap(start, end);
  int rawStart = 0, rawEnd = 0;
  if (!MapToRawIndex(edit, start, false, static_cast<int>(length), rawStart) ||
      !MapToRawIndex(edit, end, true, static_cast<int>(length), rawEnd)) return false;
  int tags = 0;
  if (!GetTagCount(edit, tags)) return false;
  const auto links = static_cast<int>(std::count(text.begin(), text.end(), '\x12') / 2);
  if (tags + links > 10) return true;
  const size_t unit = encoding ? sizeof(wchar_t) : sizeof(char);
  const size_t replacedBytes = static_cast<size_t>(rawEnd - rawStart) * unit;
  if (maxBytes > 0 && length * unit - replacedBytes + text.size() > static_cast<size_t>(maxBytes))
    return true;
  NativeCxStr value{};
  g_construct(&value, text.c_str());
  g_replaceSelection(edit, value, false);
  return true;
}

bool MoveSelection(void* edit, uint32_t key, bool shift) {
  CxStrRep* rep = nullptr;
  const char* bytes = nullptr;
  uint32_t length = 0, encoding = 0;
  int start = 0, end = 0;
  if (!GetText(edit, rep, bytes, length, encoding) ||
      !Read(edit, kStartPositionOffset, start) || !Read(edit, kEndPositionOffset, end))
    return false;
  const int total = PrintableLength(edit, static_cast<int>(length));
  if (total < 0) return false;
  const bool caretAtEnd = [&] { BYTE value = 1; Read(edit, kCaretAtEndOffset, value); return value != 0; }();
  int caret = caretAtEnd ? end : start;
  int anchor = caretAtEnd ? start : end;
  if (start > end) std::swap(start, end);
  if (key == 0xc7) caret = 0;
  else if (key == 0xcf) caret = total;
  else if (!shift && start != end) caret = key == 0xcb ? start : end;
  else if (key == 0xcb) caret = (std::max)(0, caret - 1);
  else if (key == 0xcd) caret = (std::min)(total, caret + 1);
  else return false;
  if (!shift) anchor = caret;
  const int low = (std::min)(anchor, caret), high = (std::max)(anchor, caret);
  g_setSelection(edit, low, high);
  Write(edit, kAnchorAtStartOffset, static_cast<BYTE>(anchor <= caret));
  Write(edit, kCaretAtEndOffset, static_cast<BYTE>(caret >= anchor));
  return true;
}

bool HandleEditKey(void* manager, uint32_t key, bool down) {
  if (!down) return false;
  void* edit = nullptr;
  if (!GetFocusedInput(manager, edit)) return false;
  BYTE shift = 0, control = 0;
  if (!Read(manager, kKeyboardFlagsOffset, shift) ||
      !Read(manager, kKeyboardFlagsOffset + 1, control)) return false;
  if (control) {
    if (key == 0x2e) return CopySelection(edit, false); // C
    if (key == 0x2d) return CopySelection(edit, true);  // X
    if (key == 0x2f) return Paste(edit);                // V
    if (key == 0x1e) {                                  // A
      CxStrRep* rep = nullptr;
      const char* bytes = nullptr;
      uint32_t length = 0, encoding = 0;
      if (!GetText(edit, rep, bytes, length, encoding)) return false;
      const int total = PrintableLength(edit, static_cast<int>(length));
      if (total < 0) return false;
      g_setSelection(edit, 0, total);
      Write(edit, kAnchorAtStartOffset, static_cast<BYTE>(1));
      Write(edit, kCaretAtEndOffset, static_cast<BYTE>(1));
      return true;
    }
  }
  if (key == 0xcb || key == 0xcd || key == 0xc7 || key == 0xcf)
    return MoveSelection(edit, key, shift != 0);
  return false;
}

int __fastcall KeyboardHook(void* manager, void*, uint32_t key, bool down) {
  if (HandleEditKey(manager, key, down)) return 0;
  return g_originalKeyboard ? g_originalKeyboard(manager, key, down) : 0;
}

void* ResolveFunction(uintptr_t preferredVa, const BYTE* expected, size_t length) {
  auto address = ResolvePreferredVA(g_module, preferredVa, length);
  if (!address || std::memcmp(reinterpret_cast<const void*>(address), expected, length))
    return nullptr;
  return reinterpret_cast<void*>(address);
}

} // namespace

bool InitializeChatInput(HMODULE module, const ValidatedClient& validation) {
  if (!validation.IsValidFor(module)) return false;
  if (InterlockedCompareExchange(&g_installed, 0, 0))
    return g_module == module;

  g_module = module;
  g_validation = validation;
  constexpr BYTE keyboardBytes[] = {0x83, 0xec, 0x0c, 0x53, 0x55, 0x56, 0x8b, 0xf1};
  constexpr BYTE constructorBytes[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08};
  constexpr BYTE selectionBytes[] = {0x8b, 0x81, 0xa8, 0x01, 0x00, 0x00, 0x57};
  constexpr BYTE replaceBytes[] = {0x6a, 0xff, 0x68, 0x96, 0x3c, 0x9a, 0x00, 0x64};
  auto* keyboard = static_cast<BYTE*>(ResolveFunction(kHandleKeyboardMsg,
      keyboardBytes, sizeof(keyboardBytes)));
  auto* constructor = ResolveFunction(kCxStrConstruct, constructorBytes,
      sizeof(constructorBytes));
  auto* selection = ResolveFunction(kSetSelection, selectionBytes,
      sizeof(selectionBytes));
  auto* replace = ResolveFunction(kReplaceSelection, replaceBytes,
      sizeof(replaceBytes));
  if (!keyboard || !constructor || !selection || !replace) {
    Trace("ERROR: RoF2 chat input signatures did not match");
    return false;
  }
  g_construct = reinterpret_cast<CxStrConstructorFn>(constructor);
  g_setSelection = reinterpret_cast<SetSelectionFn>(selection);
  g_replaceSelection = reinterpret_cast<ReplaceSelectionFn>(replace);
  if (!InstallDetour(module, validation, kHandleKeyboardMsg, keyboardBytes,
          sizeof(keyboardBytes), reinterpret_cast<void*>(&KeyboardHook),
          reinterpret_cast<void**>(&g_originalKeyboard))) {
    Trace("ERROR: CXWndManager keyboard hook could not be installed");
    return false;
  }
  InterlockedExchange(&g_installed, 1);
  Trace("Zeal-style chat copy/paste and selection keys initialized");
  return true;
}

} // namespace gusto::client::rof2
