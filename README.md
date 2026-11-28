> 本分支以团队提供的 **nova723** 为程序基准，保留回归测试脚本。版本来源、验证结果和同步例外见 [NOVA723_BASELINE.md](NOVA723_BASELINE.md)，运行说明见 [regression/README.md](regression/README.md)。

cd /home/.../nova
```
cmake . -B build -DNOVA_BUILD_EXAMPLES=ON -DNOVA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix deploy
```

## 从 Python 选择联仿算法

`NovaEngine` 的 `algorithm` 参数适用于 `structure` 和 `ssp_path` 两种入口。省略时使用并行固定步长。

```python
from nova_sim_py import NovaEngine

# structure 已添加 source 和 sink，并且已建立模型间的连接。
with NovaEngine(0.01, structure=structure,
                algorithm={"type": "gauss_seidel", "order": ["source", "sink"]}) as engine:
    engine.init()
    engine.step()
    engine.terminate()
```

- 固定步长用 `algorithm="fixed_step"`。串行运行可传入 `{"type": "fixed_step", "parallel": False}`。
- Gauss–Seidel 的 `order` 需要按执行顺序列出每个 FMU 实例名，且每个名字只出现一次。
- 强耦合用 `algorithm="strong_coupling"`，或传入 `{"type": "strong_coupling", "max_iterations": 2}`。该算法按现有 C++ 实现识别力源 `F` / `V_input` 和速度源 `V` / `F_input`。

选择算法需要使用重新编译的 `libnovac`。原有的默认调用方式也适用于旧版运行库。
