# 从源码构建 / Build from source

Windows 10/11 x64，CMake 3.24+、VS 2022 C++ 工具和 Windows SDK。先运行固定依赖下载脚本，再构建和测试：

```powershell
.\tools\bootstrap-deps.ps1
cmake -S . -B build/msvc -A x64
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure
```

Build on Windows with CMake 3.24+, VS 2022 C++ tools and Windows SDK. The bootstrap script fetches pinned dependencies. No Node.js background runtime is bundled.

如果文档指定的便携 LLVM 工具链已就绪，也可执行 / With the documented portable LLVM toolchain already present:

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
```

依赖及测试 / Dependencies and validation: [dependencies](dependencies.md), [validation](validation.md)。
