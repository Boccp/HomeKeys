# HomeKeys

用电脑键盘弹琴，也试着让磁轴键盘弹出轻重。

我做 HomeKeys，是想在 Windows 上用手边的键盘练琴。普通按键只能告诉程序“按下了没有”，磁轴键盘如果能读到行程，就有机会根据下压速度估算力度。这是这个项目最想尝试的部分。

目前它还是一个在持续修改的小项目。能弹、能导入 MIDI 跟着练，也有不少地方没做好。我把源码放出来，欢迎试用、提问题，或者一起改。

## 现在能做什么

- 默认简体中文，双层电脑钢琴键位，支持切换布局和八度移调。
- 普通键盘直接演奏；已识别的磁轴按键可以根据下压速度估算力度。
- 单键自动对应、全部校准和选中键校准。
- 导入 MIDI，显示下落音符，自动演奏、暂停和调整播放速度。
- 导入音频跟听，并尝试提取单旋律。
- 窗口激活时拦截 Caps Lock，减少演奏中误切大小写。

## 先说明白几个限制

磁轴力度不是压力传感器，也不是触底后的 aftertouch。现在使用的是行程变化速度的估计，USB 报告间隔和校准结果都会影响它。目前只围绕一把以 `0416:7372` 上报的 Latenpow 键盘开发，同一个 USB 标识不保证协议相同，不能当作通用磁轴驱动。

声音还是合成音色，力度主要改变音量，没有多层钢琴采样。音频转谱只适合试验清晰的单音旋律，不能可靠地把一首完整歌曲变成钢琴谱。MIDI 播放暂不还原踏板、弯音和乐器切换。

公开版只附音阶练习。想练别的曲子，请导入你有权使用的 MIDI；网上找到的谱子不因此获得公开分发授权。

## 使用

从 [Releases](https://github.com/Boccp/HomeKeys/releases) 下载 Windows x64 包，完整解压后运行 `HomeKeys.exe`。不要只拖出 exe；`lessons` 和许可文件也请保留。

选中“入门音阶练习”，点“加载所选”，再点“自动演奏 / 重播”。自己的文件可以用“导入 MIDI / 音频”打开。

默认低层白键为 `Z X C V B N M , . /`，高层白键为 `Q W E R T Y U I O P [ ]`，黑键在相邻上排。具体位置见 [键位对照](双层钢琴键位对照.md)。

普通键盘无需校准。使用磁轴试验前，请读 [磁轴说明](docs/analog.md)。未建立行程对应关系的键使用手动设定的力度；已识别的键才走磁轴力度路径。

校准和本地曲目关联保存在 exe 旁的 XML 文件中，不上传。更新时可自行保留这些文件。程序没有主动联网的业务逻辑。

当前 Windows 程序未签名，曾收到 360 拦截反馈，具体原因尚未确认。不要为了运行它关闭安全防护；可以查看源码自行构建，或把具体检测名称作为问题反馈。

## 从源码构建

需要 Windows x64、Visual Studio 2022 Build Tools 的“使用 C++ 的桌面开发”（含 Windows SDK）、CMake 3.22 或更新版本，以及 Git。首次配置会从 GitHub 下载固定版本的 JUCE 8.0.9。

在 PowerShell 中运行：

```powershell
./build.ps1
```

脚本使用两个编译任务，完成后运行离线测试，程序和必要文件放在 `dist`。脚本不会自动启动软件，不会读取你的磁轴设备。

也可以直接构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --timeout 30
```

离线环境可在 CMake 配置时传入 `-DFETCHCONTENT_SOURCE_DIR_JUCE=你的JUCE路径`。Release 中的 `HomeKeys-v0.9.0-source-with-JUCE.zip` 还包含本次构建使用的 JUCE 源码。在该归档的 HomeKeys 目录内，可使用：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE=./vendor/JUCE
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --timeout 30
./package.ps1
```

离线构建仍需事先安装上述编译器、SDK 和 CMake。

## 测试与后续

离线测试覆盖音符分配、行程估速、输入路由、MIDI 时间换算和音频生成。它们不打开音频设备或 HID，也不能代替实机延迟、兼容性和听感测试。仓库中的构建脚本会在生成程序后运行这套测试。

我接下来想优先改进音源、磁轴协议兼容和练习体验。遇到问题请在 [Issues](https://github.com/Boccp/HomeKeys/issues) 留下软件版本、键盘型号和复现步骤；音频问题也请写明输出设备。不要上传个人配置、密钥或没有分发授权的歌曲文件。

## 许可与署名

Copyright (C) 2026 Boccp。HomeKeys 的原创代码按 **GNU AGPL v3（仅第 3 版，AGPL-3.0-only）** 发布，完整条款见 [LICENSE](LICENSE)。附带的原创音阶练习也按这一许可提供。

可以修改、再分发和商用，但要遵守协议，保留适用的版权与许可声明，并按要求提供对应源码。修改版请标明改动，不要让使用者误以为是我发布的原版。程序不提供任何担保。这里的说明不替代许可证正文，也不额外限制协议授予的权利。

JUCE 和其依赖保留各自的版权及许可，见 [第三方说明](THIRD_PARTY_NOTICES.md)。
