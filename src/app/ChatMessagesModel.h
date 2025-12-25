#pragma once

#include <span>
#include <vector>

#include <QObject>
#include <QQmlComponent>
#include <QAbstractListModel>
#include <QJsonObject>

/*! Simple model to present chat messages to q QML ListView */

#include "ModelInfo.h"


class ChatMessagesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
public:
    /*! We always wraps the entire list, buit the Updates type
     *  decides what signals we emit to the QML side.
     */
    enum class Updates {
        Full,
        Append,
        LastMessageChanged // e.g. assistant is typing
    };

    // Per message roles for QML list
    enum class Roles {
        Actor = Qt::UserRole + 1,
        Message,
        Completed,
        IsUser,
        IsAssistant
    };

    enum class Format {
        Auto, // deduce from file extension (.json) or use Markdown
        Markdown,
        JSON
    };
    Q_ENUM(Format)

    explicit ChatMessagesModel(QObject *parent = nullptr);

    Q_INVOKABLE void copyMessageToClipboard(int index);
    Q_INVOKABLE void copyAllToClipboard(Format format);
    Q_INVOKABLE void saveMessage(int index, Format format, const QUrl& path);
    Q_INVOKABLE void saveConversation(int index, Format format, const QUrl& path);

    void setMessages(std::span<const ChatMessage *> messages, Updates updates = Updates::Full);

    // Implement required virtuals
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void messagesChanged();

private:
    QString actorName(const ChatMessage &msg) const;
    QString formatMessageAsMarkdown(const ChatMessage &msg) const;
    QJsonObject formatMessageAsJSON(const ChatMessage &msg) const;

    std::vector<const ChatMessage *> messages_; // Our own view, without any leading System message
};
