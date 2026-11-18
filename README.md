> 本分支以团队提供的 **nova723** 为程序基准，保留回归测试脚本。版本来源、验证结果和同步例外见 [NOVA723_BASELINE.md](NOVA723_BASELINE.md)，运行说明见 [regression/README.md](regression/README.md)。

cd /home/.../nova
```
cmake . -B build -DNOVA_BUILD_EXAMPLES=ON -DNOVA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix deploy
```

## Gauss–Seidel 联合仿真

`nova::gauss_seidel_algorithm` 可通过 C++ 的 `simulation_structure::load` 选择。构造参数为统一通信步长（秒）和 FMU 实例的执行顺序。每个实例名必须恰好出现一次。

```cpp
#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"
#include "nova/components/structure/simulation_structure.hpp"

void simulate(nova::simulation_structure& structure)
{
    // structure 已添加并连接 spring、damper 和 mass 实例。
    auto engine = structure.load(std::make_unique<nova::gauss_seidel_algorithm>(
        0.001, std::vector<std::string>{"spring", "damper", "mass"}));
    engine->init();
    engine->step(1000);
    engine->terminate();
}
```

每个宏步先为当前实例采样各条输入连接，再按给定步长推进并读取输出。已执行的上游实例提供本宏步的新输出，尚未执行的上游实例提供上一通信点的输出。每步串行扫描一次，通信步长必须满足所有 FMU 的步长约束。显式耦合的误差和稳定性取决于模型、执行顺序与通信步长。方法见 [Gomes 等人发表的《Co-Simulation: A Survey》](https://doi.org/10.1145/3179993)。

使用 `-DNOVA_BUILD_TESTS=ON` 配置后，可单独构建并运行 Gauss–Seidel 测试。测试使用仓库内置 FMU，覆盖三模型信号链、闭合反馈和有解析解的质量–弹簧–阻尼系统。

```sh
cmake --build build --config Release --target test_gauss_seidel
ctest --test-dir build -C Release -R "^test_gauss_seidel$" --output-on-failure
```
