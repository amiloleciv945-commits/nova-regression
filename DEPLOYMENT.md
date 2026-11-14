# NOVA 部署说明

日期：2026-07-24

项目路径：`/home/cai/project/nova723`

本文档基于 `/home/cai/project/nova/DEPLOYMENT.md` 的流程，并按当前源码布局和实际构建结果进行了少量调整。

## 环境要求

本次部署使用以下环境完成验证：

- Ubuntu Linux x86_64
- CMake 3.19 或更高版本
- GCC/G++ 11.4
- Python 3
- `make`、`unzip`
- Python 验证和绘图需要 `pandas`、`matplotlib`

项目依赖的 `fmi4c`、Catch2 和 oneTBB 已包含在源码树中。正常部署不需要下载依赖。

## 部署命令

在项目根目录执行：

```bash
cd /home/cai/project/nova723
cmake -S . -B build \
  -DNOVA_BUILD_EXAMPLES=ON \
  -DNOVA_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake --install build --prefix deploy
```

这里使用 `NOVA_BUILD_TESTS=OFF`，因为测试配置会尝试从 GitHub 下载 Reference-FMUs。在无法访问该下载地址的环境中，下载文件为空，CMake 会在解压阶段失败。关闭测试不影响库、Python 绑定和示例程序的构建与安装。

如果网络可以正常访问 GitHub，可在全新的构建目录中改用：

```bash
cmake -S . -B build-tests \
  -DNOVA_BUILD_EXAMPLES=ON \
  -DNOVA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --parallel 2
ctest --test-dir build-tests --output-on-failure
```

## 部署产物

构建目录中的主要产物：

```text
build/bin/bouncing_ball
build/bin/controlled_temperature
build/bin/spring_mass_damper
build/bin/libnovac.so
build/bin/libtbb.so.12
```

Python 运行时依赖：

```text
nova_sim_py/binaries/libnovac.so
nova_sim_py/binaries/libtbb.so.12
```

安装目录中的主要产物：

```text
deploy/lib/libfmi4c.a
deploy/lib/libnova.a
deploy/lib/libnovac.so
deploy/lib/libtbb.so.12
deploy/include/
deploy/share/fmi4c/
deploy/share/nova/
```

## 为部署所做的最小修改

### TBB 链接和运行路径

`thirdparty/oneTBB/lib/intel64/gcc4.8/libtbb.so` 与 `libtbb.so.12` 是文本占位文件，不能由 Linux 链接器直接使用。真实 ELF 库为：

```text
thirdparty/oneTBB/lib/intel64/gcc4.8/libtbb.so.12.16
```

因此 CMake 现在：

- 显式链接 `libtbb.so.12.16`；
- 将真实库复制为 `build/bin/libtbb.so.12`；
- 将真实库复制为 `nova_sim_py/binaries/libtbb.so.12`；
- 安装真实库为 `deploy/lib/libtbb.so.12`；
- 使用 `$ORIGIN`，让可执行文件和共享库优先从自身目录加载 TBB。

### C API 标准头文件

当前 C API 使用了 `size_t`、`std::strcpy` 和 `std::strncpy`，但缺少相应标准头文件。为解决编译错误，仅补充：

```cpp
#include <stddef.h>
#include <cstring>
```

未修改 C API 或仿真业务逻辑。

## 验证

### 安装库依赖

```bash
ldd deploy/lib/libnovac.so
```

关键结果：

```text
libtbb.so.12 => /home/cai/project/nova723/deploy/lib/libtbb.so.12
libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6
```

安装后的 `libnovac.so` 使用以下运行路径：

```text
$ORIGIN
```

因此部署目录不依赖源码树中的 TBB 占位文件。

### Python 绑定

```bash
python3 -c 'from nova_sim_py.lib import NovaLib; print(NovaLib.version())'
```

验证结果：

```text
v0.1.0
```

### C++ 示例

```bash
build/bin/controlled_temperature
```

验证结果：

- 进程退出码为 `0`；
- 仿真正常初始化并终止；
- 生成 `results/controlled_temperature.csv`；
- 无图形桌面的环境使用 Matplotlib 非交互后端，出现窗口不可显示警告不影响仿真结果。

## 已知限制

- `bouncing_ball` 当前实际引用 `data/fmus/test/` 下的两个 Hydraulic FMU。这两个 FMU 只包含 `binaries/win64`，不包含 `binaries/linux64`，因此在 Linux 上会报告 FMU 共享库缺失。该问题属于示例数据的平台兼容性，本次未修改示例或 FMU。
- 本次未运行完整测试套件，原因是 Reference-FMUs 下载在当前环境中不可用；库构建、安装、动态依赖、Python 加载和一个含 Linux FMU 的 C++ 示例均已验证。
- 首次运行绘图时，如果用户的 Matplotlib 配置目录不可写，Matplotlib 会自动使用 `/tmp` 下的缓存目录。这不会影响部署和仿真。
