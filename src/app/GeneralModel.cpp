#include <atomic>
#include <thread>
#include <span>

#include "GeneralModel.h"

#include "ScopedTimer.h"
#include "logging.h"

using namespace std;

namespace logfault {
std::pair<bool /* json */, std::string /* content or json */> toLog(const GeneralModel& m, bool json) {
    return toLogHandler(m, json, "GeneralModel");
}
} // ns

GeneralModel::GeneralModel(std::string name, std::unique_ptr<Config> &&config)
: Model(std::move(name), std::move(config))
{

}

GeneralModel::~GeneralModel()
{
    LOG_DEBUG_EX(*this) << "Destroying instance";
}

QCoro::Task<bool> GeneralModel::prompt(const QString &text, const qvw::LlamaSessionCtx::Params& params)
{
    string prompt_text = text.toStdString();
    auto op = make_unique<Model::Operation>([this, prompt_text, params]() -> bool {
        LOG_DEBUG_EX(*this) << "Prompting GeneralModel with text: " << prompt_text;

        assert(session_ctx_ != nullptr);
        if (!session_ctx_) {
            return failed("Session context is null in prompt");
        }

        final_text_.clear();

        session_ctx_->setOnPartialTextCallback([this](const std::string &partial_text) {
            LOG_DEBUG_EX(*this) << "Received partial text: " << partial_text;
            partialTextAvailable(QString::fromStdString(partial_text));
        });

        ScopedTimer timer;
        const bool result = session_ctx_->prompt(prompt_text, params);

        LOG_INFO_EX(*this) << "Prompt completed in "
                           << timer.elapsed() << " seconds.";

        return result;
    });

    auto future = op->future();

    LOG_TRACE_EX(*this) << "Enqueuing prompt: " << prompt_text;
    enqueueCommand(std::move(op));
    const auto result = co_await future;
    LOG_TRACE_EX(*this) << "TranscribeRecording command completed.";
    final_text_ = QString::fromStdString(session_ctx_->getFullTextResult());
    emit finalTextAvailable(final_text_);
    co_return result;
}

bool GeneralModel::createContextImpl()
{
    LOG_DEBUG_EX(*this) << "Creating a context/session for a loaded Whisper model";
    assert(session_ctx_ == nullptr);
    assert(haveModel());

    auto model_instance = modelInstance();
    if (!model_instance) {
        return failed("Model instance is null in createContextImpl");
    }

    auto ctx = model_instance->modelCtx();

    session_ctx_ = ctx->createLlamaSession();
    if (!session_ctx_) {
        return failed("Failed to create Llama session context in createContextImpl");
    }

    return true;
}

bool GeneralModel::stopImpl()
{
    LOG_DEBUG_EX(*this) << "Stopping GeneralModel session (noop)";
    return true;
}
