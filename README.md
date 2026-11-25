# ValiaCraftLauncher — Minecraft 整合包打包器

**作者：neolxsky** · Copyright © 2026 neolxsky · [GNU GPL v3.0 or later](LICENSE)

用 **C++20 + Qt 6.8** 编写的 Minecraft 整合包打包/解包工具：把某个版本（`versions/<版本>/`）连同配套的 **Java 运行时** 一起打包成单个 `.mcpk` 文件，解包时自动还原到目标 `.minecraft`，并**写入 PCL2（Plain Craft Launcher 2）的配置文件**，让接收方用 PCL 直接启动，无需手动设置 Java 路径和版本隔离。

```
┌──────────────────────────────────────────────────────────────┐
│  ValiaCraftLauncher — Minecraft 整合包打包器                            │
│  ┌─────────┐ ┌─────────┐                                     │
│  │  打包   │ │  解包   │   选择版本 + Java → 生成 .mcpk        │
│  └─────────┘ └─────────┘                                     │
│  游戏目录 / 版本 / Java 运行时 / 打包内容 / 输出               │
│  ──────────────────────────────────────────────────────────  │
│  日志：打包完成 / 已写入 PCL 配置 …                            │
└──────────────────────────────────────────────────────────────┘
```

## 功能特性

- **打包指定版本**：递归收集 `versions/<版本>/` 下的模组、配置、存档、资源包等；自动跳过 `logs/`、`crash-reports/`、`backups/`、启动器缓存等无用文件。
- **携带 Java 运行时**：把选中的 JDK/JRE 整体打包到包内 `runtime/<名称>/`，解包后 PCL 可直接使用；也可选择不打包，交给 PCL 自动选择。
- **PCL 配置写入**（核心特性）：
  - 版本级 `<.minecraft>/versions/<版本>/PCL/Setup.ini`：版本隔离、原版版本、Forge/NeoForge/Fabric/Quilt 版本、版本描述、发布时间、自定义 Java 路径；
  - 全局 `<.minecraft>/PCL/Setup.ini`：`LaunchFolderSelect` 启动文件夹；
  - 严格遵循 PCL2 的 `Key:Value`（英文冒号）格式、UTF-8 编码、CRLF 换行。
- **智能版本识别**：兼容原版、Forge、NeoForge、Fabric、Quilt、OptiFine、LiteLoader，以及 PCL 生成的“合并版” json（识别 `clientVersion`、`--fml.mcVersion`、`--fml.forgeVersion`、`libraries` 等多种来源）。
- **Java 探测**：扫描 PATH、`JAVA_HOME`、常见 JDK 安装目录（Oracle / Adoptium / Microsoft / Zulu / Corretto / GraalVM / Liberica）、`~/.jdks`、`~/.gradle/jdks`、游戏自带 `runtime/`，并通过 `java -XshowSettings:properties -version` 解析版本/厂商/架构。
- **解包选项**：冲突策略（覆盖 / 跳过 / 重命名保留两者）、可重命名版本避免覆盖、自动修正版本 json 的 `id`。
- **零外部依赖**：ZIP 读写（含 Zip64、deflate、UTF-8 文件名）与 PCL ini 处理全部自研，不依赖 7-Zip / zlib 动态库。
- **命令行工具**：`ValiaCraftLauncherCli.exe` 支持脚本化打包/解包与内置自测（见下文）。

## 包格式（.mcpk）

`.mcpk` 就是标准 ZIP（可用 7-Zip、WinRAR、系统解压直接打开），根目录额外带一个 `manifest.json`：

```
manifest.json                 # ValiaCraftLauncher 清单（版本、Java、选项、来源）
PCL/Setup.ini                 # 全局 PCL 配置模板（LaunchFolderSelect 使用 {{GAME_DIR}} 占位符）
versions/<版本>/              # 版本目录（json / jar / mods / config / saves …）
versions/<版本>/PCL/Setup.ini # 版本级 PCL 配置模板（Java 路径使用 {{JAVA_HOME}} 占位符）
runtime/<名称>/bin/java.exe   # 可选：内嵌 Java 运行时
libraries/…  assets/…         # 可选：游戏依赖库与资源
```

> 占位符只用于包内模板，解包时会被替换为实际路径；直接用 PCL 导入 zip 时 PCL 会忽略这些占位符并按自己的规则处理。
> 详细字段见 [docs/mcpk-format.md](docs/mcpk-format.md)。

## PCL 兼容性

PCL2 的配置文件采用 `Key:Value` 格式（**不是** `Key=Value`），UTF-8、CRLF 换行。本工具写入的键均来自对真实 PCL2 `Setup.ini` 的取样：

| 位置 | 键 | 写入值 |
| --- | --- | --- |
| 全局 | `LaunchFolderSelect` | 目标 `.minecraft` 绝对路径（末尾带 `\`） |
| 版本级 | `VersionArgumentIndieV2` | `True` / `False`（是否版本隔离） |
| 版本级 | `VersionArgumentIndie` | `1` / `0`（旧格式兼容） |
| 版本级 | `VersionVanillaName` | 原版版本号显示名，如 `1.20.1`（`VersionVanilla` 为 PCL 内部格式，保留原值不覆盖） |
| 版本级 | `VersionForge` / `VersionNeoForge` / `VersionFabric` / `VersionQuilt` | 加载器版本 |
| 版本级 | `VersionLiteLoader` | `False` |
| 版本级 | `Info` | 版本描述，如 `正式版 1.20.1, Forge 47.4.16` |
| 版本级 | `ReleaseTime` | `yyyy-MM-dd HH:mm` |
| 版本级 | `VersionArgumentJava` | 自定义 Java 路径（指向解包后的 `runtime/.../bin/java.exe`） |

写配置时**合并**已有内容，不会覆盖 `VersionServerAuthServer`、`Logo`、`VersionRamCustom` 等与打包无关的用户设置。

> 说明：`VersionArgumentJava` 是 PCL 版本级“自定义 Java”键的尽力兼容写法。若某些 PCL 版本未识别，可在 PCL 的“版本设置 → 高级设置 → Java 选择”里手动指向 `runtime/.../bin/java.exe`，本工具生成的目录结构完全可用。详见 [docs/pcl-compat.md](docs/pcl-compat.md)。

## 构建

### 环境要求

| 组件 | 版本 |
| --- | --- |
| Qt | 6.8.x（mingw_64 套件） |
| 编译器 | MinGW-w64 GCC 13.1（Qt 自带 `Tools/mingw1310_64`） |
| CMake | ≥ 3.21 |
| 系统 | Windows 10/11（代码本身跨平台，仅构建脚本与 Java 探测针对 Windows 优化） |

### 一键构建

```powershell
# 编译（Release）
.\build.ps1

# 编译 + 部署到项目 dist\（含全部 Qt DLL / 插件 / MinGW 运行库）+ 启动
.\build.ps1 -Deploy -Run

# 指定 Qt 路径 / 清理重编 / 自定义分发目录
.\build.ps1 -QtDir D:\Qt\6.8.3\mingw_64 -Clean -Deploy
.\build.ps1 -Deploy -DistDir D:\ValiaCraftLauncher
```

脚本会自动定位 Qt 与 MinGW；**当项目路径包含中文时**，会自动在 `%LOCALAPPDATA%\ValiaCraftLauncher\src` 建立 junction 并把构建目录放到 `%LOCALAPPDATA%\ValiaCraftLauncher\build`（MinGW make 无法处理非 ASCII 源码路径，这是必要变通）。

部署产物默认在项目的 `dist\` 下，自带全部运行依赖：

```
dist\
├─ ValiaCraftLauncher.exe      # 图形界面（双击运行，无需安装 Qt）
├─ ValiaCraftLauncherCli.exe   # 命令行工具
├─ Qt6Core/Gui/Widgets/Network/Svg.dll
├─ libstdc++-6 / libgcc_s_seh-1 / libwinpthread-1.dll
└─ platforms\ styles\ imageformats\ tls\ networkinformation\ generic\ iconengines\
```

> 请把整个 `dist` 目录一起复制分发，**不要只拿 exe**——Qt 运行库与插件就在旁边。

### 手动构建

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/mingw_64
cmake --build build -j 8
cmake --build build --target deploy     # 可选：在项目的 dist/ 下收集 Qt DLL
```

## 使用

### 图形界面

1. **打包页**
   1. 选择 `.minecraft` 目录（默认自动探测 `%APPDATA%\.minecraft`）；
   2. 在下拉框选择要打包的版本，界面会显示原版版本、加载器、建议 Java 版本与 PCL 隔离状态；
   3. 选择 Java 运行时（默认按版本需求自动匹配），可勾选“把该 Java 运行时一起打进包里”；
   4. 按需勾选 `libraries` / `assets` / 实例根目录文件 / PCL 配置；
   5. 指定输出 `.mcpk` 后点击“开始打包”。
2. **解包页**
   1. 选择或**直接拖入** `.mcpk` / `.zip`，界面会预览包内版本、Java、打包内容；
   2. 选择目标 `.minecraft` 目录；
   3. 选择文件冲突策略；如需保留目标已有同名版本，可在“版本名”里填一个新名字；
   4. 点击“开始解包”，完成后 PCL 配置会自动写好。

### 命令行

```powershell
# 列出某游戏目录下的版本
ValiaCraftLauncherCli.exe --list --game "D:\PCL-latest\.minecraft"

# 打包：版本 + 打包 Java 运行时（自动写入 PCL 配置）
ValiaCraftLauncherCli.exe --pack --game "D:\PCL-latest\.minecraft" --version "GTL十周目" `
  --java "C:\Program Files\Java\jre1.8.0_491\bin\java.exe" `
  --out "D:\out\GTL.mcpk"

# 打包：额外带 libraries/assets，排除实例根目录文件
ValiaCraftLauncherCli.exe --pack --game ... --version ... --libraries --assets --no-instance

# 解包：覆盖冲突文件，并重命名版本
ValiaCraftLauncherCli.exe --unpack --package "D:\out\GTL.mcpk" --target "E:\MC\.minecraft" `
  --policy overwrite --version-name "GTL-副本"

# 内置自测（打包→解包→校验 PCL 配置，约 1 秒）
ValiaCraftLauncherCli.exe --selftest
```

`--policy` 取值：`skip`（默认，保留已有文件）、`overwrite`、`rename`。
`--no-pcl` 可关闭 PCL 配置的写入/打包。

### 启动参数

```
ValiaCraftLauncher.exe --game <路径>        # 打开界面并定位到该游戏目录
ValiaCraftLauncher.exe --unpack <文件>      # 打开界面并预填要解包的整合包
ValiaCraftLauncher.exe --screenshot <png>   # 调试用：截图后退出（配合 QT_QPA_PLATFORM=offscreen）
```

## 项目结构

```
ValiaCraftLauncher/
├─ CMakeLists.txt              # 构建定义（core 静态库 + GUI + CLI + deploy 目标）
├─ CMakePresets.json
├─ build.ps1                   # 一键构建（自动处理中文路径 / 可选代码签名）
├─ LICENSE                     # GNU GPL v3.0 全文（作者 neolxsky）
├─ src/
│  ├─ main.cpp                 # GUI 入口（含 --game/--unpack/--screenshot）
│  ├─ app.rc                   # 图标 + 版本信息（作者 neolxsky）
│  ├─ app.ico / app_icon.png   # 应用图标（由 icon_gen.py 生成）
│  ├─ resources.qrc            # Qt 资源（窗口图标）
│  ├─ icon_gen.py              # 图标生成脚本（Pillow）
│  ├─ core/                    # 与界面无关的核心库
│  │  ├─ Common.h/.cpp         # VersionInfo / JavaInfo / 通用格式化
│  │  ├─ Crc32.h/.cpp          # CRC-32（分段可增量）
│  │  ├─ Deflate.h/.cpp        # raw deflate / inflate（自研 inflate，deflate 走 Qt zlib）
│  │  ├─ ZipWriter.h/.cpp      # ZIP 写入（store/deflate、UTF-8、Zip64）
│  │  ├─ ZipReader.h/.cpp      # ZIP 读取/解压（路径穿越防护、CRC 校验）
│  │  ├─ PclSettings.h/.cpp    # PCL Setup.ini 读写 + 配置写入逻辑
│  │  ├─ MinecraftLayout.h/.cpp# 目录识别、版本枚举、version json 解析（继承链）
│  │  ├─ JavaScanner.h/.cpp    # Java 探测与版本解析
│  │  ├─ Manifest.h/.cpp       # .mcpk 清单
│  │  ├─ Packer.h/.cpp         # 打包流程
│  │  └─ Unpacker.h/.cpp       # 解包流程 + PCL 配置合并
│  └─ ui/                      # Qt Widgets 界面
│     ├─ MainWindow            # 主窗口、菜单、日志布局
│     ├─ PackPage              # 打包页
│     ├─ UnpackPage            # 解包页（支持拖放）
│     └─ LogPanel              # 彩色日志面板
├─ tools/ValiaCraftLauncherCli.cpp       # 命令行 + 自测
└─ docs/
   ├─ mcpk-format.md           # 包格式说明
   └─ pcl-compat.md            # PCL 配置兼容说明
```

## 技术要点

- **自研 ZIP**：`ZipWriter` 采用“逐文件压缩 → 写本地头 → 写数据”的顺序，天然支持进度；`ZipReader` 从尾部搜索 EOCD，兼容 Zip64（大包/大量文件），解压时校验 CRC 并阻断路径穿越。
- **deflate/inflate**：写压缩流使用 Qt 内置 zlib（`qCompress`）并剥离 zlib 头尾得到 raw deflate；读压缩流使用自研 RFC1951 解码器，不依赖 Qt 私有 API。
- **后台线程**：打包/解包/Java 扫描均在 `QThread` 中执行，通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回传进度，界面不卡顿。
- **版本解析兜底链**：`clientVersion` → `--fml.mcVersion` → `libraries` 中的 `net.minecraft:client` → 版本 id → `inheritsFrom` 父链；加载器同理（`libraries` → `--fml.*` → PCL 版本配置 → id 名称）。

## 已知限制

- 目前只处理 Windows 下的 Java 探测（Linux/macOS 可运行但探测范围有限）。
- 单文件压缩在内存中进行，超过 64 MB 的文件自动改为 store（不压缩），超大文件（>2 GB）不保证。
- `.mcpk` 的 `manifest.json` 仅本工具使用；其它启动器（PCL、HMCL 等）导入 zip 时按普通整合包结构处理。
- `VersionArgumentJava` 键为尽力兼容；若 PCL 未识别，请按上文说明在界面里手动选择一次 Java（仅需一次）。

## 许可

本项目以 **GNU General Public License v3.0（或更高版本）** 发布，Copyright © 2026 **neolxsky**，详见 [LICENSE](LICENSE)。

- 你可以自由地使用、修改、分发本程序（包括商用）；
- 分发修改版或二进制时，必须附带完整源代码并保留相同的开源许可；
- 详情以 [`LICENSE`](LICENSE) 中的 GPLv3 全文为准。

### 代码签名（可选）

exe 已内嵌作者版本信息（资源管理器 → 属性 → 详细信息可见「作者：neolxsky」）。如需 **Authenticode 数字签名**，自备代码签名证书后执行：

```powershell
# 证书已安装到当前用户（按指纹）
.\build.ps1 -Deploy -Sign -CertThumbprint <证书指纹>

# 使用 .pfx 文件
.\build.ps1 -Deploy -Sign -CertFile D:\cert\neolxsky.pfx -CertPassword <密码>
```

脚本会自动查找 `signtool.exe`（Windows SDK），使用 SHA-256 + 时间戳服务器完成签名，签名后可用 `signtool verify /pa dist\ValiaCraftLauncher.exe` 校验。
