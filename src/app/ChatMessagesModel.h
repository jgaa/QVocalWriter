#pragma once

#include <span>
#include <vector>

#include <QObject>
#include <QAbstractListModel>

/*! Simple model to present chat messages to q QML ListView */

#include "ModelInfo.h"


class ChatMessagesModel : public QAbstractListModel
{
    Q_OBJECT
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

    explicit ChatMessagesModel(QObject *parent = nullptr);

    void setMessages(std::span<const ChatMessage *> messages, Updates updates = Updates::Full);

    // Implement required virtuals
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;


signals:
    void messagesChanged();

private:
    std::vector<const ChatMessage *> messages_; // Our own view, without and leading System message
};
