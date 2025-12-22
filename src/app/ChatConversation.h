#pragma once

#include <span>
#include <deque>
#include <memory>
#include <array>

#include <QObject>
#include <QString>

#include "ModelInfo.h"

class ChatMessagesModel;

class ChatConversation : public QObject
{
    Q_OBJECT
public:
    ChatConversation(QString name, QObject *parent = nullptr);

    ~ChatConversation();

    const QString& name() const { return name_; }

    void addMessage(std::shared_ptr<ChatMessage> message);
    void updateLastMessage(std::string text); // for partial updates from assistant
    void finalizeLastMessage();
    void setModel(ChatMessagesModel *model);
    std::vector<const ChatMessage *> getMessages() const;

    /*! for continuation prompts.
     *  Sends system prompt as well if there is only one user message
     */
    messages_view_t getLastMessageAsView();

signals:
    void messagesChanged();
    void nameChanged();

private:
    QString name_;
    std::deque<std::shared_ptr<ChatMessage>> messages_;
    ChatMessagesModel *model_{nullptr};
    std::array<const ChatMessage *, 2> last_message_cache_{nullptr, nullptr};
};
