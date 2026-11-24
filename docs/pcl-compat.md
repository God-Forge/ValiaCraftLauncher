# PCL2 配置兼容说明

## 格式

PCL2（Plain Craft Launcher 2）的配置文件不是常见 ini：

- 分隔符是**英文冒号** `:`，不是等号：`VersionArgumentIndie:1`；
- 编码 UTF-8（无 BOM），换行 CRLF；
- 空行、无冒号行会被忽略；同一键出现多次时后者覆盖前者。

本工具使用 `PclSettings` 类读写，写回时保持键顺序并合并已有内容。

## 文件位置

| 文件 | 作用 |
| --- | --- |
| `<.minecraft>/PCL/Setup.ini` | 全局设置（启动文件夹等） |
| `<.minecraft>/versions/<版本>/PCL/Setup.ini` | 版本级设置（版本隔离、加载器、内存、Java、服务器验证等） |

## 本工具写入的键

> 以下键名来自对真实 PCL2 2.10.x 生成的 `Setup.ini` 取样。

### 全局 `PCL/Setup.ini`

| 键 | 写入值 | 说明 |
| --- | --- | --- |
| `LaunchFolderSelect` | `D:\目标\.minecraft\` | 启动文件夹，值末尾带反斜杠 |

### 版本级 `versions/<版本>/PCL/Setup.ini`

| 键 | 写入值 | 依据 |
| --- | --- | --- |
| `VersionArgumentIndieV2` | `True` / `False` | 真实样本中存在，新格式的版本隔离开关 |
| `VersionArgumentIndie` | `1` / `0` | 真实样本中存在，旧格式隔离开关（保持两者一致） |
| `VersionVanilla` | 不写（保留原值） | 真实样本中为 PCL 内部格式（如 `20.0.1`），为避免破坏解析不覆盖 |
| `VersionVanillaName` | `1.20.1` | 真实样本中存在，版本显示名 |
| `VersionForge` | `47.4.16` | 真实样本 |
| `VersionNeoForge` / `VersionFabric` / `VersionQuilt` | 加载器版本 | 与 `VersionForge` 同族，按加载器写入对应键 |
| `VersionLiteLoader` | `False` | 真实样本 |
| `Info` | `正式版 1.20.1, Forge 47.4.16` | 真实样本：`Info:正式版 1.20.1, Forge 47.4.16` |
| `ReleaseTime` | `2023-06-12 21:25` | 真实样本 |
| `VersionArgumentJava` | `D:\…\runtime\…\bin\java.exe` | 尽力兼容：PCL 版本级“自定义 Java”路径 |

### 不会被覆盖的键

写入时会在已有配置基础上合并，保留以下与打包无关的用户设置：

`VersionServerLogin`、`VersionServerAuthServer`、`VersionServerAuthName`、`VersionServerAuthRegister`、`VersionRamType`、`VersionRamCustom`、`Logo`、`IsStar`、`State`、`LaunchAdvanceJvm` 等。

## 版本隔离

- 打包时读取版本级 `VersionArgumentIndieV2` / `VersionArgumentIndie` 判断隔离状态；文件不存在或两个键都缺失时按 **开启** 处理（PCL2 新版本默认开启版本隔离）。
- 解包时写入相同状态；若打包时版本未隔离，还会额外把 `.minecraft` 根目录下的实例文件（mods/config/saves 等）一起打包回根目录。

## `VersionArgumentJava` 的兼容性说明

PCL2 各版本对“版本级自定义 Java”的键名/取值方式可能略有差异。本工具写入的 `VersionArgumentJava` 是常见写法之一；如果 PCL 未自动采用：

1. 打开 PCL → 版本设置 → 高级设置 → Java 选择；
2. 选择“自定义”，指向 `<.minecraft>\runtime\<名称>\bin\java.exe`。

由于解包后的目录结构与 PCL 的 `runtime/` 约定一致，PCL 的 Java 自动扫描通常也能发现它。

## 解包后的完整流程（PCL 用户）

1. 用 PCL → “启动器设置 → 其他 → 导入整合包”，选择 `.mcpk`（或直接用本工具解包到 `.minecraft`）；
2. PCL 会扫描到 `versions/<版本>` 并显示在版本列表；
3. 启动时 PCL 读取版本级 `Setup.ini`：隔离、Java 路径、加载器版本均已就绪。
