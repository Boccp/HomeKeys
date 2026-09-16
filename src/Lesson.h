// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Core.h"
#include <thread>
#include <mutex>

namespace homekeys {
struct LessonNote { double start=0,end=0; int pitch=60,id=0; float velocity=.7f; };
struct LessonEvent { double time=0; Event event; };
struct LessonData {
    juce::String name,error;
    bool audioFile=false;
    double duration=0,rate=44100;
    juce::AudioBuffer<float> recording;
    std::vector<LessonNote> notes;
    std::vector<LessonEvent> events;
    void finish() {
        for(auto n:notes) {
            events.push_back({n.start,{EventType::on,n.id,n.pitch,n.velocity}});
            events.push_back({n.end,{EventType::off,n.id,n.pitch,0}});
            duration=std::max(duration,n.end);
        }
        std::stable_sort(events.begin(),events.end(),[](auto a,auto b){
            return a.time==b.time ? int(a.event.type)>int(b.event.type) : a.time<b.time;
        });
        std::sort(notes.begin(),notes.end(),[](auto a,auto b){return a.start<b.start;});
    }
};
// Optional rough monophonic transcription, not a polyphonic piano transcriber.
inline void transcribeMelody(LessonData& d,const std::atomic<bool>& cancel) {
    constexpr int rate=8000,window=512,hop=160,maxLag=123;
    std::array<float,window> x{}; std::array<double,maxLag+1> diff{};
    int active=-1; double started=0,last=0;
    auto close=[&](double end){if(active>=0 && end-started>=.10)d.notes.push_back({started,end,active,int(d.notes.size()),.7f});active=-1;};
    const int count=int(d.duration*rate);
    for(int offset=0;offset+window<count && !cancel.load();offset+=hop) {
        double energy=0;
        for(int i=0;i<window;++i) {
            const double p=(offset+i)*d.rate/rate;
            const int at=std::min(int(p),d.recording.getNumSamples()-2);
            float value=0;
            for(int c=0;c<d.recording.getNumChannels();++c) {
                const auto* samples=d.recording.getReadPointer(c);
                value+=samples[at]+float(p-at)*(samples[at+1]-samples[at]);
            }
            x[i]=value/d.recording.getNumChannels();energy+=x[i]*x[i];
        }
        int note=-1; double cumulative=0;
        if(energy/window>0.0001) {
            for(int tau=1;tau<=maxLag;++tau) {
                double sum=0;for(int j=0;j<window-maxLag;++j){const double v=x[j]-x[j+tau];sum+=v*v;}
                cumulative+=sum;diff[tau]=cumulative>0?sum*tau/cumulative:1;
            }
            for(int tau=8;tau<maxLag-1;++tau) if(diff[tau]<.15 && diff[tau]<=diff[tau+1]) {
                const double denominator=diff[tau-1]-2*diff[tau]+diff[tau+1];
                const double lag=tau+(std::abs(denominator)>1e-9?.5*(diff[tau-1]-diff[tau+1])/denominator:0);
                note=int(std::lround(69+12*std::log2((rate/lag)/440.0)));
                if(note<36 || note>95)note=-1;
                break;
            }
        }
        const double time=double(offset)/rate;last=time;
        if(note!=active) {close(time);if(note>=0){active=note;started=time;}}
    }
    close(last);
}
inline std::unique_ptr<LessonData> loadLesson(const juce::File& file,const std::atomic<bool>& cancel) {
    auto d=std::make_unique<LessonData>();d->name=file.getFileName();
    if(file.hasFileExtension("mid;midi")) {
        if(file.getSize()>16*1024*1024){d->error=juce::String::fromUTF8("MIDI 文件超过 16 MB 限制。");return d;}
        auto in=file.createInputStream();juce::MidiFile midi;int type=0;
        if(!in || !midi.readFrom(*in,true,&type) || type==2){d->error=juce::String::fromUTF8("无法读取 MIDI；支持格式 0/1。");return d;}
        midi.convertTimestampTicksToSeconds();
        int id=0;
        for(int track=0;track<midi.getNumTracks() && !cancel.load();++track) {
            const auto* sequence=midi.getTrack(track);
            for(int i=0;i<sequence->getNumEvents();++i) {
                const auto* e=sequence->getEventPointer(i);const auto& m=e->message;
                if(m.isNoteOn() && e->noteOffObject) {
                    const double start=m.getTimeStamp(),end=e->noteOffObject->message.getTimeStamp();
                    if(!std::isfinite(start)||!std::isfinite(end)||start<0||end>3600||end<start||id>=50000) {d->error=juce::String::fromUTF8("MIDI 超出时长或音符限制。");return d;}
                    if(m.getChannel()!=10)d->notes.push_back({start,end,m.getNoteNumber(),id++,m.getFloatVelocity()});
                }
            }
        }
    } else {
        d->audioFile=true;
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader || reader->sampleRate<=0 || reader->lengthInSamples<2 || reader->numChannels<1) {d->error=juce::String::fromUTF8("不支持或无法读取此音频。");return d;}
        d->duration=double(reader->lengthInSamples)/reader->sampleRate;d->rate=reader->sampleRate;
        if(d->duration>120 || reader->lengthInSamples>120*96000 || reader->sampleRate>96000){d->error=juce::String::fromUTF8("试用版音频限制：两分钟以内，采样率不超过 96 kHz。");return d;}
        d->recording.setSize(std::min(2,int(reader->numChannels)),int(reader->lengthInSamples));
        if(!reader->read(&d->recording,0,int(reader->lengthInSamples),0,true,true)) {d->error=juce::String::fromUTF8("音频读取失败。");return d;}
        transcribeMelody(*d,cancel);
    }
    d->finish();return d;
}
// Owned and accessed by audio callback, except when caller holds callback lock.
class LessonPlayer {
    Engine synth;size_t next=0;double time=0,sampleRate=48000;bool running=false;
public:
    std::unique_ptr<LessonData> data;
    std::atomic<double> position{0}, speed{1};
    std::atomic<bool> playing{false};
    void prepare(double rate){sampleRate=rate;synth.prepare(rate);}
    void stop(){running=false;playing=false;time=0;position=0;next=0;synth.panic();}
    void start(){stop();if(data && data->error.isEmpty() && data->duration>0){running=true;playing=true;}}
    void pause(){running=false;playing=false;synth.panic();}
    void resume(){
        if(data && time<data->duration){
            if(!data->audioFile)for(auto n:data->notes)if(n.start<time && n.end>time)synth.event({EventType::on,n.id,n.pitch,n.velocity});
            running=true;playing=true;
        }
    }
    void render(float* const* outputs,int channels,int samples) {
        if(!running || !data)return;
        const double step=(data->audioFile?1.0:speed.load())/sampleRate;
        for(int i=0;i<samples;++i) {
            if(time>data->duration+.25){pause();break;}
            if(data->audioFile) {
                const double p=time*data->rate;const int at=int(p);
                if(at+1<data->recording.getNumSamples())for(int c=0;c<channels;++c) {
                    const auto* source=data->recording.getReadPointer(std::min(c,data->recording.getNumChannels()-1));
                    outputs[c][i]+=.5f*(source[at]+float(p-at)*(source[at+1]-source[at]));
                }
            } else {
                int budget=0;while(next<data->events.size() && data->events[next].time<=time && budget++<1024)synth.event(data->events[next++].event);
                float sample=0;float* mono=&sample;synth.render(&mono,1,1);
                for(int c=0;c<channels;++c)outputs[c][i]+=sample;
            }
            time+=step;
        }
        position=time;
    }
};
}
