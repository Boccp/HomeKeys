// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include "Core.h"
#include "Travel.h"
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>

namespace homekeys {
struct AnalogEvent { Event event; unsigned generation; };
struct KeyCalibration { int key = -1; TravelCalibration range{}; };
class AnalogInput {
    friend struct AnalogInputTest;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::mutex mappingMutex;
    std::array<KeyCalibration,256> mapping{};
    std::array<std::optional<TravelTracker>,256> trackers;
    std::array<int,256> sounding{};
    std::array<int,256> monitorCells{};
    std::array<std::optional<TravelTracker>,256> monitors;
    void sendEvent(Event e, unsigned gen) {
        if (!audio.push({e,gen})) { ++overflows; mute(); }
    }
    static HANDLE openDevice() {
        GUID guid; HidD_GetHidGuid(&guid);
        auto set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
        if(set==INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
        HANDLE result=INVALID_HANDLE_VALUE;
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize=sizeof(iface);
        for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&iface);++i) {
            DWORD bytes=0; SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);
            if(!bytes) continue;
            std::vector<unsigned char> buffer(bytes);
            auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize=sizeof(*detail);
            if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,nullptr)) continue;
            HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            if(h==INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES attr{}; attr.Size=sizeof(attr);
            HIDP_CAPS caps{}; PHIDP_PREPARSED_DATA pp=nullptr;
            const bool matches=HidD_GetAttributes(h,&attr) && attr.VendorID==0x0416 && attr.ProductID==0x7372;
            if(matches && HidD_GetPreparsedData(h,&pp)) { HidP_GetCaps(pp,&caps); HidD_FreePreparsedData(pp); }
            CloseHandle(h);
            if(!matches || caps.UsagePage!=0xff1b || caps.Usage!=0x91 || caps.InputReportByteLength!=64 || caps.OutputReportByteLength!=64) continue;
            result=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(result!=INVALID_HANDLE_VALUE) break;
        }
        SetupDiDestroyDeviceInfoList(set); return result;
    }
    static bool writePacket(HANDLE h, const std::array<unsigned char,64>& packet) {
        OVERLAPPED op{}; op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!op.hEvent) return false;
        DWORD bytes=0; BOOL ok=WriteFile(h,packet.data(),64,&bytes,&op);
        if(!ok && GetLastError()==ERROR_IO_PENDING) {
            if(WaitForSingleObject(op.hEvent,500)==WAIT_OBJECT_0) ok=GetOverlappedResult(h,&op,&bytes,FALSE);
            else { CancelIoEx(h,&op); GetOverlappedResult(h,&op,&bytes,TRUE); }
        }
        CloseHandle(op.hEvent); return ok && bytes==64;
    }
    void process(TravelFrame f, std::uint64_t time, unsigned& seenGeneration) {
        ++reports; lastDepth=f.depth; lastCell=int(f.row)*256+f.column;
        // Monitoring must work before physical-key-to-note calibration exists.
        for(int i=0;i<256;++i) {
            if(monitors[i] && monitorCells[i]!=lastCell.load()) continue;
            if(!monitors[i]) { monitorCells[i]=lastCell.load(); monitors[i].emplace(TravelCalibration{f.row,f.column,0,40}); }
            if(auto e=monitors[i]->observe(f,time); e && e->noteOn) {
                monitorVelocity=int(std::clamp(e->estimatedVelocity.value_or(.5f)*sensitivity.load(),.03f,1.f)*127+.5f);
                monitorEstimated=e->estimatedVelocity.has_value();
            }
            break;
        }
        if(!display.push(f)) ++displayOverflows;
        std::lock_guard lock(mappingMutex);
        const auto gen=generation.load();
        if(gen!=seenGeneration) {
            for(auto& t:trackers) if(t) t->reset();
            sounding.fill(-1); seenGeneration=gen;
        }
        for(int key=0;key<256;++key) {
            const auto& m=mapping[key];
            if(m.key<0 || m.range.row!=f.row || m.range.column!=f.column) continue;
            if(!trackers[key]) trackers[key].emplace(m.range);
            auto e=trackers[key]->observe(f,time);
            if(!e) return;
            held[key]=e->travel>.10f;
            if(!enabled.load()) { trackers[key]->reset(); sounding[key]=-1; return; }
            if(e->noteOn) {
                const int note=shiftMode.load() && (GetAsyncKeyState(VK_SHIFT)&0x8000) ? shiftedNotes[key].load() : notes[key].load();
                if(note<0) return;
                const float value=e->estimatedVelocity.value_or(.5f);
                if(!e->estimatedVelocity) ++fallbacks;
                const float shaped=std::clamp(value*sensitivity.load(),.03f,1.0f);
                lastVelocity=int(shaped*127+.5f); lastEstimated=e->estimatedVelocity.has_value();
                sounding[key]=note;
                sendEvent({EventType::on,key+256,note,shaped},gen);
            }
            if(e->noteOff && sounding[key]>=0) {
                sendEvent({EventType::off,key+256,sounding[key],0},gen); sounding[key]=-1;
            }
            return;
        }
    }
    void run() {
        // Cross-process guard also covers alternate executable filenames.
        HANDLE owner=CreateMutexW(nullptr,TRUE,L"Local\\HomeKeys.Analog.0416.7372");
        if(!owner || GetLastError()==ERROR_ALREADY_EXISTS) {
            if(owner) CloseHandle(owner); status=7; return;
        }
        HANDLE h=openDevice();
        if(h==INVALID_HANDLE_VALUE) { status=4; ReleaseMutex(owner); CloseHandle(owner); return; }
        // Experimental KeyAxis stream command; contains volatile vendor parameters.
        // Only sent on explicit Start. No flash/reset/calibration command is sent.
        std::array<unsigned char,64> arm{1,0x21,0,0,0,0x18,2,0x3e,0x26,0x3e,0x1e,0x1e,0x1e,0x3e,0x1e,0x1e,0x3e,0x1e,0x3e,0x2e,0x10,0x2e,0x30,0x3e};
        std::array<unsigned char,64> disarm{1,0x21,0,0,0,0x18,3};
        const bool armed=writePacket(h,arm);
        if(!armed) status=5;
        else {
            status=2; unsigned seenGeneration=generation.load();
            const auto deadline=GetTickCount64()+60000;
            while(!stopping.load()) {
                OVERLAPPED op{}; op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                if(!op.hEvent) { status=5; break; }
                std::array<unsigned char,64> packet{}; DWORD bytes=0;
                BOOL ok=ReadFile(h,packet.data(),64,&bytes,&op);
                bool pending=!ok && GetLastError()==ERROR_IO_PENDING;
                if(pending) {
                    while(!stopping.load() && (reports.load()>0 || GetTickCount64()<deadline)) {
                        const auto wait=WaitForSingleObject(op.hEvent,20);
                        if(wait==WAIT_OBJECT_0) { ok=GetOverlappedResult(h,&op,&bytes,FALSE); pending=false; break; }
                        if(wait==WAIT_FAILED) break;
                    }
                    if(pending) { CancelIoEx(h,&op); GetOverlappedResult(h,&op,&bytes,TRUE); }
                }
                CloseHandle(op.hEvent);
                if(stopping.load()) break;
                if(!reports.load() && GetTickCount64()>=deadline) { status=6; break; }
                if(!ok) { status=5; break; }
                if(auto frame=parseTravel({packet.data(),bytes})) {
                    status=3;
                    const auto now=std::chrono::steady_clock::now().time_since_epoch();
                    process(*frame,std::chrono::duration_cast<std::chrono::microseconds>(now).count(),seenGeneration);
                }
            }
        }
        // Also attempt stop if ARM completed ambiguously.
        stopCommandOk=writePacket(h,disarm);
        CloseHandle(h); mute();
        ReleaseMutex(owner); CloseHandle(owner);
        if(stopping.load()) status=0;
    }
public:
    SpscQueue<AnalogEvent,1024> audio;
    SpscQueue<TravelFrame,4096> display;
    std::array<std::atomic<int>,256> notes{};
    std::array<std::atomic<int>,256> shiftedNotes{};
    std::atomic<bool> shiftMode{false}, monitorEstimated{false};
    std::atomic<int> monitorVelocity{0};
    std::array<std::atomic<bool>,256> held{};
    std::atomic<bool> enabled{false}, panic{false}, lastEstimated{false}, stopCommandOk{true};
    std::atomic<unsigned> generation{0}, reports{0}, overflows{0}, displayOverflows{0}, fallbacks{0};
    std::atomic<int> status{0}, lastDepth{0}, lastCell{-1}, lastVelocity{0};
    std::atomic<float> sensitivity{1};
    AnalogInput() { for(auto& n:notes)n=-1; for(auto& n:shiftedNotes)n=-1; sounding.fill(-1); }
    ~AnalogInput() { stop(); }
    void mute() { ++generation; panic=true; for(auto& h:held)h=false; }
    void start() {
        stop(); reports=0; stopping=false; status=1; stopCommandOk=true;
        for(auto& monitor:monitors) monitor.reset();
        monitorVelocity=0; monitorEstimated=false;
        lastDepth=0; lastCell=-1; lastVelocity=0; lastEstimated=false;
        TravelFrame f; while(display.pop(f)) {}
        worker=std::thread([this]{run();});
    }
    void stop() { enabled=false; stopping=true; if(worker.joinable())worker.join(); mute(); }
    bool connected() const { return status==2 || status==3; }
    bool mapped(int key) { std::lock_guard lock(mappingMutex); return mapping[key].key>=0; }
    bool bind(int key, TravelCalibration c) {
        if(key<0 || key>=256 || !c.valid()) return false;
        std::lock_guard lock(mappingMutex);
        for(int k=0;k<256;++k) if(k!=key && mapping[k].key>=0 && mapping[k].range.row==c.row && mapping[k].range.column==c.column) return false;
        mapping[key]={key,c}; trackers[key].emplace(c); return true;
    }
    std::array<KeyCalibration,256> snapshot() { std::lock_guard lock(mappingMutex); return mapping; }
};
}
