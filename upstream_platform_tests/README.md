这里保留上游 Android/Windows 的原生源码、安装器和版本文档约束测试。
它们会读取 Kotlin、Android XML、Windows C++ 和 Inno Setup 文件，这些工程不属于
Sailfish 应用，因此未放入默认 `test/` 测试集。它们的完整原版仍在参考项目中。

共享业务与界面测试保留在 `test/`，另有 Sailfish MPRIS、凭据与 RPM 配置测试。
本目录中一些文件也含 Android/Windows 平台通道的单元测试；这些未计入 Sailfish
默认测试通过数量。不要把本目录的检查结果视为 SailfishOS 的运行验证。
