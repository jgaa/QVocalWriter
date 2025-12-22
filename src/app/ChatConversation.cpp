#include "ChatConversation.h"
#include "ChatMessagesModel.h"

#include "logging.h"

namespace {
template <typename T = std::span<const std::shared_ptr<ChatMessage>>>
void updateModel(ChatMessagesModel *m,
                 const T& messages,
                 ChatMessagesModel::Updates u) {

    if (m != nullptr) {
        std::vector<const ChatMessage *> msg_ptrs;
        msg_ptrs.reserve(messages.size());
        for (const auto &msg : messages) {
            msg_ptrs.push_back(msg.get());
        }
        m->setMessages(std::span<const ChatMessage *>{msg_ptrs.data(), msg_ptrs.size()}, u);
    } else {
        LOG_WARN_N << "updateModel called with null model pointer";
    }
}
} // namespace)


ChatConversation::ChatConversation(QString name, QObject *parent)
    : QObject{parent}, name_{std::move(name)}
{
}

ChatConversation::~ChatConversation()
{
    if (model_) {
        // Clear the model
        updateModel(model_, {}, ChatMessagesModel::Updates::Full);
        model_ = nullptr;
    }
}

void ChatConversation::addMessage(std::shared_ptr<ChatMessage> message)
{
    messages_.emplace_back(std::move(message));
    updateModel(model_, messages_, ChatMessagesModel::Updates::Append);
}

void ChatConversation::updateLastMessage(std::string text)
{
    if (messages_.empty()) {
        return;
    }

    messages_.back()->content = std::move(text);
    updateModel(model_, messages_, ChatMessagesModel::Updates::LastMessageChanged);
}

void ChatConversation::finalizeLastMessage()
{
    if (messages_.empty()) {
        return;
    }

    messages_.back()->completed = true;
    updateModel(model_, messages_, ChatMessagesModel::Updates::LastMessageChanged);
}

void ChatConversation::setModel(ChatMessagesModel *model)
{
    LOG_TRACE_N << "Setting model";
    assert(model);
    if (model_ && model != model_) {
        updateModel(model_, {}, ChatMessagesModel::Updates::Full);
    }

    model_ = model;
    updateModel(model, messages_, ChatMessagesModel::Updates::Full);
}

std::vector<const ChatMessage *> ChatConversation::getMessages() const
{
    std::vector<const ChatMessage *> msg_ptrs;
    msg_ptrs.reserve(messages_.size());
    for (const auto &msg : messages_) {
        msg_ptrs.push_back(msg.get());
    }
    return msg_ptrs;
}

messages_view_t ChatConversation::getLastMessageAsView()
{
    if (messages_.empty() || messages_.back()->role != PromptRole::User) {
        LOG_TRACE_N << "No user message at the end.";
        return {};
    }

    if (messages_.size() == 2) {
        if (messages_[0]->role == PromptRole::System
            && messages_[1]->role == PromptRole::User) {

            // Send both system and user message
            // This is the start of the conversation.
            last_message_cache_[0] = messages_[0].get();
            last_message_cache_[1] = messages_[1].get();
            LOG_TRACE_N << "Returning system and user message (start of conversation).";
            return messages_view_t{last_message_cache_.data(), 2};
        }
    }

    LOG_TRACE_N << "Returning last user message only.";
    last_message_cache_[0] = messages_.back().get();
    return {last_message_cache_.data(), 1};
}
