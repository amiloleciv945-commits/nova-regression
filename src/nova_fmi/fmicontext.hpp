
#ifndef NOVA_FMI_FMICONTEXT_HPP
#define NOVA_FMI_FMICONTEXT_HPP

#include "nova_fmi/dll_loader.hpp"
#include "components/util/temp_dir.hpp"

#include <fmi4c.h>

#include <memory>

namespace nova_fmi
{

class fmicontext
{

public:
    explicit fmicontext(fmuHandle* handle, std::unique_ptr<nova::temp_dir> unzippedFmu, std::shared_ptr<dll_loader> loader)
        : fmuHandle_(handle)
        , unzippedFmu_(std::move(unzippedFmu))
        , dll_loader_(std::move(loader))
    { }

    fmicontext(const fmicontext& rhs) = delete;
    fmicontext& operator=(const fmicontext& rhs) = delete;
    fmicontext(fmicontext&& rhs) = delete;
    fmicontext& operator=(fmicontext&& rhs) = delete;

    fmuHandle* get() const { return fmuHandle_; }
    std::shared_ptr<dll_loader> get_dll() const { return dll_loader_; }
    std::filesystem::path get_unzipped_path() const { return unzippedFmu_->path(); }

    ~fmicontext()
    {
        fmi4c_freeFmu(fmuHandle_);
    }

private:
    fmuHandle* fmuHandle_;
    std::unique_ptr<nova::temp_dir> unzippedFmu_;
    std::shared_ptr<dll_loader> dll_loader_;
};

} // namespace nova_fmi

#endif // NOVA_FMI_FMICONTEXT_HPP
