# .mcpk 包格式说明

`.mcpk` 是标准 ZIP 文件（`application/zip`），可用 7-Zip / WinRAR / Windows 资源管理器直接打开。ValiaCraftLauncher 只额外依赖根目录的 `manifest.json` 来描述包内容。

## 目录布局

```
manifest.json
PCL/Setup.ini                          # 可选：全局 PCL 配置模板
versions/<versionId>/<versionId>.json  # 版本 json（必需，否则无法识别版本）
versions/<versionId>/<versionId>.jar   # 客户端 jar（如有）
versions/<versionId>/mods/...          # 实例文件（模组、配置、存档等）
versions/<versionId>/PCL/Setup.ini     # 可选：版本级 PCL 配置模板
runtime/<runtimeName>/bin/java.exe     # 可选：内嵌 Java 运行时
libraries/...                          # 可选
assets/...                             # 可选
```

`runtime/<runtimeName>` 由打包时的“包内目录名”决定，默认 `mcpacker-java-<主版本>`（如 `mcpacker-java-17`）。

## manifest.json

```json
{
  "format": "mcpacker",
  "formatVersion": 1,
  "tool": "ValiaCraftLauncher 0.1.0",
  "createdAt": "2026-11-24T13:22:00+08:00",
  "version": {
    "id": "GTL十周目",
    "mcVersion": "1.20.1",
    "loader": "Forge",
    "loaderVersion": "47.4.16",
    "type": "release",
    "releaseTime": "2023-06-12T21:25:51+08:00",
    "javaMajor": 17,
    "isolated": true
  },
  "java": {
    "included": true,
    "relativePath": "runtime/mcpacker-java-17",
    "exeRelative": "runtime/mcpacker-java-17/bin/java.exe",
    "major": 17,
    "fullVersion": "17.0.21",
    "vendor": "Azul Systems, Inc.",
    "arch": "amd64",
    "originalPath": "C:/Users/xxx/.jdks/zulu17.0.21/bin/java.exe"
  },
  "options": {
    "isolation": true,
    "includeVersion": true,
    "includeLibraries": false,
    "includeAssets": false,
    "includeInstanceFiles": false,
    "includePclConfig": true
  },
  "sourceGameDir": "D:/PCL-latest/.minecraft",
  "fileCount": 4139,
  "totalBytes": 673380677
}
```

### 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `format` | string | 固定 `mcpacker`，缺省时本工具进入兼容模式（按普通 ZIP 结构解包） |
| `formatVersion` | int | 当前为 `1` |
| `tool` | string | 生成工具及版本 |
| `createdAt` | string | ISO-8601 创建时间 |
| `version.id` | string | 版本目录名（解包后可被 `--version-name` 覆盖） |
| `version.mcVersion` | string | 原版版本号 |
| `version.loader` | string | `Vanilla` / `Forge` / `NeoForge` / `Fabric` / `Quilt` / `OptiFine` / `LiteLoader` |
| `version.loaderVersion` | string | 加载器版本 |
| `version.type` | string | `release` / `snapshot` / … |
| `version.javaMajor` | int | json 声明或按原版版本推断的 Java 主版本 |
| `version.isolated` | bool | 打包时 PCL 的版本隔离状态 |
| `java.included` | bool | 是否携带 Java 运行时 |
| `java.relativePath` | string | 包内 Java 根目录 |
| `java.exeRelative` | string | 包内 java.exe 的相对路径（`bin/java.exe`） |
| `java.major` | int | Java 主版本 |
| `java.fullVersion` | string | 完整版本号，如 `17.0.21` |
| `java.vendor` / `java.arch` | string | 厂商 / 架构，仅用于展示 |
| `java.originalPath` | string | 打包机上的原始路径，仅供参考 |
| `options.*` | bool | 打包时选择的内容，解包界面据此展示 |
| `fileCount` / `totalBytes` | int | 原始文件数与字节数（不含 manifest 自身） |

## 占位符

包内的 PCL 配置模板里使用两个占位符，解包时替换为实际路径：

| 占位符 | 替换为 |
| --- | --- |
| `{{GAME_DIR}}` | 目标 `.minecraft` 目录（Windows 原生分隔符） |
| `{{JAVA_HOME}}` | 解包后 Java 根目录（`<目标>/.minecraft/runtime/<名称>`） |

## 版本 json 的兼容处理

解包时若指定了 `--version-name`（或界面里填写了新的版本名），工具会：

1. 将 `versions/<旧>/…` 的条目重映射到 `versions/<新>/…`；
2. 把 `<旧>.json` / `<旧>.jar` 重命名为 `<新>.json` / `<新>.jar`；
3. 改写 json 内的 `id` 字段为新版本名。

## 兼容模式

若压缩包**没有** `manifest.json`，但包含 `versions/<id>/<id>.json` 结构，本工具仍可解包：

- 版本名取自目录名；
- 不会写入内嵌 Java 路径（包内也没有 java 信息）；
- 仍然会按版本目录内容写入 PCL 版本级配置（隔离等）。
