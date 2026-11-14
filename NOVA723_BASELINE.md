# nova723 基准更新

2026年11月14日，将本分支的程序基准由团队 GitHub 旧代码切换为团队提供的 nova723 文件包。旧提交 `3defbd0` 保留在 Git 历史中。本次没有改写历史，也没有更新上游团队仓库。“nova723”指本次收到的文件包，不代表已确认团队没有其他更新。

## 同步范围

- 同步原包的源码、头文件、构建文件、依赖、示例、测试及 Python 接口；这些程序文件与原包逐文件比对一致。
- 一并放入原包的 `libnovac.dll`、`libnovac.so`、`libtbb.so.12` 和 `nova.exe`。Windows Python 接口现在加载 `libnovac.dll`，不再加载旧的 `libnova_simc.dll`。
- 移除同步目录内原包没有的旧源码，以及旧的 `Inject/`、`nova_example/`、`tool/` 实现。未纳入 Python 缓存和运行库备份文件。
- 保留回归脚本、案例配置和已有数据；将回归脚本接回 nova723 的 `NovaEngine` 及类型连接接口。独立的类型传递脚本默认使用本仓库。
- 保留原仓库许可证及历史说明资料。README 增加本更新入口；`.gitignore` 使用原包规则并补充本地案例数据、备份文件的忽略规则，允许跟踪配套 Linux 运行库。

Windows 运行库自身报告版本为 `v0.1.0`，SHA256：

```text
338a4a9d62cee358c9abb5d5b09d104d2c048b3157e1b9cb78b639b1cfc89713
```

不能用这个版本号与旧库的 `v0.5.8` 大小直接判断先后；本次以收到的 nova723 文件包及文件哈希确定来源。

## 数据文件例外

原包的 `data/fmus/2.0/nodes_input.fmu` 和 `nodes_output.fmu` 无法作为 ZIP 读取，因此保留此前仓库内可读取的对应 FMU，没有用损坏文件覆盖。其他随包数据按原包同步。这两份 nodes 模型未列入此次运行验证。

`data/regression/` 中的物理案例 FMU 和参考 CSV 仍为本地输入，不随此次提交上传。其他电脑运行 case1、case3、case5 前，需要按 [回归说明](regression/README.md) 准备这些文件。三类数据传递检查使用仓库自带的 identity FMU，无需这些外部输入。

## 验证记录

验证环境为 Windows 64 位、Python 3.14.7，使用原包运行库，未重新编译 C++。

| 检查 | 结果 |
| --- | --- |
| 回归脚本单元检查 | 18 项通过 |
| case1、case3、case5 | 运行完成并比较参考数据；输出 CSV 和逐点比较 CSV 与最初 nova723 工作副本的记录逐字节一致；共生成 7 张比较图 |
| case2、case4、temperature | 运行完成并保存数据；没有参考数据，未作数值通过判定 |
| Integer、Boolean、String | 两实例连接组和断开连接对照组共 36 条逐类型记录，全部符合预期 |

case1、case3、case5 的容差仍为空，只报告差异，不宣称验收通过。case2、case4 的数值合理性仍未核实。三类传递检查覆盖的是 FMI 1.0 identity 模型；详见 [数据传递检查](regression/DATA_TRANSFER_CHECK.md)。

本机的批量记录位于 `results/regression/20261114-184244-101781/`，类型传递记录位于 `results/regression/type-transfer-20261114-184230-646886/`。结果目录由 Git 忽略，其他电脑可按说明重新生成。

本次未验证 Linux 运行，也未验证源码重新编译后的程序；随包 Linux 依赖及构建配置保持原样。
