// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#include <juce_audio_utils/juce_audio_utils.h>
#include "Core.h"
#include "AnalogInput.h"
#include "AutoKeyLearner.h"
#include "CapsGuard.h"
#include "Lesson.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif

class MainComponent final : public juce::AudioAppComponent, private juce::Timer {
    CapsGuard capsGuard;
    bool capsProtected=false,curtain=false;
    homekeys::LessonPlayer lesson;
    std::thread importer;
    std::atomic<bool> cancelImport{false}, importing{false};
    std::mutex importMutex;
    std::unique_ptr<homekeys::LessonData> pendingLesson;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton importButton{juce::String::fromUTF8("导入 MIDI / 音频")}, playLesson{juce::String::fromUTF8("自动演奏 / 重播")}, pauseLesson{juce::String::fromUTF8("暂停 / 继续")}, curtainButton{juce::String::fromUTF8("演奏幕布")};
    juce::TextButton about{juce::String::fromUTF8("关于 / 许可证")};
    juce::Slider lessonSpeed;
    juce::ComboBox library;
    juce::TextButton loadLibrary{juce::String::fromUTF8("加载所选")}, attachLibrary{juce::String::fromUTF8("关联本地文件")};
    juce::File libraryFile() const {return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("HomeKeys-library.xml");}
    juce::File songFile(int id) const {
        if(id!=1)return {};
        auto xml=juce::XmlDocument::parse(libraryFile());
        const auto custom=xml?juce::File(xml->getStringAttribute("song"+juce::String(id))):juce::File();
        if(custom.existsAsFile())return custom;
        constexpr const char* files[]={"scale.mid"};
        return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("lessons").getChildFile(files[id-1]);
    }
    bool associateSong(int id,const juce::File& file) {
        auto xml=juce::XmlDocument::parse(libraryFile());if(!xml)xml=std::make_unique<juce::XmlElement>("HomeKeysLibrary");
        xml->setAttribute("song"+juce::String(id),file.getFullPathName());return xml->writeTo(libraryFile());
    }
    void queueLesson(const juce::File& file) {
        stopLesson();cancelImport=true;
        if(importer.joinable())importer.join();
        {std::lock_guard lock(importMutex);pendingLesson.reset();}
        cancelImport=false;importing=true;
        lessonStatus=juce::String::fromUTF8("后台读取 / 分析中，可继续演奏。");
        importer=std::thread([this,file]{
            std::unique_ptr<homekeys::LessonData> result;
            try {result=homekeys::loadLesson(file,cancelImport);}catch(...){result=std::make_unique<homekeys::LessonData>();result->error=juce::String::fromUTF8("导入失败：文件损坏或资源不足。");}
            if(!cancelImport.load()){std::lock_guard lock(importMutex);pendingLesson=std::move(result);}
            importing=false;
        });
    }
    juce::String lessonStatus=juce::String::fromUTF8("MIDI：音符幕布与自动演奏；音频：原声跟听 + 实验性单旋律转谱。");
    void stopLesson() { const juce::ScopedLock lock(deviceManager.getAudioCallbackLock()); lesson.stop(); }
    void importFile(int songId=0) {
        chooser=std::make_unique<juce::FileChooser>(juce::String::fromUTF8("选择本地 MIDI 或音频"),juce::File(),"*.mid;*.midi;*.wav;*.mp3;*.flac;*.aiff");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<MainComponent>(this),songId](const juce::FileChooser& fc){
            if(!safe)return;const auto file=fc.getResult();if(!file.existsAsFile())return;
            const bool saved=songId<=0 || safe->associateSong(songId,file);
            safe->queueLesson(file);
            if(!saved)safe->lessonStatus=juce::String::fromUTF8("正在导入，但曲目关联保存失败。");
        });
    }
    void acceptImport() {
        std::unique_ptr<homekeys::LessonData> ready;
        {std::lock_guard lock(importMutex);ready=std::move(pendingLesson);}
        if(!ready)return;
        if(ready->error.isNotEmpty()){lessonStatus=ready->error;return;}
        lessonStatus=ready->name+juce::String::fromUTF8(ready->audioFile?" | 原声跟听；幕布为实验性单旋律估计，非准确钢琴谱。":" | MIDI 音符，支持变速；当前使用内置音色。");
        if(ready->notes.empty())lessonStatus+=juce::String::fromUTF8(" 未识别到可显示音符。");
        {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());lesson.stop();lesson.data=std::move(ready);}
        lessonSpeed.setEnabled(!lesson.data->audioFile);curtain=true;repaint();
    }
    homekeys::Engine engine;
    homekeys::AnalogInput analog;
    juce::TextButton analogStart{juce::String::fromUTF8("开启磁轴试验")}, analogStop{juce::String::fromUTF8("停止检测")}, calibrate{juce::String::fromUTF8("校准选中键")};
    juce::TextButton calibrateAll{juce::String::fromUTF8("全部校准")}, cancelCalibration{juce::String::fromUTF8("结束校准")};
    homekeys::AutoKeyLearner autoLearner;
    std::array<bool,256> selectedKeys{};
    std::vector<int> calibrationBatch;
    int batchPosition=0;
    bool batchSaveFailed=false;
    unsigned autoDrops=0;
    double autoLearnDeadline=0;
    juce::Slider sensitivity;
    juce::String analogHint = juce::String::fromUTF8("实验协议：开启可能影响临时触发参数；所有键默认可演奏；单独按键并松开可自动识别磁轴。");
    const juce::String calibrationKeys{"1234567890-=QWERTYUIOP[]ASDFGHJKL;ZXCVBNM,./"};
    int selectedKey='Z';
    int calibrationIndex=-1, candidateCell=-1, candidatePeak=0;
    unsigned calibrationDrops=0;
    double calibrationDeadline=0;
    juce::File calibrationFile() const {
        return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("HomeKeys-travel-v1.xml");
    }
    void syncNotes() {
        for(auto& n:analog.notes) n=-1;
        for(auto& n:analog.shiftedNotes) n=-1;
        analog.shiftMode=layout.getSelectedId()==3;
        eachBinding([this](homekeys::Binding b){ analog.notes[b.key]=b.note+octave*12; analog.shiftedNotes[b.key]=homekeys::blackAbove(b.note)<0 ? -1 : homekeys::blackAbove(b.note)+octave*12; });
    }
    bool saveCalibration() {
        juce::XmlElement root("HomeKeysTravelV1");
        for(auto m:analog.snapshot()) if(m.key>=0) {
            auto* item=root.createNewChildElement("key");
            item->setAttribute("code",m.key); item->setAttribute("row",int(m.range.row));
            item->setAttribute("column",int(m.range.column)); item->setAttribute("rest",int(m.range.rest)); item->setAttribute("peak",int(m.range.peak));
        }
        return root.writeTo(calibrationFile());
    }
    void loadCalibration() {
        auto xml=juce::XmlDocument::parse(calibrationFile());
        if(!xml || !xml->hasTagName("HomeKeysTravelV1")) return;
        for(auto* e=xml->getFirstChildElement();e;e=e->getNextElement()) {
            int k=e->getIntAttribute("code",-1),r=e->getIntAttribute("row",-1),c=e->getIntAttribute("column",-1),a=e->getIntAttribute("rest",-1),b=e->getIntAttribute("peak",-1);
            if(k>=0 && k<256 && r>=0 && r<256 && c>=0 && c<256 && a>=0 && a<=5 && b>=20 && b<=40)
                analog.bind(k,{(std::uint8_t)r,(std::uint8_t)c,(std::uint8_t)a,(std::uint8_t)b});
        }
    }
    void beginCalibration(bool all) {
        if(!analog.connected()) { analogHint=juce::String::fromUTF8("请先开启磁轴试验。"); return; }
        calibrationBatch.clear(); batchSaveFailed=false;
        eachBinding([&](homekeys::Binding b){if(all || selectedKeys[b.key]) calibrationBatch.push_back(b.key);});
        if(calibrationBatch.empty()) { analogHint=juce::String::fromUTF8("先点击键帽选中；Ctrl + 点击可多选。"); return; }
        silence(); analog.enabled=false; autoLearner.reset(); batchPosition=0;
        calibrationIndex=calibrationKeys.indexOfChar((juce::juce_wchar)calibrationBatch[0]);
        homekeys::TravelFrame f; while(analog.display.pop(f)) {}
        calibrationPrompt(); grabKeyboardFocus();
    }
    void calibrationPrompt() {
        candidateCell=-1; candidatePeak=0;
        calibrationDrops=analog.displayOverflows.load();
        calibrationDeadline=juce::Time::getMillisecondCounterHiRes()+30000;
        analogHint=juce::String::fromUTF8("批量校准 ")+juce::String(batchPosition+1)+"/"+juce::String((int)calibrationBatch.size())+"  "+juce::String::charToString(calibrationKeys[calibrationIndex])+juce::String::fromUTF8("  请单独慢慢按到底，再完全松开（停止检测可退出）。");
    }
    void updateAnalog() {
        if(calibrationIndex>=0 && (!analog.connected() || juce::Time::getMillisecondCounterHiRes()>calibrationDeadline)) {
            calibrationIndex=-1; analogHint=juce::String::fromUTF8("校准已停止：设备断开或单键等待超过 30 秒；已完成的键已保存。");
        }
        homekeys::TravelFrame f;
        for(int n=0;n<4096 && analog.display.pop(f);++n) {
            if(calibrationIndex<0) {
                if(!hasKeyboardFocus(true) || !analog.connected()) { autoLearner.reset(); continue; }
                if(autoDrops!=analog.displayOverflows.load()) { autoDrops=analog.displayOverflows.load(); autoLearner.reset(); continue; }
                const auto now=juce::Time::getMillisecondCounterHiRes();
                if(now>autoLearnDeadline) autoLearner.reset();
                int physical=-1;
                eachBinding([&](homekeys::Binding b){ if(keyDown(b.key)) physical=physical==-1?b.key:-2; });
                if(physical>=0) autoLearnDeadline=now+2000;
                if(auto learned=autoLearner.observe(f,physical)) {
                    if(!analog.mapped(learned->key) && analog.bind(learned->key,learned->range)) {
                        const bool saved=saveCalibration();
                        analogHint=juce::String::charToString((juce::juce_wchar)learned->key)+juce::String::fromUTF8(" 已自动识别，下次击键使用磁轴力度；批量校准可进一步精调。");
                        if(!saved) analogHint+=juce::String::fromUTF8("（保存失败）");
                    }
                }
                continue;
            }
            if(analog.displayOverflows.load()!=calibrationDrops) { calibrationPrompt(); continue; }
            const int cell=int(f.row)*256+f.column;
            if(candidateCell<0 && f.depth>=10) candidateCell=cell;
            if(candidateCell<0) continue;
            if(cell!=candidateCell) {
                if(f.depth>5) { calibrationPrompt(); analogHint=juce::String::fromUTF8("检测到多个键，请全部松开，再单独按当前提示键。"); }
                continue;
            }
            candidatePeak=std::max(candidatePeak,int(f.depth));
            if(f.depth<=3 && candidatePeak>=20) {
                const int key=int(calibrationKeys[calibrationIndex]);
                if(!analog.bind(key,{f.row,f.column,f.depth,(std::uint8_t)candidatePeak})) {
                    calibrationPrompt(); analogHint=juce::String::fromUTF8("该物理键已分配，请按正确提示键；可停止检测退出校准。"); continue;
                }
                const bool saved=saveCalibration();
                batchSaveFailed=batchSaveFailed || !saved;
                calibrationIndex=-1;
                analogHint=juce::String::charToString((juce::juce_wchar)key)+juce::String::fromUTF8(" 校准完成，立即试弹轻重力度；点击其他键帽再校准。");
                if(!saved) analogHint+=juce::String::fromUTF8("（本次可用，但保存失败）");
                analog.mute();
                if(++batchPosition<(int)calibrationBatch.size()) {
                    calibrationIndex=calibrationKeys.indexOfChar((juce::juce_wchar)calibrationBatch[batchPosition]);
                    calibrationPrompt();
                } else analogHint=juce::String::fromUTF8(batchSaveFailed?"本批校准可用，但部分保存失败。":"本批校准完成，可直接演奏。");
            }
        }
        analog.enabled=analog.connected() && calibrationIndex<0 && hasKeyboardFocus(true) && !audioSuspended;
    }

    homekeys::InputState input;
    homekeys::SpscQueue<homekeys::Event, 1024> events;
    std::atomic<bool> emergency{false};
    std::atomic<int> voices{0}, dropped{0};
    std::atomic<int> audibleSource{0}, audibleVelocity{0}, audibleKey{0};
    std::atomic<float> audiblePeak{0};
    float callbackPeak=0; // audio thread only
    void applyAudioEvent(homekeys::Event e, int source) noexcept {
        engine.event(e);
        if(e.type==homekeys::EventType::on) {
            audibleSource=source; audibleVelocity=int(e.value*127+.5f);
            audibleKey=source==2?e.id-256:e.id;
            callbackPeak=0;
        }
    }
    juce::ComboBox layout;
    juce::Slider velocity;
    juce::TextButton down{juce::String::fromUTF8("降低八度")}, up{juce::String::fromUTF8("升高八度")}, stop{juce::String::fromUTF8("静音 / Esc")}, settings{juce::String::fromUTF8("音频设置")};
    int octave = 0;
    bool pedal = false;
    bool audioSuspended = false;
    juce::String deviceError;

    static bool keyDown(int key) {
#if JUCE_WINDOWS
        int vk = key;
        if (key == ';') vk = VK_OEM_1;
        if (key == ',') vk = VK_OEM_COMMA;
        if (key == '.') vk = VK_OEM_PERIOD;
        if (key == '[') vk = VK_OEM_4;
        if (key == ']') vk = VK_OEM_6;
        if (key == '=') vk = VK_OEM_PLUS;
        if (key == '-') vk = VK_OEM_MINUS;
        if (key == '/') vk = VK_OEM_2;
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        return juce::KeyPress::isKeyCurrentlyDown(key);
#endif
    }
    template<class Fn> void eachBinding(Fn fn) const {
        if (layout.getSelectedId() == 4) for (auto b : homekeys::pianoLayout) fn(b);
        else if (layout.getSelectedId() == 3) for (auto b : homekeys::maxLayout) fn(b);
        else if (layout.getSelectedId() == 2) for (auto b : homekeys::classicLayout) fn(b);
        else for (auto b : homekeys::homeLayout) fn(b);
    }
    void emit(homekeys::Event event) {
        if (!events.push(event)) { ++dropped; emergency.store(true, std::memory_order_release); input.reset(); }
    }
    void silence() {
        emergency.store(true, std::memory_order_release);
        input.reset();
        analog.mute();
        pedal = false;
        repaint();
    }
    void scanKeys() {
        if (!hasKeyboardFocus(true)) return;
        if (keyDown(juce::KeyPress::escapeKey)) { stopLesson(); silence(); return; }
        if(calibrationIndex>=0) return;
        const bool nextPedal = keyDown(' ');
        if (pedal != nextPedal) {
            pedal = nextPedal;
            emit({homekeys::EventType::pedal, 0, 0, pedal ? 1.0f : 0.0f});
        }
        eachBinding([this](homekeys::Binding b) {
            auto sink = [this](homekeys::Event event) { emit(event); };
            if (analog.connected() && analog.mapped(b.key)) { if(!keyDown(b.key)) input.up(b.key,sink); return; }
            const int note=layout.getSelectedId()==3 && keyDown(VK_SHIFT) ? homekeys::blackAbove(b.note) : b.note;
            if (keyDown(b.key)) { if(note>=0) input.down(b.key, note + octave * 12, static_cast<float>(velocity.getValue()), sink); }
            else input.up(b.key, sink);
        });
    }
    void timerCallback() override {
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if(foregroundPid==GetCurrentProcessId() && isShowing())capsProtected=capsGuard.install();
        else {capsGuard.uninstall();capsProtected=false;}
        acceptImport();
        const auto* peer = getPeer();
        const bool hidden = !isShowing() || (peer != nullptr && peer->isMinimised());
        if (hidden) {
            if (!audioSuspended) { stopLesson(); analog.stop(); calibrationIndex=-1; silence(); deviceManager.closeAudioDevice(); audioSuspended = true; startTimerHz(4); }
            return;
        }
        if (audioSuspended) { deviceManager.restartLastAudioDevice(); audioSuspended = false; startTimerHz(60); }

        updateAnalog();
        if (!hasKeyboardFocus(true)) {
            bool held = pedal;
            eachBinding([&](homekeys::Binding b) { held = held || input.held(b.key); });
            if (held) silence();
        } else scanKeys();
        repaint();
    }
    void showSettings() {
        stopLesson();
        silence();
        juce::DialogWindow::LaunchOptions options;
        auto* panel = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 0, 2, 2, false, false, true, false);
        panel->setSize(560, 430);
        options.content.setOwned(panel);
        options.dialogTitle = juce::String::fromUTF8("音频设备与缓冲");
        options.dialogBackgroundColour = juce::Colour(0xff171d29);
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }
public:
    MainComponent() {
        setSize(1080, 960);
        capsProtected=false;
        setWantsKeyboardFocus(true);
        layout.addItem(juce::String::fromUTF8("完整四度布局（参考 Push）"), 1);
        layout.addItem(juce::String::fromUTF8("连续钢琴布局"), 2);
        layout.addItem(juce::String::fromUTF8("五八度钢琴（Shift 黑键）"), 3);
        layout.addItem(juce::String::fromUTF8("双层钢琴（常用电脑键位）"), 4);
        layout.setSelectedId(4, juce::dontSendNotification);
        layout.onChange = [this] { silence(); syncNotes(); grabKeyboardFocus(); };
        velocity.setRange(0.1, 1, 0.05);
        velocity.setValue(0.7);
        velocity.setSliderStyle(juce::Slider::LinearHorizontal);
        velocity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);
        down.onClick = [this] { silence(); octave = std::max(-2, octave-1); syncNotes(); grabKeyboardFocus(); };
        up.onClick = [this] { silence(); octave = std::min(2, octave+1); syncNotes(); grabKeyboardFocus(); };
        stop.onClick = [this] { stopLesson(); silence(); grabKeyboardFocus(); };
        settings.onClick = [this] { showSettings(); };
        analogStart.onClick=[this] { silence(); calibrationIndex=-1; analog.start(); analogHint=juce::String::fromUTF8("默认可演奏；先单独慢按到底并松开，自动识别后即可使用磁轴力度。"); grabKeyboardFocus(); };
        analogStop.onClick=[this] { calibrationIndex=-1; analog.stop(); silence(); analogHint=juce::String::fromUTF8("已停止行程检测，恢复普通按键演奏。"); grabKeyboardFocus(); };
        calibrate.onClick=[this] { beginCalibration(false); };
        calibrateAll.onClick=[this] { beginCalibration(true); };
        cancelCalibration.onClick=[this] { calibrationIndex=-1; autoLearner.reset(); silence(); analogHint=juce::String::fromUTF8("已结束校准，完成项已保存，其他键保留默认值。"); grabKeyboardFocus(); };
        selectedKeys['Z']=true;
        sensitivity.setRange(.25,2,.05); sensitivity.setValue(1);
        sensitivity.setSliderStyle(juce::Slider::LinearHorizontal);
        sensitivity.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);
        sensitivity.onValueChange=[this]{analog.sensitivity=float(sensitivity.getValue());};
        library.addItem(juce::String::fromUTF8("入门音阶练习"),1);
        library.setSelectedId(1,juce::dontSendNotification);
        loadLibrary.onClick=[this]{const auto file=songFile(library.getSelectedId());if(file.existsAsFile())queueLesson(file);else lessonStatus=juce::String::fromUTF8("找不到学习文件：请完整解压安装包并保留 lessons 文件夹，或点击「关联本地文件」。");};
        attachLibrary.onClick=[this]{importFile(library.getSelectedId());};
        importButton.onClick=[this]{importFile();};
        playLesson.onClick=[this]{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());lesson.start();curtain=true;grabKeyboardFocus();};
        pauseLesson.onClick=[this]{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(lesson.playing)lesson.pause();else lesson.resume();grabKeyboardFocus();};
        curtainButton.onClick=[this]{curtain=!curtain;grabKeyboardFocus();repaint();};
        lessonSpeed.setRange(.25,1.5,.05);lessonSpeed.setValue(1);
        lessonSpeed.setSliderStyle(juce::Slider::LinearHorizontal);lessonSpeed.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);
        lessonSpeed.onValueChange=[this]{lesson.speed=lessonSpeed.getValue();};
        about.onClick=[] { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("关于 HomeKeys"),
            juce::String::fromUTF8("HomeKeys 0.9.0\nCopyright (C) 2026 Boccp\n\n本程序按 GNU AGPL 第 3 版发布，允许按协议修改和再分发，不提供任何担保。\n完整条款见随包 LICENSE；第三方声明见 THIRD_PARTY_NOTICES.md 和 licenses 目录。\n\n源码：https://github.com/Boccp/HomeKeys\n协议：https://www.gnu.org/licenses/agpl-3.0.html")); };
        loadCalibration(); syncNotes();
        for (auto* component : std::initializer_list<juce::Component*>{&about, &layout, &velocity, &down, &up, &stop, &settings, &analogStart, &analogStop, &calibrate, &calibrateAll, &cancelCalibration, &sensitivity, &importButton, &playLesson, &pauseLesson, &curtainButton, &lessonSpeed, &library, &loadLibrary, &attachLibrary}) {
            addAndMakeVisible(component);
            component->setWantsKeyboardFocus(false);
        }
        setAudioChannels(0, 2);
        if (deviceManager.getCurrentAudioDevice() == nullptr) deviceError = juce::String::fromUTF8("未检测到音频设备，请打开音频设置。");
        startTimerHz(60); // UI/missed-release fallback; key events also dispatch immediately.
    }
    ~MainComponent() override { stopTimer(); chooser.reset(); cancelImport=true; if(importer.joinable())importer.join(); analog.stop(); shutdownAudio(); }
    void prepareToPlay(int, double rate) override { engine.prepare(rate); lesson.prepare(rate); }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& block) override {
        juce::ScopedNoDenormals noDenormals;
        block.clearActiveBufferRegion();
        homekeys::Event event;
        const bool reset = emergency.exchange(false, std::memory_order_acq_rel) | analog.panic.exchange(false);
        // Bound callback work, including recovery when the producer overflows.
        for (int n=0; n<1024 && events.pop(event); ++n) if (!reset) applyAudioEvent(event,1);
        homekeys::AnalogEvent ae;
        for(int n=0;n<1024 && analog.audio.pop(ae);++n)
            if(!reset && ae.generation==analog.generation.load() && analog.enabled.load()) applyAudioEvent(ae.event,2);
        if (reset) { engine.panic(); callbackPeak=0; audibleSource=0; }
        std::array<float*, 2> outputs{};
        const int channels = std::min(2, block.buffer->getNumChannels());
        for (int c=0; c<channels; ++c) outputs[c] = block.buffer->getWritePointer(c, block.startSample);
        engine.render(outputs.data(), channels, block.numSamples);
        lesson.render(outputs.data(),channels,block.numSamples);
        for(int c=0;c<channels;++c)for(int i=0;i<block.numSamples;++i)outputs[c][i]=std::clamp(outputs[c][i],-.98f,.98f);
        if(channels>0) for(int i=0;i<block.numSamples;++i) callbackPeak=std::max(callbackPeak,std::abs(outputs[0][i]));
        audiblePeak.store(callbackPeak,std::memory_order_relaxed);
        voices.store(engine.voiceCount(), std::memory_order_relaxed);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getModifiers().isAltDown() || key.getModifiers().isCtrlDown()) return false;
        scanKeys(); return true;
    }
    bool keyStateChanged(bool) override { scanKeys(); return true; }
    void mouseDown(const juce::MouseEvent& e) override {
        if(curtain){grabKeyboardFocus();return;}
        const char* rows[]={"1234567890-=","QWERTYUIOP[]","ASDFGHJKL;","ZXCVBNM,./"};
        for(int r=0;r<4;++r) for(int c=0;rows[r][c];++c) {
            juce::Rectangle<int> rect(44+c*80+(r>1?(r-1)*18:0),210+r*66,70,60);
            if(rect.contains(e.getPosition())) { selectedKey=rows[r][c];
                if(e.mods.isCtrlDown()) selectedKeys[selectedKey]=!selectedKeys[selectedKey];
                else { selectedKeys.fill(false); selectedKeys[selectedKey]=true; } analogHint=juce::String::fromUTF8("已选 ")+juce::String::charToString((juce::juce_wchar)selectedKey)+juce::String::fromUTF8("：Ctrl + 点击可多选，然后点击「校准选中键」。"); }
        }
        grabKeyboardFocus(); repaint();
    }
    void focusLost(FocusChangeType) override { silence(); }
    void resized() override {
        analogStart.setBounds(36,680,140,34); analogStop.setBounds(184,680,95,34);
        calibrate.setBounds(287,680,120,34); calibrateAll.setBounds(415,680,95,34);
        cancelCalibration.setBounds(518,680,95,34); sensitivity.setBounds(736,680,270,34);
        about.setBounds(820,150,185,30);
        library.setBounds(36,150,360,30);loadLibrary.setBounds(405,150,100,30);attachLibrary.setBounds(514,150,130,30);
        importButton.setBounds(36,872,170,34);playLesson.setBounds(216,872,155,34);
        pauseLesson.setBounds(381,872,125,34);curtainButton.setBounds(516,872,115,34);lessonSpeed.setBounds(735,872,270,34);
        layout.setBounds(36, 112, 310, 34);
        down.setBounds(365, 112, 96, 34);
        up.setBounds(470, 112, 96, 34);
        stop.setBounds(580, 112, 110, 34);
        settings.setBounds(getWidth()-198, 38, 162, 34);
        velocity.setBounds(770, 112, 235, 34);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff10151f));
        g.setColour(juce::Colour(0xffedf3ff));
        g.setFont(30.0f);
        g.drawText("HomeKeys", 36, 28, 340, 46, juce::Justification::centredLeft);
        g.setFont(14.0f);
        g.setColour(juce::Colour(0xff91a1ba));
        g.drawText(juce::String::fromUTF8("白键连续、黑键嵌在上一排；默认采用常用电脑钢琴指法。"), 36, 75, 650, 23, juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("力度"), 705, 112, 70, 34, juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8(layout.getSelectedId() == 4 ? "低音白键 Z X C V B N M；黑键 S D G H J" : layout.getSelectedId() == 3 ? "五个八度：C2—C7（共 61 音）" : layout.getSelectedId() == 1 ? "横向：相邻半音" : "白键：主排连续排列"), 44, 183, 440, 25, juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8(layout.getSelectedId() == 4 ? "高音白键 Q W E R T Y U；黑键 2 3 5 6 7" : layout.getSelectedId() == 3 ? "数字→Q 排→A 排→Z 排；Shift 对应黑键" : layout.getSelectedId() == 1 ? "向上一排：纯四度（5 个半音）" : "黑键：上排对应升降音"), 555, 183, 440, 25, juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8(layout.getSelectedId() == 4 ? "常用双层钢琴排列，无需 Shift；默认 C3—G5，八度按钮可整体移调。" : layout.getSelectedId() == 3 ? "36 个白键 + Shift 的 25 个黑键；Y / G / H / L 都用于演奏。" : layout.getSelectedId() == 1 ? "四度布局含同音位置重复。" : "灰色按键：当前钢琴布局未分配音符。"), 36, 608, 980, 25, juce::Justification::centredLeft);
        const auto keyMappings=analog.snapshot();
        const char* rows[] = {"1234567890-=", "QWERTYUIOP[]", "ASDFGHJKL;", "ZXCVBNM,./"};
        for (int row=0; row<4; ++row) {
            const juce::String keys(rows[row]);
            for (int col=0; col<keys.length(); ++col) {
                const int key = static_cast<int>(keys[col]);
                int note = -1;
                eachBinding([&](homekeys::Binding b) { if (b.key == key) note = b.note + octave*12; });
                const float x = 44.0f + col*80.0f + (row>1?(row-1)*18.0f:0);
                const float y = 210.0f + row*66.0f;
                const bool held = input.held(key) || analog.held[key].load();
                const int pitchClass=note>=0?note%12:-1;
                const bool black=pitchClass==1 || pitchClass==3 || pitchClass==6 || pitchClass==8 || pitchClass==10;
                const bool white=layout.getSelectedId()==4 && note>=0 && !black;
                g.setColour(held ? juce::Colour(0xff71e3c1) : white?juce::Colour(0xffedf1f7) : note>=0?juce::Colour(0xff263349):juce::Colour(0xff181f2c));
                g.fillRoundedRectangle(x, y, 70, 60, 9);
                if(selectedKeys[key]) { g.setColour(juce::Colour(0xffffc66e)); g.drawRoundedRectangle(x,y,70,60,9,2); }
                g.setColour(held || white ? juce::Colour(0xff10151f) : juce::Colour(0xffedf3ff));
                g.setFont(22.0f);
                g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(key)), static_cast<int>(x+12), static_cast<int>(y+7), 50, 29, juce::Justification::centredLeft);
                g.setFont(13.0f);
                const auto label = note >= 0 ? juce::MidiMessage::getMidiNoteName(note, true, true, 4) : juce::String::fromUTF8("未分配");
                g.drawText(label, static_cast<int>(x+12), static_cast<int>(y+34), 60, 21, juce::Justification::centredLeft);
                if(keyMappings[key].key>=0) { g.setColour(juce::Colour(0xff71e3c1)); g.fillEllipse(x+58,y+6,6,6); }
                if(layout.getSelectedId()==3 && note>=0 && homekeys::blackAbove(note-octave*12)>=0) {
                    const int black=homekeys::blackAbove(note-octave*12)+octave*12;
                    g.setColour(juce::Colour(0xffb6c5db)); g.setFont(10.f);
                    g.drawText(juce::String("S:")+juce::MidiMessage::getMidiNoteName(black,true,true,4),(int)x+35,(int)y+20,40,14,juce::Justification::centredRight);
                }
                if (key == 'F' || key == 'J') { g.setColour(juce::Colour(0xff71e3c1)); g.fillRect(x+28, y+55, 20.0f, 2.0f); }
            }
        }
        if(curtain) {
            g.setColour(juce::Colour(0xff0b111d));g.fillRect(36,210,1008,270);
            const double t=lesson.position.load();
            g.setColour(juce::Colour(0xff71e3c1));g.drawLine(36,453,1044,453,2);
            if(lesson.data) {
                int low=60,high=72;
                for(auto n:lesson.data->notes){low=std::min(low,n.pitch);high=std::max(high,n.pitch);}
                const float width=1008.f/(high-low+1);
                for(int n=low;n<=high;++n){g.setColour(juce::Colour(0xff1d293c));g.drawVerticalLine(int(36+(n-low)*width),210,480);}
                int drawn=0;
                for(auto n:lesson.data->notes) {
                    if(n.end<t-.3 || n.start>t+3 || drawn>=500)continue;
                    ++drawn;const float x=36+(n.pitch-low)*width;
                    const float y=std::max(210.f,float(453-(n.end-t)*75));
                    const float bottom=std::min(453.f,float(453-(n.start-t)*75));
                    if(bottom<=y)continue;
                    g.setColour(n.start<=t?juce::Colour(0xffffc66e):juce::Colour(0xff71e3c1));
                    g.fillRoundedRectangle(x+1,y,std::max(2.f,width-2),std::max(3.f,bottom-y),3);
                    int key=-1;eachBinding([&](homekeys::Binding b){if(key<0 && b.note+octave*12==n.pitch)key=b.key;});
                    if(width>=18){g.setColour(juce::Colours::black);g.setFont(12.f);g.drawText(key>=0?juce::String::charToString((juce::juce_wchar)key):juce::String::fromUTF8("·"),(int)x,(int)y,(int)width,16,juce::Justification::centred);}
                    if(width>=30 && bottom-y>=30) {g.setFont(10.f);g.drawText(juce::MidiMessage::getMidiNoteName(n.pitch,true,true,4),(int)x,(int)y+15,(int)width,14,juce::Justification::centred);}
                }
                g.setColour(juce::Colours::white);g.setFont(13.f);
                g.drawText(juce::String(t,1)+" / "+juce::String(lesson.data->duration,1)+juce::String::fromUTF8(" 秒  |  音块字母=电脑键；·=当前布局无直接键位"),44,455,980,23,juce::Justification::centredLeft);
            } else {g.setColour(juce::Colours::white);g.drawText(juce::String::fromUTF8("导入 MIDI 或音频后，点击自动演奏。"),60,300,950,40,juce::Justification::centred);}
        }
        g.setColour(juce::Colour(0xff91a1ba));g.setFont(13.f);
        g.drawText(lessonStatus,36,915,1008,23,juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("MIDI 速度"),640,872,90,34,juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8(capsProtected?"Caps Lock 防误触已开启":"Caps Lock 钩子已停用"),660,153,380,22,juce::Justification::centredLeft);
        g.setColour(pedal ? juce::Colour(0xff71e3c1) : juce::Colour(0xff263349));
        g.fillRoundedRectangle(290, 494, 470, 42, 8);
        g.setColour(juce::Colours::white);
        g.setFont(15.0f);
        g.drawText(pedal ? juce::String::fromUTF8("空格 · 延音已开启") : juce::String::fromUTF8("空格 · 按住延音"), 290, 494, 470, 42, juce::Justification::centred);
        g.setColour(juce::Colour(0xff91a1ba));
        g.drawText(juce::String::fromUTF8("八度 ") + juce::String(octave) + juce::String::fromUTF8("   |   复音 ") + juce::String(voices.load()) + juce::String::fromUTF8("/64   |   丢弃事件 ") + juce::String(dropped.load()), 36, 560, 980, 25, juce::Justification::centredLeft);
        if (auto* device = deviceManager.getCurrentAudioDevice()) {
            const double rate = device->getCurrentSampleRate();
            const int buffer = device->getCurrentBufferSizeSamples();
            const auto status = device->getName() + "   |   " + juce::String(rate, 0) + " Hz   |   " + juce::String(buffer) + juce::String::fromUTF8(" 帧   |   缓冲时长 ") + juce::String(rate > 0 ? 1000.0*buffer/rate : 0, 2) + juce::String::fromUTF8(" 毫秒（非总延迟）   |   CPU ") + juce::String(deviceManager.getCpuUsage()*100, 1) + "%";
            g.drawText(status, 36, 590, getWidth()-72, 25, juce::Justification::centredLeft);
        } else g.drawText(deviceError, 36, 590, 980, 25, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff71e3c1));
        g.drawText(juce::String::fromUTF8("磁轴灵敏度"),630,680,100,34,juce::Justification::centredLeft);
        const char* states[]={"未开启","正在连接","已开启，等待行程","正在接收行程","未找到兼容接口","通信失败，已停止","60 秒无行程，已停止","设备被其他实例占用"};
        const int state=std::clamp(analog.status.load(),0,7);
        g.drawText(juce::String::fromUTF8(states[state])+juce::String::fromUTF8("  |  行程原始值 ")+juce::String(analog.lastDepth.load())+"/40"+juce::String::fromUTF8("  |  监测力度 ")+juce::String(analog.monitorVelocity.load())+"/127"+(analog.monitorEstimated.load()?juce::String::fromUTF8("（估速）"):juce::String::fromUTF8("（默认/待击键）"))+juce::String::fromUTF8("  |  演奏力度 ")+(analog.lastVelocity.load()>0?juce::String(analog.lastVelocity.load()):juce::String::fromUTF8("待已校准键击键")),36,725,1008,25,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffedf3ff));
        g.drawText(analogHint,36,755,1008,25,juce::Justification::centredLeft);
        int mappedCount=0; for(auto m:keyMappings) if(m.key>=0) ++mappedCount;
        g.setColour(juce::Colour(0xff91a1ba));
        g.drawText(juce::String::fromUTF8("绿点=已校准 ")+juce::String(mappedCount)+juce::String::fromUTF8(" 键  |  行程报告 ")+juce::String(analog.reports.load())+juce::String::fromUTF8("  |  输入溢出 ")+juce::String(analog.overflows.load())+(analog.stopCommandOk.load()?juce::String():juce::String::fromUTF8("  停止指令未确认，请拔插键盘恢复。")),36,790,1008,25,juce::Justification::centredLeft);
        const int source=audibleSource.load();
        const auto sourceName=juce::String::fromUTF8(source==2?"磁轴力度":source==1?"固定力度（此键未使用磁轴）":"等待击键");
        const auto actual=source==0?sourceName:juce::String::charToString((juce::juce_wchar)audibleKey.load())+"  "+sourceName+"  "+juce::String(audibleVelocity.load())+"/127";
        g.setColour(source==1?juce::Colour(0xffffc66e):juce::Colour(0xff71e3c1));
        g.drawText(juce::String::fromUTF8("实际发声：")+actual+juce::String::fromUTF8("  |  起音以来输出峰值 ")+juce::String(juce::Decibels::gainToDecibels(audiblePeak.load(),-100.f),1)+" dBFS",36,825,1008,25,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff91a1ba));
        g.drawText(juce::String::fromUTF8("点击演奏区域后按键发声；空格延音，Esc 静音。当前为合成音色。"), 36, 636, 1000, 26, juce::Justification::centredLeft);
    }
};

class HomeKeysApplication final : public juce::JUCEApplication {
    class Window final : public juce::DocumentWindow {
    public:
        Window() : DocumentWindow("HomeKeys", juce::Colour(0xff10151f), allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            getContentComponent()->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
public:
    const juce::String getApplicationName() override { return "HomeKeys"; }
    const juce::String getApplicationVersion() override { return "0.9.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted(const juce::String&) override {
        if (window) { window->setMinimised(false); window->setVisible(true); window->toFront(true); }
    }
    void initialise(const juce::String&) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName("Microsoft YaHei UI");
        juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(juce::String::fromUTF8(R"zh(
language: Chinese (Simplified)
countries: cn
"none" = "无"
"Show advanced settings..." = "显示高级设置…"
"Error when trying to open audio device!" = "无法打开音频设备"
"OK" = "确定"
"Cancel" = "取消"
"Close" = "关闭"
"(no audio output channels found)" = "（未找到音频输出通道）"
"Active output channels:" = "启用的输出通道："
"(no audio input channels found)" = "（未找到音频输入通道）"
"Active input channels:" = "启用的输入通道："
"Control Panel" = "设备控制面板"
"Opens the device's own control panel" = "打开设备自带的控制面板"
"Reset Device" = "重置设备"
"Resets the audio interface - sometimes needed after changing a device's properties in its custom control panel" = "重置音频接口；在设备控制面板修改参数后可能需要此操作"
"Output:" = "输出："
"Device:" = "设备："
"Test" = "试听"
"Plays a test tone" = "播放测试音"
"Input:" = "输入："
"Sample rate:" = "采样率："
"Audio buffer size:" = "音频缓冲大小："
"Audio device type:" = "音频设备类型："
"No MIDI inputs available" = "没有可用的 MIDI 输入"
"Active MIDI inputs:" = "启用的 MIDI 输入："
"Bluetooth MIDI" = "蓝牙 MIDI"
"Scan for bluetooth MIDI devices" = "扫描蓝牙 MIDI 设备"
"MIDI Output:" = "MIDI 输出："
)zh"), false));
        window = std::make_unique<Window>();
    }
    void shutdown() override { window.reset(); }
};
START_JUCE_APPLICATION(HomeKeysApplication)
