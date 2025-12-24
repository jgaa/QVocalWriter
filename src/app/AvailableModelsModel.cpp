
#include <QSettings>

#include "AvailableModelsModel.h"
#include "ModelMgr.h"

#include "logging.h"
using namespace std;


AvailableModelsModel::AvailableModelsModel(ModelKind kind, QString propertiesTag, QObject *parent)
    : QAbstractListModel(parent), kind_(kind), properties_tag_(std::move(propertiesTag))
{
    if (!properties_tag_.isEmpty()) {
        // load current model-id from settings
        selected_model_id_ = QSettings{}.value(properties_tag_, "").toString().toStdString();
    }
}

void AvailableModelsModel::SetModels(const models_t& models)
{
    static const ModelInfo none_model{"[none]"};

    models_.clear();
    models_.reserve(models.size() +1);
    models_.emplace_back(&none_model);
    for(const auto& m : models) {
        assert(m != nullptr);

        ModelEntry entry;
        entry.info = m;
        entry.downloaded = ModelMgr::instance().isDownloaded(kind_, *m);

        models_.emplace_back(std::move(entry));
    }

    if (!selected_model_id_.empty() && !models_.empty()) {
        // select first model by default
        bool found = false;
        for (const auto& entry : models_) {
            if (entry.info->id == selected_model_id_) {
                selected_model_name_ = QString::fromUtf8(entry.info->name);
                found = true;
                break;
            }
        }

        if (!found) {
            selected_model_id_.clear();
            selected_model_name_.clear();
        }
    }


    if (!initialzed_) {
        connect(&ModelMgr::instance(), &ModelMgr::modelDownloaded,
                this, [this](ModelKind kind, const string& id) {

            LOG_TRACE_N << "AvailableModelsModel received modelsChanged signal for kind="
                        << static_cast<int>(kind) << " id=" << id;

            if (kind_ == kind) {
                // update downloaded status in models_ for id
                int row = 0;
                for (auto& entry : models_) {
                    if (entry.info->id == id) {
                        // Send row changed signal
                        QModelIndex idx = index(row, 0);
                        entry.downloaded = true;
                        LOG_TRACE_N << "Model downloaded: updating row " << row;
                        emit dataChanged(idx, idx, {static_cast<int>(Roles::Downloaded)});
                        break;
                    }
                    ++row;
                }
            }
        });
        initialzed_ = true;
    }
}

int AvailableModelsModel::selected() const
{
    if (selected_model_id_.empty()) {
        return -1;
    }

    // Search for selected_model_id_
    for (size_t i = 0; i < models_.size(); ++i) {
        if (models_[i].info->id == selected_model_id_) {
            return static_cast<int>(i);
        }
    }

    return -1; // Not found
}

void AvailableModelsModel::setSelected(int index)
{
    const auto current = selected();
    LOG_TRACE_N << "Setting selected model index from " << current << " to " << index;
    if (current != index) {
        if (index < 0 || static_cast<size_t>(index) >= models_.size()) {
            selected_model_id_.clear();
            selected_model_name_.clear();
        } else {
            if (auto& id = models_[index].info->id; !id.empty()) {
                selected_model_id_ = id;
                selected_model_name_ = QString::fromUtf8(models_[index].info->name);
            } else {
                selected_model_id_.clear();
                selected_model_name_.clear();
            }
        }
        emit selectedChanged();
        QSettings{}.setValue(properties_tag_, QString::fromUtf8(selected_model_id_));
    }
}

const ModelInfo *AvailableModelsModel::selectedModel() const
{
    const int index = selected();
    if (index >= 0 && static_cast<size_t>(index) < models_.size()) {
        return models_[index].info;
    }
    return nullptr;
}

int AvailableModelsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(models_.size());
}

QVariant AvailableModelsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= models_.size()) {
        return {};
    }

    const ModelEntry& entry = models_[static_cast<size_t>(index.row())];

    switch (static_cast<Roles>(role)) {
    case Roles::Name:
        return QString::fromUtf8(entry.info->name);
    case Roles::Id:
        return QString::fromUtf8(entry.info->id);
    case Roles::SizeMB:
        return static_cast<qulonglong>(entry.info->size_mb);
    case Roles::Downloaded:
        return entry.downloaded;
    default:
        return {};
    }
}

QHash<int, QByteArray> AvailableModelsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[static_cast<int>(Roles::Name)] = "name";
    roles[static_cast<int>(Roles::Id)] = "id";
    roles[static_cast<int>(Roles::SizeMB)] = "sizeMB";
    roles[static_cast<int>(Roles::Downloaded)] = "downloaded";
    return roles;
}
