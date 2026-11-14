# 整数、布尔值和字符串传递检查

检查日期为2026年11月14日。目标是查找已有FMU案例并验证两个实例之间的数据传递。

## 案例查找结果

已读取“测试案例库(1)”中的22份FMU和“base_example(1)/base_example”中的10份FMU的modelDescription.xml。这32份文件的输入输出接口中，未找到Integer、Boolean或String类型。部分模型含有这些类型的内部变量或参数，无法直接用它们构成所需的输入输出连接。

在同步 nova723 之前的仓库 data 目录的46份FMU中，找到以下候选。

| 文件 | 接口 | 处理结果 |
| --- | --- | --- |
| data/fmus/1.0/identity.fmu | Integer、Boolean、String各有一个输入和输出 | 用两个实例验证数据传递 |
| data/fmus/1.0/mass_spring_damper下的Mass、Spring、Damper | 含非实数输入，缺少对应非实数输出 | 未选作本次完整传递案例 |
| data/fmus/3.0/ref/Stair.fmu | Int32输出counter | 缺少对应类型输入，未选用 |

identity是已有的FMI 1.0 Co-Simulation模型，包内包含win64和linux64动态库。本次执行使用Windows 64位版本。其模型说明声明输出等于输入，模型内部相应输入输出共享valueReference；测试使用source和sink两个独立实例，并检查source输出、sink输入和sink输出。

FMU SHA256为 `39e4a189f67e47b0863bbffcb8c330d3b603c8b62299bd5fef7930b2accc94a2`。

盘点记录保存在 `results/regression/type-transfer-20261114/inventory.json`。同时扫描了nova723工作副本，找到相同候选，该副本中另有两份nodes_input/output文件无法作为ZIP读取。当前团队仓库的对应文件可以读取。本结论基于实际成功解析的模型和已执行案例。

## nova723实际测试

使用nova723随包的libnovac.dll，版本v0.1.0，SHA256为 `338a4a9d62cee358c9abb5d5b09d104d2c048b3157e1b9cb78b639b1cfc89713`。通过其原有的整数、布尔值和字符串连接及读写接口运行。

在每次0.1秒通信步之前，脚本向source输入写入测试值，步进后读取source输出、sink输入、sink输出。共执行6步，并分别运行连接组和断开连接的对照组。

| 类型 | 测试输入 | 连接组结果 |
| --- | --- | --- |
| Integer | 7、-3、0、2147483647、-2147483648、42 | 6次读取均与输入精确一致 |
| Boolean | true、false、true、true、false、true | 6次读取均与输入精确一致 |
| String | hello、空字符串、含空格文本、中文、含逗号和引号文本、256个x | 6次读取均与输入精确一致 |

断开连接的对照组中，source输出随测试值变化，sink输入和输出保持初始化值。两组共保存36条逐类型记录，全部符合预期。

本仓库已于2026年11月14日同步nova723源码和配套运行库。从仓库根目录运行：

```powershell
python regression/check_type_transfer.py
```

默认使用本仓库，不再依赖另一个nova723工作副本。如需检查其他副本，可传入 `--nova-root`。脚本会输出独立结果目录，保存connected.json、disconnected.json、两份日志及summary.json，包括模型与运行库哈希。当前脚本按nova723的类型接口测试，若传入不兼容的运行库，会记录错误并返回失败。

同步后在本仓库重新运行，两组仍全部通过，记录位于 `results/regression/type-transfer-20261114-184230-646886/`。同步前另一工作副本的通过记录位于 `results/regression/type-transfer-20261114-183215-800972/`。这些结果目录保留在本机，由Git忽略；其他电脑运行命令可生成自己的记录。两组日志没有异常输出。该结论适用于所记录的模型、运行库和输入序列。

## 同步前旧运行库的对照（历史记录）

同步前的提交 `3defbd0` 使用libnova_simc.dll v0.5.8，与上述nova723运行库不同；现已换成nova723随包库。以下记录描述的是旧库。使用该库原有的make_connection入口、场景XML写入source输入，再通过原生CSV观察source和sink。场景在0秒写入7、true和hello，在0.2秒写入-3、false和world。另有7.5到-3.5的实数信号作为对照。使用相同identity模型，分别执行连接组和断开组，各运行到0.5秒。

| 类型 | 实际观察 | 结论 |
| --- | --- | --- |
| Real | 连接组sink跟随source变化，断开组sink保持0 | 该对照传递成功 |
| Integer | 两组source均先为7、后为-3，连接组sink始终为0 | 本次传递测试失败 |
| Boolean | 原生CSV中没有对应输入输出列，Python接口也没有类型读取方法 | 该运行入口尚未完成传递验证 |
| String | 旧版源码的通用连接函数只分派real/int/bool，CSV中也无字符串列 | 该运行入口尚未完成传递验证 |

运行过程中有临时目录清理警告，进程返回0。缺少CSV列本身不足以确认FMU不支持相应类型。整数结果与连接API源码的预期不符，具体原因仍需结合该DLL的实际构建版本进一步定位。

复现用probe_team.py、scenario.xml、两组CSV和日志，以及库哈希和读取结果，均保存在 `results/regression/type-transfer-20261114/`。旧库的这些观察不能替代nova723的通过记录，也不能将nova723的结果用于宣称旧库已通过三类数据测试。

## 对接结论

已有identity模型足以开展基础数据传递检查，nova723下的三种类型已经实际验证。如果本任务要求FMI 2.0模型，或要求物理学院提供的专用验收案例，再请案例提供方构建相应模型。可请对方提供同类型输出到输入的两模型连接、随时间变化的输入序列及预期接收值，整数应覆盖正负和0，布尔值应发生切换，字符串应至少覆盖非空和空值。

当前分支已经在nova723配套库上完成这三类基础传递检查。结果限于上述FMI 1.0模型、Windows运行环境和测试序列，尚不代表其他FMI版本或所有模型均已验证。
