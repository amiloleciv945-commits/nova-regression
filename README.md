> 本分支以团队提供的 **nova723** 为程序基准，保留回归测试脚本。版本来源、验证结果和同步例外见 [NOVA723_BASELINE.md](NOVA723_BASELINE.md)，运行说明见 [regression/README.md](regression/README.md)。

cd /home/.../nova
```
cmake . -B build -DNOVA_BUILD_EXAMPLES=ON -DNOVA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix deploy
```