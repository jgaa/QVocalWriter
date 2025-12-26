#pragma once

#include <array>
#include <string_view>


#include <QQuickItem>
#include <QAbstractListModel>
#include <QSettings>
#include <QString>
#include <QQmlEngine>

/*!
 * RewriteStyleModel
 *
 * QML-facing model that lets the user pick an output “style” (None + several formats).
 * Holds the selected index (and can optionally persist it via QSettings).
 *
 * Usage in QML:
 *   ComboBox {
 *     model: rewriteStyleModel
 *     textRole: "name"
 *     valueRole: "id"
 *     currentIndex: rewriteStyleModel.selected
 *     onCurrentIndexChanged: rewriteStyleModel.selected = currentIndex
 *   }
 *
 * Then in C++ / QML:
 *   const auto prompt = rewriteStyleModel.makePrompt(transcription, extraConstraints);
 */
class RewriteStyleModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(QString socialMedia MEMBER social_media_type_ NOTIFY selectedChanged)
    Q_PROPERTY(bool isSocialMedia READ isSocialMedia NOTIFY selectedChanged)

public:
    enum class Roles : int {
        Name = Qt::UserRole + 1,
        Kind,
        HasPrompt
    };
    Q_ENUM(Roles)

    enum class Kind : int {
        None = -1,
        Blog,
        Email,
        SocialMedia,
        TechnicalDoc,
        MeetingNotes,
        Plan,
        Creative,
        Conservative,
        Rant,
        Clean
    };
    Q_ENUM(Kind)

    explicit RewriteStyleModel(const QString& settingsKey, QObject* parent = nullptr);

    // ---- QAbstractListModel ----
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    // ---- Selection ----
    int selected() const noexcept;

    bool hasSelection() const noexcept {
        return selected_ > 0;
    }

    bool isSocialMedia() const {
        return selectedKind() == Kind::SocialMedia;
    }

    QString extra() const;

    Q_INVOKABLE void setSelected(int index);

    Q_INVOKABLE Kind selectedKind() const;

    Q_INVOKABLE QString selectedName() const;

    QString socialMediaType() const;

    // ---- Settings ----
    QString settingsKey() const;

    // ---- Prompt building ----
    /*!
     * Build a full prompt for the currently selected style.
     *
     * Placeholders:
     *   %1 = extraConstraints (can be empty; should be short directives)
     *        For social media, just the target: "Twitter/Facebook/Instagram/etc."
     *
     * If "None" is selected, returns an empty QString.
     */
    Q_INVOKABLE QString makePrompt() const;

private:
    struct Item {
        Kind kind;
        QString name;
        int prompt_index{-1}; // -1 => none
    };

    int clampedSelected_() const noexcept;

    void loadSelectedFromSettings();

    void saveSelectedToSettings() const;

signals:
    void selectedChanged();
    void settingsKeyChanged();

private:
    int selected_{0};
    std::vector<Item> items_;
    QString settings_key_{};
    QString social_media_type_;
};
