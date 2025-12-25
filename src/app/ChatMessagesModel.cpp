
#include <QGuiApplication>
#include <QClipboard>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>


#include "ChatMessagesModel.h"

#include "logging.h"

using namespace std;


ChatMessagesModel::ChatMessagesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ChatMessagesModel::copyMessageToClipboard(int row)
{
    if (auto msg = data(index(row), static_cast<int>(Roles::Message)).toString(); !msg.isEmpty()) {
        QGuiApplication::clipboard()->setText(msg);
    } else {
        LOG_WARN_N << "No message found at row " << row;
    }
}

void ChatMessagesModel::copyAllToClipboard(Format format)
{
    if (format == Format::Auto) {
        format = Format::Markdown;
    }

    if (format == Format::Markdown) {
        QString all;
        for (const auto *msg : messages_) {
            if (!msg) continue;
            all += formatMessageAsMarkdown(*msg);
            all += "\n\n";
        }
        QGuiApplication::clipboard()->setText(all.trimmed());
        return;
    }

    QJsonArray messagesArray;

    for (const auto *msg : messages_) {
        if (!msg) {
            continue;
        }

        const QJsonObject obj = formatMessageAsJSON(*msg);
        messagesArray.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("messages"), messagesArray);

    const QJsonDocument doc(root);
    const QByteArray jsonBytes = doc.toJson(QJsonDocument::Indented); // or Compact
    QGuiApplication::clipboard()->setText(QString::fromUtf8(jsonBytes));
}


void ChatMessagesModel::saveMessage(int index, Format format, const QUrl &path)
{
    
}

void ChatMessagesModel::saveConversation(int index, Format format, const QUrl &path)
{
    
}


void ChatMessagesModel::setMessages(std::span<const ChatMessage *> messages, Updates updates)
{
    const bool strip_system = !messages.empty() && messages[0]->role == PromptRole::System;
    const auto msgs = strip_system ? messages.subspan(1) : messages;

    if (msgs.empty()) {
        if (!messages_.empty()) {
            beginResetModel();
            messages_.clear();
            endResetModel();
            LOG_TRACE_N << "Cleared all messages from model.";
        }
        return;
    }

    switch (updates) {
    case Updates::Full:
        LOG_TRACE_N << "Full messages update, total messages: " << messages_.size();
        beginResetModel();
        messages_.assign(msgs.begin(), msgs.end());
        endResetModel();
        break;
    case Updates::Append: {
        LOG_TRACE_N << "Append message, total messages: " << messages_.size();
        const auto row = messages_.size();
        beginInsertRows(QModelIndex(), row, row);
        messages_.push_back(msgs.back());
        endInsertRows();
    } break;
    case Updates::LastMessageChanged:
        if (!messages_.empty()) {
            LOG_TRACE_N << "Last message changed, total messages: " << messages_.size();
            const auto idx = index(messages_.size() - 1);
            messages_.back() = msgs.back();
            emit dataChanged(idx, idx);
        }
        break;
    default:
        LOG_WARN_N << "Unknown update type in setMessages: " << static_cast<int>(updates);
        break;
    }
}

int ChatMessagesModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        LOG_TRACE_N << "rowCount called with invalid parent";
        //return 0;
    }
    LOG_TRACE_N << "rowCount called, total rows: " << messages_.size();
    return static_cast<int>(messages_.size());
}

QVariant ChatMessagesModel::data(const QModelIndex &index, int role) const
{
    LOG_TRACE_N << "data() called for row " << index.row() << ", role " << role;

    if (!index.isValid()) {
        LOG_DEBUG_N << "data() called with invalid index";
        return {};
    }
    if (index.row() < 0
        || static_cast<size_t>(index.row()) >= messages_.size()) {
        LOG_DEBUG_N << "data() called with out-of-bounds index: " << index.row();
        return {};
    }

    const auto ix = static_cast<size_t>(index.row());
    assert(messages_.at(ix) != nullptr);
    const ChatMessage &msg = *messages_[ix];

    switch (static_cast<Roles>(role)) {
    case Roles::Actor:
        return actorName(msg);
    case Roles::Message:
        LOG_TRACE_N << "Message at index " << ix << ": " << msg.content;
        return QString::fromStdString(msg.content);
    case Roles::Completed:
        LOG_TRACE_N << "Message at index " << ix << " completed: " << msg.completed;
        return msg.completed;
    case Roles::IsUser:
        LOG_TRACE_N << "Message at index " << ix << " is user: " << (msg.role == PromptRole::User);
        return msg.role == PromptRole::User;
    case Roles::IsAssistant:
        return msg.role == PromptRole::Assistant;
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> ChatMessagesModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(Roles::Actor)] = "actor";
    roles[static_cast<int>(Roles::Message)] = "message";
    roles[static_cast<int>(Roles::Completed)] = "completed";
    roles[static_cast<int>(Roles::IsUser)] = "isUser";
    roles[static_cast<int>(Roles::IsAssistant)] = "isAssistant";
    return roles;
}

QString ChatMessagesModel::actorName(const ChatMessage &msg) const
{
    switch (msg.role) {
    case PromptRole::User:
        return tr("You");
    case PromptRole::Assistant:
        return tr("Assistant");
    default:
        return tr("system");
    }
}

QString ChatMessagesModel::formatMessageAsMarkdown(const ChatMessage &msg) const
{
    const QString role = actorName(msg);
    const QString content = QString::fromStdString(msg.content).trimmed();

    const QString timestamp =
        QDateTime::fromSecsSinceEpoch(msg.timestamp).toString(Qt::ISODate);

    const QString duration =
        QString::number(msg.duration_seconds, 'f', 2);

    QString md;
    md += "### " + role + "\n";
    md += content + "\n\n";
    md += "<sub>🕒 " + timestamp + " · ⏱ " + duration + "s</sub>";

    return md;
}

QJsonObject ChatMessagesModel::formatMessageAsJSON(const ChatMessage &msg) const
{
    QJsonObject o;
    o["role"] = actorName(msg);
    o["content"] = QString::fromStdString(msg.content);
    o["timestamp"] = QDateTime::fromSecsSinceEpoch(msg.timestamp).toString(Qt::ISODate);
    o["duration"] = msg.duration_seconds;
    return o;
}
