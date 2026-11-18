#include "nova/engine/nova_fmu_locator.hpp"

#include "engine/fmi_model.hpp"
#include "nova/components/logger/logger.hpp"

#ifdef NOVA_WITH_PROXYFMU
#include "proxyfmu/proxy_model.hpp"
#endif

using namespace nova;

#ifdef NOVA_WITH_PROXYFMU
namespace
{
std::optional<proxy::remote_info> parse(const std::string& url)
{
    // Find the start of the host (after "proxyfmu://")
    std::size_t start_pos = url.find("://");
    if (start_pos == std::string::npos) {
        return std::nullopt; // Return empty if no protocol found
    }
    start_pos += 3; // Move past "://"

    std::size_t end_pos = url.find('?', start_pos);
    if (end_pos == std::string::npos) {
        end_pos = url.length();
    }

    const std::string path = url.substr(start_pos, end_pos - start_pos);
    if (path.find(':') == std::string::npos) {
        return std::nullopt;
    }

    return proxy::remote_info::parse(path);
}
} // namespace
#endif

std::shared_ptr<model> nova_fmu_locator::locate(const std::filesystem::path& base, const std::string& uri)
{
    std::string key = base.string() + "::" + uri;
    if (cache_.contains(key)) {
        log::debug("Locator cache hit for key {}", key);
        return cache_.at(key);
    }

#ifdef NOVA_WITH_PROXYFMU
    if (uri.rfind("proxyfmu", 0) == 0) {
        const auto find = uri.find("file=", 0);
        if (find == std::string::npos) {
            log::warn("proxyfmu source missing file= component");
            return nullptr;
        }
        const auto fmuFile = base / uri.substr(find + 5);
        std::optional<proxy::remote_info> info = parse(uri);
        auto m = std::make_shared<proxy_model>(fmuFile, info);
        cache_[key] = m;
        return m;
    }
#endif

    const auto fmuFile = base / uri;
    if (std::filesystem::exists(fmuFile) && fmuFile.extension() == ".fmu") {
        auto m = std::make_shared<fmi_model>(fmuFile);
        cache_[key] = m;
        return m;
    }

    log::warn("Unable to locate fmu for uri: {}", uri);
    return nullptr;
}
