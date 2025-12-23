#pragma once

#include <span>

#include <QObject>
#include <QQmlComponent>
#include <QAbstractListModel>

#include "ModelInfo.h"

class AvailableModelsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
public:
    enum class Roles {
        Name = Qt::UserRole + 1,
        Id,
        SizeMB,
        Downloaded
    };

    struct ModelEntry {
        ModelInfo info{};
        bool downloaded{false};
    };

    Q_INVOKABLE void setSelected(int index);
    Q_INVOKABLE QVariant roleValue(int row, const QString &roleName) const;

    AvailableModelsModel(ModelKind kind, QString propertiesTag, QObject *parent = nullptr);
    void SetModels(const std::span<const ModelInfo>& models);
    int selected() const;
    const ModelInfo* selectedModel() const;
    const QString& selectedModelName() const {
        return selected_model_name_;
    }
    ModelKind kind() const noexcept {
        return kind_;
    }
    const std::string_view currentId() const noexcept {
        return selected_model_id_;
    }
    bool empty() const noexcept {
        return models_.empty();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void modelsChanged();
    void selectedChanged();

private:
    ModelKind kind_{ModelKind::GENERAL};
    std::string selected_model_id_{};
    std::vector<class ModelEntry> models_;
    QString selected_model_name_;
    bool initialzed_{false};
    QString properties_tag_{};
};
