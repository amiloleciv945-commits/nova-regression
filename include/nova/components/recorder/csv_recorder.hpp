
#ifndef NOVA_CSV_RECORDER_HPP
#define NOVA_CSV_RECORDER_HPP

#include "nova/components/recorder/engine_observer.hpp"
#include "nova/engine/variable_identifier.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace nova
{

struct csv_config
{

    void load(const std::filesystem::path& configPath);

    void register_variable(variable_identifier v);

    void clear_on_reset(bool flag);

    size_t& decimation_factor();

    [[nodiscard]] bool shouldLogInstance(const std::string& instanceName) const;

    [[nodiscard]] bool shouldLogVariable(const std::string& variableName) const;

    void verify(const std::vector<variable_identifier>& ids) const;

private:
    bool clear_on_reset_{true};
    size_t decimationFactor_ = 1;
    std::vector<variable_identifier> variable_register;

    csv_config() = default;

    friend class csv_recorder;
};

class csv_recorder : public engine_observer
{

public:
    explicit csv_recorder(const std::filesystem::path& path);

    csv_config& config()
    {
        return config_;
    }

    void pre_init(nova_engine& sim) override;

    void post_init(nova_engine& sim) override;

    void post_step(nova_engine& sim) override;

    void post_terminate(nova_engine& sim) override;

    void on_reset() override;

    [[nodiscard]] std::filesystem::path output_path() const
    {
        return path_;
    }

private:
    bool headerWritten{false};
    csv_config config_;
    std::ofstream outFile_;
    std::filesystem::path path_;

    void write_header(const nova_engine& sim);
    void write_row(const nova_engine& sim);
};

} // namespace nova

#endif // NOVA_CSV_RECORDER_HPP
