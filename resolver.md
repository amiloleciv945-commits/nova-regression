# [模型解析器重构方案 (Model Resolver -> Nova FMU Locator)]

当前代码库中的 `model_resolver` 使用了过度复杂的多态工厂模式（Polymorphic Factory Pattern）。通过 `model_sub_resolver` 接口派生出诸如 `file_model_sub_resolver` 和 `proxy_model_sub_resolver` 等子类，并在 `default_model_resolver` 中进行组装。

根据你的最新要求，我们将彻底废弃 `model_resolver` 这个旧的命名概念，将简化后的核心功能内聚到一个名为 **`nova_fmu_locator`** 的工具类中。

## Proposed Changes (代码修改计划)

### 1. 移除旧的 `model_resolver` 及所有子解析器相关文件

彻底删除多态设计相关的子解析器文件，以及旧的 `model_resolver` 源文件，我们将重新建立新的文件。

#### [DELETE] [file_model_sub_resolver.hpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/file_model_sub_resolver.hpp)
#### [DELETE] [file_model_sub_resolver.cpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/file_model_sub_resolver.cpp)
#### [DELETE] [proxy_model_sub_resolver.hpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/proxy_model_sub_resolver.hpp)
#### [DELETE] [proxy_model_sub_resolver.cpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/proxy_model_sub_resolver.cpp)
#### [DELETE] [url_model_sub_resolver.hpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/url_model_sub_resolver.hpp)
#### [DELETE] [url_model_sub_resolver.cpp](file:///C:/Users/24512/Desktop/eova/src/engine/resolvers/url_model_sub_resolver.cpp)
#### [DELETE] [model_resolver.hpp](file:///C:/Users/24512/Desktop/eova/include/nova/engine/model_resolver.hpp)
#### [DELETE] [model_resolver.cpp](file:///C:/Users/24512/Desktop/eova/src/engine/model_resolver.cpp)

### 2. 创建全新的核心定位器 `nova_fmu_locator`

创建两个全新的文件，吸收被删除的子解析器的所有逻辑，将加载流程直接内联（Inline）。

#### [NEW] `include/nova/engine/nova_fmu_locator.hpp`
- 定义 `nova_fmu_locator` 类。
- 提供 `locate(const std::filesystem::path& base, const std::string& uri)` 方法。
- 保留原本存在于 `model_resolver` 中的缓存机制 (`std::unordered_map<std::string, std::shared_ptr<model>> cache_`)。

#### [NEW] `src/engine/nova_fmu_locator.cpp`
- 实现 `locate` 方法。
- 将原本 `file_model_sub_resolver` 和 `proxy_model_sub_resolver` 中的核心逻辑直接顺序写入。逻辑如下：
  1. 优先检查 `uri` 是否以 `"proxyfmu"` 开头，如果是，执行现有的代理加载逻辑（使用 `#ifdef NOVA_WITH_PROXYFMU` 宏保护）。
  2. 否则，执行普通文件加载逻辑，校验 `.fmu` 后缀及文件存在性。
  3. 如果成功，将返回的 `model` 存入 `cache_` 中；如果均失败，返回 `nullptr` 并记录日志。

### 3. 同步修改外部调用代码

#### [MODIFY] [ssp_loader.cpp](file:///C:/Users/24512/Desktop/eova/src/components/ssp/ssp_loader.cpp)
目前的 `ssp_loader.cpp` 构造方法中使用了旧的 `default_model_resolver` 函数。重构后需要修改为包含新的头文件，并直接实例化 `nova_fmu_locator`：
```cpp
// 替换引用的头文件
#include "nova/engine/nova_fmu_locator.hpp"

// ... 
auto locator = std::make_unique<nova_fmu_locator>();
auto model = locator->locate(desc_.dir(), component.source);
```

#### [MODIFY] [CMakeLists.txt](file:///C:/Users/24512/Desktop/eova/src/CMakeLists.txt)
更新 CMake 构建脚本：
- 从 `nova_fmi` 目标源文件列表中删除 `model_resolver.cpp` 以及各个 `_model_sub_resolver.cpp`。
- 新增 `nova_fmu_locator.cpp` 到编译列表中。
- 如果 `include/nova/engine/` 中的头文件被手动包含在某个 `install` 命令或 GLOB 中，确保 `nova_fmu_locator.hpp` 被正确处理。

## User Review Required

> [!IMPORTANT]
> 核心类已更名为 `nova_fmu_locator`，内部方法为了语义统一也计划从 `resolve` 改为 `locate`。原本依赖多态的子文件被彻底清理。请确认该设计方案及命名。

## Verification Plan
1. 完成修改后执行全量编译（`cmake --build build --config Release`），确保新源文件能被正确编译并且无任何链接错误。
2. 运行 `quarter_truck_ssp.py` 测试文件。它在加载 SSP 阶段会通过 `ssp_loader` 调用我们新的 `nova_fmu_locator`。如果终端正常输出并且最终写入了 CSV，证明我们的重构在逻辑上完美无损地替代了旧的解析系统。
