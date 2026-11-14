# NOVA 回归案例脚本

脚本调用当前仓库的 `nova_sim_py`，逐个运行 FMI 2.0 Co-Simulation 案例，保存输出 CSV、日志和汇总报告。每个案例使用独立进程，失败或超时后继续运行其余案例。提供参考 CSV 时，脚本还会记录逐点差异和误差汇总。

## 运行

在 VS Code 打开本仓库根目录，使用 64 位 Python 在终端运行：

```powershell
python regression/run.py --list
python regression/run.py --case case1 --case case3 --case case5
```

`--case` 可重复提供，省略时运行目录中的全部案例配置。当前在 Windows 上使用仓库内的 `nova_sim_py/binaries/libnovac.dll` v0.1.0（nova723 随包版本） 和 64 位 Python 3.14.7 验证。脚本使用 Python 标准库。绘图需要 Windows PowerShell 5.1 与 .NET 图表库，绘图失败时仍保留数值比较和 `plot.log`。

## 准备案例文件

将团队提供的 FMU 和参考 CSV 放到配置里写明的位置。项目首次克隆后需要另外准备下面的本地输入。三组重点案例中的 FMU 和 CSV 来自 `base_example(1)/base_example`，分别使用对应案例目录下的文件。

| 案例 | 文件目录 | 需要的文件 |
| --- | --- | --- |
| case1 | `data/regression/case1_bundle_20261113/` | `base_example_case1_fmu1.fmu`、`base_example_case1_fmu2.fmu`、`case1.csv` |
| case3 | `data/regression/case3_revised_20261112/` | 修订版 `base_example_case3_fmu1.fmu`、`base_example_case3_fmu2.fmu`、`case3.csv` |
| case5 | `data/regression/case5_bundle_20261113/` | `co_simulation_example_Hydraulic_HydraulicActuator.fmu`、`co_simulation_example_Hydraulic_mass_spring.fmu`、`case5.csv` |
| case2 / case4 | `data/regression/case2/`、`data/regression/case4/` | 原案例库中对应目录的两份 FMU，具体文件名见各自配置 |
| temperature | `data/fmus/2.0/20sim/` | 仓库自带的 `ControlledTemperature.fmu` |

case1、case3 的步长为 0.002 秒，case5 的步长为 0.0001 秒，均运行 1 秒。case2、case4 保留用于后续核实。库版本、模型和参考文件哈希会写入每次运行记录。

## 查看结果

终端会打印 `results/regression/日期时间/` 下的结果目录。打开其中的 `report.md`，在 VS Code 中按 `Ctrl+Shift+V` 预览。`summary.csv` 汇总案例状态；每个案例目录包括：

- `case.json`、`result.csv`、`result.json` 和 `run.log`，分别记录配置、时序数据、状态及运行日志。
- 配置参考 CSV 时，还会保存 `reference.csv`、`comparison.csv`、`comparison.json`。Windows 绘图成功后可查看 `comparison-*.png`。

退出码 `0` 表示所选案例均通过配置的数值比较，`1` 表示至少有失败或超时，`2` 表示仍有已运行但精度待判定或日志需检查的案例。对于「运行完成，日志需检查」，打开对应的 `run.log`。旧运行库试跑曾记录 FMU 临时目录清理警告；case5 还可能记录液压模型的低压提示。具体以本次日志为准。数值比较及其状态仍可在结果中查看。

## 增加案例

在 `regression/cases/` 下按现有 JSON 添加配置。路径相对于仓库根目录解析。当前支持 FMI 2.0 Co-Simulation、从 0 开始的固定步长，以及实数变量连接、读取和初始化参数。FMU 动态库需匹配运行平台。

| 字段 | 用途 |
| --- | --- |
| `id`、`name` | 案例标识与名称，CLI 使用配置文件名选择案例 |
| `source`、`settings_source` | 模型、连接与仿真设置的出处 |
| `models` | 实例名与 FMU 路径 |
| `connections` | 从源输出到目标输入，以 `实例::变量` 表示 |
| `parameters` | 可选的初始化实数参数，例如 `{"fmu1::mass2a.m": 1.1}` |
| `step_size`、`steps` | 通信步长与步数，仿真时长为两者乘积 |
| `outputs` | 要保存的实数变量 |
| `timeout_seconds` | 案例总超时时间，单位秒 |
| `reference` | 参考 CSV 配置，可以设为 `null` |

参考 CSV 配置中的 `file`、`source` 和可选的 `sha256` 记录文件及来源；`time_column` 指定参考时间列。`variables` 可选择 `outputs` 的子集，`columns` 将输出变量名映射到 CSV 列名。`strict` 要求时间网格一致；`actual_times` 允许参考数据更密，但必须覆盖每个实际采样点。脚本按时间进行匹配，无插值和平移。

`atol` 和 `rtol` 同时为空时只计算误差；同时提供非负数后，按 `abs(本次值 - 参考值) <= atol + rtol * abs(参考值)` 判定。当前重点案例的容差均留空。

## 检查脚本

```powershell
python -B -m unittest discover -s regression -p 'test_*.py' -v
```

`regression/` 中的代码、配置及说明用于版本管理。`data/regression/` 中的本地输入和 `results/regression/` 中的输出由 Git 忽略。

## 整数、布尔值和字符串传递

```powershell
python regression/check_type_transfer.py
```

该独立脚本使用仓库自带的 FMI 1.0 identity 模型，在当前仓库的 nova723 运行库上验证两个实例之间的三类传递，并运行断开连接的对照组。退出码 0 表示两组检查均通过，1 表示失败。默认不需要另一个工作副本，也不需要补充案例文件。详细范围见 [DATA_TRANSFER_CHECK.md](DATA_TRANSFER_CHECK.md)。
