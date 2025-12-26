// LanguagesModel.h
#pragma once

#include <vector>

#include <QObject>
#include <QAbstractListModel>
#include <QSettings>
#include <QtQml/qqml.h>

class LanguagesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(QString selectedCode READ selectedCode WRITE setSelectedCode  NOTIFY selectedChanged)

public:
    enum class Roles {
        Name = Qt::UserRole + 1,
        Code,
        NativeName
    };

    struct Entry {
        QString name;       // e.g. "English"
        QString code;       // e.g. "en" or "auto"
        QString nativeName; // optional, e.g. "English", "Български"
    };

    explicit LanguagesModel(const QString& settingsKey, QObject *parent = nullptr);

    // Model API
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Selection
    int selected() const noexcept { return indexOfCode(selected_code_); }
    void setSelected(int index);
    void setSelectedCode(const QString& code);

    QString selectedCode() const;
    QString selectedName() const;
    bool autoIsSelected() const;
    bool haveSelection() const noexcept { return !selected_code_.isEmpty(); }


    // Convenience helpers
    Q_INVOKABLE int indexOfCode(const QString& code) const noexcept;
    Q_INVOKABLE void selectByCode(const QString& code);
    Q_INVOKABLE void showAuto(bool show); // Difference between source and destination languages

signals:
    void selectedChanged();

private:
    void loadSelection();
    void saveSelection() const;
    void clampSelection();
    int  fallbackIndex() const noexcept;
    std::optional<Entry> findEntryByCode(const QString& code) const noexcept;

    std::vector<Entry> entries_;
    QString selected_code_;
    QString settings_key_;
    bool show_auto_{true};
};
