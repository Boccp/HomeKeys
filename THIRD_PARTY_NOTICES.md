# 第三方许可与参考

HomeKeys 的原创代码：Copyright (C) 2026 Boccp，AGPL-3.0-only。

## JUCE 8.0.9

本项目按 JUCE 的 AGPLv3 选项使用其模块。JUCE 同时提供商业许可；不采用 AGPL 授权的发行需要另行满足 JUCE 商业许可要求。完整声明见 licenses/JUCE-8.0.9.md。

固定版本源码：https://github.com/juce-framework/JUCE/tree/8.0.9 。构建通过 CMake FetchContent 获取该版本，许可证不会因此改成 HomeKeys 的版权声明。

Windows 构建涉及的第三方音频／图形库声明保留在 licenses 下，包括 FLAC、Ogg Vorbis、HarfBuzz、SheenBidi、libjpeg、libpng 和 zlib。源码发行附带对应版本 JUCE 源码归档，以保留内嵌的其他版权和许可证。

## 键位和协议参考

- 双层电脑钢琴键序参考 Songtive：https://www.songtive.com/en/apps/piano
- 五八度 Shift 布局参考 Recursive Arts：https://recursivearts.com/virtual-piano/keymapping.html
- 实验性磁轴报文依据公开 KeyAxis 协议文档：https://github.com/RigZeeel/KeyAxis/blob/main/PROTOCOL.md

以上列出实现参考，不表示这些项目参与开发或为 HomeKeys 背书。

公开版本不包含第三方歌曲 MIDI、曲谱图片或 OCR 数据。lessons/scale.mid 是开发期间编写的音阶练习，按本项目 AGPL-3.0-only 提供。
