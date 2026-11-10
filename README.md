# SprintSplit —— 把「冲刺」从翻滚键里拆出来（ELDEN RING 输入层 mod）

原版把 **后跃 / 翻滚 / 冲刺** 绑在同一个键（手柄 B）上：点一下是翻滚或后跃，按住是冲刺。
所以只改游戏里的按键设置是拆不开的，本 mod 从**输入层**把这两种输入拆成两个键：

| 操作 | 原版 | 装了这个 mod |
| --- | --- | --- |
| 站立不动 + 按 B | 后跃 | 后跃（不变） |
| 移动中 + 按 B | 翻滚 | 翻滚（不变） |
| 移动中 + **按住 B** | 冲刺 | **只会翻滚一次，不再冲刺** |
| **按住冲刺键** + 推动左摇杆 | — | 冲刺 |

冲刺键默认是 **L3（左摇杆按下）**，可以在一行配置里换成任意键。

---

## 一、原理（为什么必须做成 DLL）

`eldenring.exe` 的导入表按**序号**引用了 `XInput1_4.dll`：

```
XINPUT1_4.dll  (2 symbols)
    #2 (ordinal)   -> XInputGetState
    #3 (ordinal)   -> XInputSetState
```

Windows 加载 DLL 时优先查找 **exe 所在目录**，所以把同名代理 DLL 放在 `Game\` 里，
游戏就会先加载它。这个代理：

1. 把 `XInputGetState` / `XInputSetState` 等调用原样转发给 `C:\Windows\System32\XInput1_4.dll`；
2. 只在 `XInputGetState` 返回前改写按键字（`XINPUT_GAMEPAD.wButtons`）：
   - **物理 B 键**：无论你按多短多长，都改写成一次固定时长的「按下 → 松开」
     （默认 50ms，60 帧下约 3 帧）。游戏对 B 的判定是「松开时看按了多久」，
     这么短就只能是点按 → 后跃 / 翻滚。
   - **冲刺键**：当它被按住、并且左摇杆偏移超过死区时，输出里**持续带上 B**，
     游戏看到的就是「按住 B + 移动」→ 冲刺。松开冲刺键或停下摇杆就撤销。

不修改任何游戏文件、不写游戏内存、不做代码注入，只改本进程读到的手柄状态。
因此和版本无关（1.16 / 1.17 都一样），也能和 ME3（重回巅峰启动器）、Seamless Coop 共存。

---

## 二、目录

```
SprintSplit\
├─ src\
│  ├─ xinput_proxy.cpp   XInput1_4.dll 代理 + 导出函数
│  ├─ sprint_logic.h     按键状态机（纯逻辑，无 Windows 依赖）
│  ├─ config.h/.cpp      INI 解析
│  └─ proxy.def          导出表（序号必须和系统 DLL 一致）
├─ tools\
│  ├─ pe_imports.py      查看 PE 导入表（当初就是用它确认游戏按序号导入）
│  ├─ pe_exports.py      查看 PE 导出表
│  ├─ check_exports.py   校验编译出来的 DLL 序号是否齐全
│  ├─ selftest_dll.py    加载 DLL 并试调 XInputGetState（不用开游戏）
│  └─ simulate.py        状态机仿真（不启动游戏也能验证时序）
├─ .github\workflows\build.yml   GitHub Actions 云端编译（没本地编译器时用）
├─ build.bat             一键编译：MSVC → MinGW → Zig 依次尝试
├─ install.bat           复制到 ..\Game
├─ uninstall.bat         卸载
├─ SprintSplit.ini       配置模板
└─ README.md
```

---

## 三、编译

仓库里只有源码，`xinput1_4.dll` 需要你自己用下面的任意一种工具链编译（都是一条命令）。

### 方案 A：Visual Studio Build Tools（推荐）

1. 下载 “Visual Studio 2022 生成工具”（Build Tools），安装时勾选
   **使用 C++ 的桌面开发 / Desktop development with C++**；
2. 打开命令行进入本目录，运行：

```bat
build.bat
```

脚本会用 `vswhere` 找到 MSVC，执行：

```bat
cl /MT /O2 /std:c++17 /EHsc src\xinput_proxy.cpp src\config.cpp ^
   /link /DLL /DEF:src\proxy.def /OUT:build\xinput1_4.dll
```

### 方案 B：MinGW-w64

把 `g++` 放进 PATH 后运行 `build.bat`，或手动：

```bat
g++ -O2 -std=c++17 -shared -static src\xinput_proxy.cpp src\config.cpp src\proxy.def -o build\xinput1_4.dll
```

> 如果链接时报 `cannot find -lstdc++`，说明这份 MinGW 没带静态 C++ 运行库，
> 去掉 `-static`，再把 `libstdc++-6.dll`、`libgcc_s_seh-1.dll` 一起复制到 `Game\`。

### 方案 C：Zig

```bat
zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared -static ^
    src\xinput_proxy.cpp src\config.cpp src\proxy.def -o build\xinput1_4.dll
```

### 编译后自检（强烈建议，不用开游戏）

```bat
python tools\check_exports.py build\xinput1_4.dll
python tools\selftest_dll.py  build\xinput1_4.dll
```

第一条检查导出序号，第二条会真的 `LoadLibrary` 这个 DLL、按序号 2/3 取函数、
调一次 `XInputGetState`，并把日志打印出来。两条都通过再进游戏。

必须看到 `#2 XInputGetState` 和 `#3 XInputSetState`。
**少了这两个序号，游戏会直接报「找不到入口点」启动失败。**

---

## 四、安装

```bat
build.bat
install.bat
```

`install.bat` 会做两件事：

```
Game\xinput1_4.dll                      <- 代理 DLL（必须和 eldenring.exe 同目录）
Game\SprintSplit\SprintSplit.ini        <- 配置（已存在则不覆盖）
```

然后像平常一样启动游戏（ME3 启动器 / `eldenring.exe`）。
启动后如果 `Game\SprintSplit\SprintSplit.log` 里出现

```
SprintSplit starting (SprintSplit.ini loaded), module dir D:\games\ELDEN RING\Game
first XInputGetState call, pad 0, buttons=0x0000
```

说明 mod 已经生效。

> 不要用带 EAC 的在线模式启动。这个 mod 只面向离线 / ME3 / 无缝联机这类已经绕过 EAC 的环境。

---

## 五、配置（`Game\SprintSplit\SprintSplit.ini`）

改完重启游戏生效。

| 配置项 | 默认 | 说明 |
| --- | --- | --- |
| `Enabled` | `true` | 总开关，`false` 就完全是原版行为 |
| `UserIndex` | `-1` | `-1` 所有手柄，`0..3` 指定序号 |
| `Log` | `true` | 是否写日志 |
| `SprintButton` | `L3` | 冲刺键，可写 `A B X Y LB RB L3 R3 UP DOWN LEFT RIGHT START BACK GUIDE`，也支持 `LB+RB` 这种组合 |
| `Deadzone` | `0.25` | 左摇杆超过多少算「在移动」 |
| `TapMs` | `30` | 点按时长，也就是「按下 → 翻滚」的固定延迟。`30`≈2 帧，`16`≈1 帧（最跟手），`50` 最稳 |
| `PreGapMs` | `30` | 冲刺中按 B 时，先松开 B 的时长 |
| `PostGapMs` | `30` | 点按结束后、恢复冲刺前的松开时长 |
| `SprintReleaseMs` | `150` | 松开摇杆后冲刺保持多久 |
| `ResumeSprintAfterRoll` | `true` | 冲刺中翻滚后，冲刺键还按着时是否自动恢复冲刺 |
| `SuppressSprintButton` | `true` | 是否把冲刺键从游戏输入里屏蔽（默认 L3 移动中不再触发蹲下） |

### 换成别的冲刺键

例如想用 RB：把 `SprintButton=L3` 改成 `SprintButton=RB`，重启游戏即可，不用重新编译。

> 扳机键（LT / RT）是模拟量，本 mod 只处理按键，不支持扳机。

---

## 六、已知限制

1. **冲刺键和蹲下共用 L3**：站立时按 L3 仍然是蹲下/站起；移动中按 L3 只冲刺、不会蹲。
   想完全避开就去配置里把 `SprintButton` 换成别的键。
2. **轻点冲刺键会变成一次翻滚**：因为点按 < 一帧的 B 会被游戏当成 tap。
   冲刺键请**按住**，不要连点。
3. 只影响 XInput 手柄。DS4 / DualSense 需要先用 DS4Windows 之类转成 XInput。
4. 只改手柄；键盘的按键逻辑不受影响。
5. 游戏内如果改过键位（比如把翻滚挪到别的键），本 mod 不生效 —— 它假设 **B 仍然是后跃/翻滚/冲刺键**。
6. 菜单里那行字仍然显示「后跃、翻滚、冲刺」，那是文本资源，本 mod 不改 UI。
7. 游戏更新不会失效（不依赖版本偏移），但如果哪天游戏改成按名字导入 XInput，
   需要同步改 `src/proxy.def`。

---

## 七、故障排查

| 现象 | 原因 / 处理 |
| --- | --- |
| 启动报「找不到入口点」「0xc0000139」 | DLL 导出序号不对。用 `src\proxy.def` 重新编译，并跑 `check_exports.py` 自检 |
| 进游戏完全没变化 | 看 `Game\SprintSplit\SprintSplit.log` 有没有生成；没有就是 DLL 没被加载（启动的不是这个 exe，或被 EAC 拦了） |
| 手柄完全没反应 | 日志里若有 `cannot load the real XInput1_4.dll`，说明转发失败；否则检查 `UserIndex` |
| 冲刺按了没反应 | 确认 `SprintButton` 拼写正确；必须**推着摇杆**才算移动 |
| 翻滚变慢 / 偶尔不触发 | `TapMs` 调大到 `60~80` |
| 停下摇杆后还在冲刺 | 调小 `SprintReleaseMs` |
| 想临时对比原版 | `Enabled=false`，重启游戏 |

日志示例（正常）：

```
[2026-11-10 21:30:01] SprintSplit starting (SprintSplit.ini loaded), module dir D:\games\ELDEN RING\Game
[2026-11-10 21:30:01] sprint=0x0040 deadzone=0.25 tap=30ms preGap=30ms postGap=30ms release=150ms resume=1 suppress=1 enabled=1
[2026-11-10 21:30:09] first XInputGetState call, pad 0, buttons=0x0000
```

---

## 八、卸载

```bat
uninstall.bat
```

或手动删除：

```
Game\xinput1_4.dll
Game\SprintSplit\
```

---

## 九、关于「翻滚延迟」

游戏是在 **B 松开的那一刻**才判定「这是点按（翻滚）还是按住（冲刺）」的，所以原版翻滚的延迟
= 你按住的时长 + 判定。这个 mod 不能改游戏内部的判定规则，但它把「松开」这件事变得确定：

```
原版：  按下 B ──（你自己决定松手的时刻，通常 80~150ms）──> 松开 ──> 翻滚
本 mod：按下 B ──（固定 TapMs，默认 30ms = 2 帧）──> 合成松开 ──> 翻滚
```

所以：

- **能减、也能稳定**：不管你把 B 按住多久，松开都被固定成 2 帧后发生，不会再「按长了一点就没反应」。
- **调到 `TapMs=16`** 就是 1 帧（约 17ms），基本等价于 Steam 手柄配置里的 **Start Press**，
  是输入层能做到的极限；代价是帧率很低时可能丢输入，`30` 更稳。
- **做不到「按下那一帧就翻滚」**：那需要绕过游戏的判定直接请求翻滚动作（要 hook 游戏的动作/行为逻辑，
  属于另一类 mod），输入层做不到。
- 额外收益：B 不再兼任冲刺，冲刺中想翻滚时不用「先松手再按」，少了一次输入往返。

---

## 十、用 GitHub Actions 云端编译（本机没有编译器时）

GitHub 的 `windows-latest` 运行器自带 Visual Studio 2022 + Windows SDK，等于免费借一台
装好 MSVC 的 Windows 机器。仓库里已经放好 [.github/workflows/build.yml](.github/workflows/build.yml)，
推到 GitHub 就会自动编译并产出 DLL。

步骤：

1. 在 GitHub 新建一个仓库（public / private 都行，private 每月也有 2000 分钟免费额度）；
2. 把 **`SprintSplit` 文件夹里的内容**（不是文件夹本身）作为仓库根目录推上去：

```bat
cd /d "D:\games\ELDEN RING\SprintSplit"
git init
git add .
git commit -m "SprintSplit"
git branch -M main
git remote add origin https://github.com/<你的用户名>/<仓库名>.git
git push -u origin main
```

3. 打开仓库的 **Actions → build-sprintsplit**，workflow 会自动跑（也可以在页面点 *Run workflow*）；
4. 跑完后在这次的 run 页面底部下载 **Artifacts → SprintSplit-xinput1_4.zip**，里面就是
   `xinput1_4.dll` + `SprintSplit.ini`；
5. 解压后把 `xinput1_4.dll` 放进 `Game\`，`SprintSplit.ini` 放进 `Game\SprintSplit\`（或直接跑 install.bat）。

workflow 里除了编译，还会自动跑两步自检：

- `check_exports.py`：确认导出了游戏需要的 **序号 2 / 3**（少了游戏会启动失败）；
- `selftest_dll.py`：真的 `LoadLibrary` 这个 DLL、按序号取函数、调一次 `XInputGetState`。

> 注意：下载 Artifacts 需要登录 GitHub；产物默认保留 90 天。
> 用 MSVC 编译时是 `/MT` 静态链接 CRT，装到游戏目录后不需要额外装 VC++ 运行库。

---

## 十一、开发笔记

- `tools/simulate.py` 是 `src/sprint_logic.h` 的逐行 Python 镜像，改逻辑时两边一起改，
  先跑仿真看时序，再编译，比反复进游戏试快得多：

```bat
python tools\simulate.py
```

- 关键时序（60 帧）：`B down 200ms → B up 250ms` 表示一次 50ms 的点按；
  冲刺中按 B 会看到 `up(200) → down(250) → up(300) → down(350)` 这样一串，
  中间 50ms 的 down 才是给游戏看的翻滚点按。
- 游戏按序号导入的信息来自 `tools/pe_imports.py ... xinput`，系统 DLL 的导出表来自
  `tools/pe_exports.py C:\Windows\System32\XInput1_4.dll`。
