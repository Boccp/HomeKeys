// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include <windows.h>
class CapsGuard {
    HHOOK hook=nullptr;
    inline static bool swallowed=false;
    static LRESULT CALLBACK callback(int code,WPARAM w,LPARAM l) {
        if(code==HC_ACTION) {
            const auto* k=reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
            if(k->vkCode==VK_CAPITAL) {
                DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
                const bool active=pid==GetCurrentProcessId();
                const bool up=w==WM_KEYUP || w==WM_SYSKEYUP;
                if(active || (up && swallowed)) { swallowed=!up; return 1; }
            }
        }
        return CallNextHookEx(nullptr,code,w,l);
    }
public:
    bool install() { if(!hook)hook=SetWindowsHookExW(WH_KEYBOARD_LL,callback,GetModuleHandleW(nullptr),0); return hook!=nullptr; }
    void uninstall() { if(hook){UnhookWindowsHookEx(hook);hook=nullptr;} swallowed=false; }
    ~CapsGuard() { uninstall(); }
};
