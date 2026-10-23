# agent-gateway-cpp — Day 1 最小可运行基架

C++17 / CMake / CTest，无第三方依赖，默认纯本地可构建。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/agent_gateway
```

Windows 原生多配置生成器（如 Visual Studio）请使用：
```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\agent_gateway.exe
```

练习：阅读 3 个 target（agent_core、agent_gateway、agent_tests）；修改 health_message 文本与测试预期，重建测试。
