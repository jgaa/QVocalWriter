#pragma once

#include "Model.h"
#include "qvw/LlamaEngine.h"

class GeneralModel final : public Model
{
public:
    GeneralModel(std::string name,
                 std::unique_ptr<Config> && config);
    ~GeneralModel() override;


    ModelKind kind() const noexcept override {
        return ModelKind::GENERAL;
    }

    QCoro::Task<bool> prompt(const QString& text, const qvw::LlamaSessionCtx::Params& params);

protected:
    bool createContextImpl() override;
    bool stopImpl() override;

private:
    std::shared_ptr<qvw::LlamaSessionCtx> session_ctx_;
    std::unique_ptr<Config> config_;
    QString final_text_;
};
