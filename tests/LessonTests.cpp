// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#include "Lesson.h"
#include <iostream>
#include <cstdlib>
void require(bool b,const char* s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
int main(int argc,char** argv){
    std::atomic<bool> cancel{false};
    if(argc>1) {
        for(int i=1;i<argc;++i) {
            auto data=homekeys::loadLesson(juce::File(juce::String::fromUTF8(argv[i])),cancel);
            require(data->error.isEmpty() && !data->notes.empty(),"bundled MIDI must contain usable notes");
            int low=127,high=0;float soft=1,loud=0;
            for(auto n:data->notes){low=std::min(low,n.pitch);high=std::max(high,n.pitch);soft=std::min(soft,n.velocity);loud=std::max(loud,n.velocity);}
            std::cout<<data->name<<" | notes="<<data->notes.size()<<" | seconds="<<data->duration<<" | MIDI range="<<low<<".."<<high<<" | velocity="<<soft<<".."<<loud<<'\n';
            homekeys::LessonPlayer player;player.prepare(48000);player.data=std::move(data);player.start();
            std::array<float,480> buffer{};float* out=buffer.data();double energy=0;
            const int blocks=int((player.data->duration+.5)*100);
            for(int block=0;block<blocks;++block){buffer.fill(0);player.render(&out,1,480);for(float x:buffer){require(std::isfinite(x),"finite bundled-song audio");energy+=x*x;}}
            require(energy>0 && !player.playing,"bundled MIDI renders and reaches end");
        }
        return 0;
    }
    juce::MidiFile midi;midi.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence tempo,notes;
    auto first=juce::MidiMessage::tempoMetaEvent(500000);first.setTimeStamp(0);tempo.addEvent(first);
    auto second=juce::MidiMessage::tempoMetaEvent(1000000);second.setTimeStamp(480);tempo.addEvent(second);
    auto on=juce::MidiMessage::noteOn(1,60,.8f);on.setTimeStamp(0);notes.addEvent(on);
    auto off=juce::MidiMessage::noteOff(1,60);off.setTimeStamp(960);notes.addEvent(off);
    midi.addTrack(tempo);midi.addTrack(notes);
    auto file=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("lesson-fixture",".mid");
    {juce::FileOutputStream out(file);require(out.openedOk() && midi.writeTo(out),"write MIDI fixture");}
    auto data=homekeys::loadLesson(file,cancel);
    require(data->error.isEmpty() && data->notes.size()==1,"parse multi-track MIDI");
    require(std::abs(data->notes[0].end-1.5)<.001,"tempo map converts ticks to seconds");
    homekeys::LessonPlayer player;player.prepare(48000);player.data=std::move(data);player.speed=.5;player.start();
    std::array<float,480> buffer{};float* out=buffer.data();player.render(&out,1,480);
    double energy=0;for(float x:buffer)energy+=x*x;
    require(energy>0 && std::abs(player.position.load()-.005)<.0001,"MIDI produces audio at selected tempo");
    player.pause();buffer.fill(0);player.render(&out,1,480);for(float x:buffer)require(x==0,"pause silence");
    player.resume();buffer.fill(0);player.render(&out,1,480);energy=0;for(float x:buffer)energy+=x*x;
    require(energy>0,"resume restores active notes");player.stop();require(player.position==0 && !player.playing,"stop resets transport");
    homekeys::LessonData melody;melody.audioFile=true;melody.rate=8000;melody.duration=1;
    melody.recording.setSize(1,8000);
    for(int i=0;i<8000;++i)melody.recording.setSample(0,i,.3f*std::sin(float(i)*2*3.14159265358979323846f*440/8000));
    homekeys::transcribeMelody(melody,cancel);
    require(!melody.notes.empty() && melody.notes[0].pitch==69,"monophonic A4 analysis");
    std::cout<<"Lesson tests passed: MIDI tempo map, audio rendering, speed, pause/resume, stop and synthetic monophonic transcription. No device opened.\n";
}
