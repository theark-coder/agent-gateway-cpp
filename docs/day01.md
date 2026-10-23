# Day 1 编译/链接和 CMake 记录

- 摸底：4/5；错题：悬空引用/对象生命周期。
- 编译：cpp -> 预处理 -> 编译 -> 汇编 -> .o/.obj
- 链接：对象文件 + 库 -> 可执行文件；找不到定义可能出现 undefined reference。
- CMake 配置：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- 构建：`cmake --build build --parallel`
- 测试：`ctest --test-dir build --output-on-failure`
- 干净重建：`cmake -S . -B build-clean -DCMAKE_BUILD_TYPE=Debug` 然后 build、ctest。

## 待自行补充
- 自己的配置、编译、测试日志：
- Git commit：
- 口述评分（>=7/10）：
