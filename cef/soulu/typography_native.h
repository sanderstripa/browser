#pragma once
#include <windows.h>
#include <string>

namespace soulu {
// Same result IDs/default-button semantics as MessageBox, with private Onest.
int TypographyMessageBox(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags,
                         const void* tag=nullptr);
bool TypographyPrompt(HWND owner, const std::wstring& text, const std::wstring& initial,
                      std::wstring& result, const void* tag=nullptr);
void TypographyCancelDialogs(const void* tag);
int TypographyTrackPopupMenu(HMENU menu,UINT flags,int x,int y,int reserved,HWND owner,const RECT* bounds);
bool TypographyMenuMessage(UINT message,LPARAM parameter);
}
