#pragma once

#include <string>
#include <memory>
#include <string_view>
#include <filesystem>
#include <cassert>
#include <functional>

/*! Only pure interfaces here. The implementations will be in separate libraries.
 */

namespace qvw {

class EngineBase;
class WhisperSessionCtx;
class LlamaSessionCtx;

/*! Parameters for loading the model.
 *
 * Specific engines may extend this struct with their own parameters.
 */
struct EngineLoadParams {
    EngineLoadParams() = default;
    virtual ~EngineLoadParams() = default;
};

/*! Context for a session/operation.
 *
 * Specific engines will define their own context structures.
 */
struct SessionCtx {
    SessionCtx() = default;
    virtual ~SessionCtx() = default;


    /*! Sets a callback function to receive partial text results during processing.
     *
     * Specific engines may implement this method to provide real-time feedback.
     *
     * @param callback Function to be called with partial text results.
     */
    virtual void setOnPartialTextCallback(std::function<void(const std::string&)> callback) = 0;

    /*! Retrieves the full text result after processing.
     *
     * Specific engines will implement this method to return the final output.
     *
     * @return The full text result as a string.
     */
    virtual std::string getFullTextResult() const = 0;
};

/*! Context for a loaded model.
 *
 * Specific engines will define their own context structures.
 */
class ModelCtx {
public:
    ModelCtx() = default;
    virtual ~ModelCtx() = default;

    virtual std::string info() const noexcept = 0;

    virtual const EngineBase& engine() const noexcept = 0;

    virtual EngineBase & engine() noexcept  = 0;

    virtual const std::string& modelId() const noexcept = 0;

    /*! Creates a new Whisper session context for processing.
     *
     * Only applicable for Whisper models.
     *
     * @return Shared pointer to the newly created session context.
     */
    virtual std::shared_ptr<WhisperSessionCtx> createWhisperSession() {
        return {};
    };

    virtual std::shared_ptr<LlamaSessionCtx> createLlamaSession() {
        return {};
    }
};


/* Abstract interface for the engine base.
 *
 * The actual implementations (whisper, llama) will derive from this.
 * They are built independently as separate shared libraries to keep dependencies isolated,
 * and allow different versions of the underlying libraries.
 */

class EngineBase {
public:
    EngineBase() = default;
    virtual ~EngineBase() = default;


    /*! Returns the version string of the underlying engine/library.
     *
     * Example: "whisper.cpp 1.3.0" or "llama.cpp 2.3.1"
     */
    virtual std::string version() const = 0;

    /*! One time initialization of the engine.
     *
     * Must be called before any other methods.
     */
    virtual bool init() = 0;

    /*! Reurns the last error message, if any.
     *
     * If the last operation was successful, returns an empty string.
     */
    virtual std::string lastError() const noexcept = 0;


    /*! Loads the model from the given path with the specified parameters.
     *
     * The returned context is a shared context and can be used by more than one session.
     * Specific engines may impose limitations on concurrent usage.
     *
     * When the shared pointer goes out of scope, the model context is unloaded.
     *
     * @param modelId Identifier of the model being loaded.
     * @param modelPath Filesystem path to the model file.
     * @param params Load parameters specific to the engine.
     *
     * @return Shared pointer to the loaded model context, or nullptr on failure.
     */
    virtual std::shared_ptr<ModelCtx> load(const std::string& modelId, const std::filesystem::path& modelPath, const EngineLoadParams& params) = 0;

    virtual int numLoadedModels() const noexcept = 0;

};

} // ns
