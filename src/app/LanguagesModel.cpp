// LanguagesModel.cpp

#include <array>

#include "LanguagesModel.h"

#include <algorithm>

#include <QSettings>

using namespace std;

namespace {

static const auto defaultLanguages = to_array<LanguagesModel::Entry>({
        { "[Auto detect]", "auto", "" },

        // ---- EU languages ----
        { "Bulgarian", "bg", "Български" },
        { "Croatian", "hr", "Hrvatski" },
        { "Czech", "cs", "Čeština" },
        { "Danish", "da", "Dansk" },
        { "Dutch", "nl", "Nederlands" },
        { "English", "en", "English" },
        { "Estonian", "et", "Eesti" },
        { "Finnish", "fi", "Suomi" },
        { "French", "fr", "Français" },
        { "German", "de", "Deutsch" },
        { "Greek", "el", "Ελληνικά" },
        { "Hungarian", "hu", "Magyar" },
        { "Irish", "ga", "Gaeilge" },
        { "Italian", "it", "Italiano" },
        { "Latvian", "lv", "Latviešu" },
        { "Lithuanian", "lt", "Lietuvių" },
        { "Maltese", "mt", "Malti" },
        { "Polish", "pl", "Polski" },
        { "Portuguese", "pt", "Português" },
        { "Romanian", "ro", "Română" },
        { "Slovak", "sk", "Slovenčina" },
        { "Slovenian", "sl", "Slovenščina" },
        { "Spanish", "es", "Español" },
        { "Swedish", "sv", "Svenska" },

        // ---- Common global languages ----
        { "Arabic", "ar", "العربية" },
        { "Chinese (Simplified)", "zh", "中文" },
        { "Hebrew", "he", "עברית" },
        { "Hindi", "hi", "हिन्दी" },
        { "Indonesian", "id", "Bahasa Indonesia" },
        { "Japanese", "ja", "日本語" },
        { "Korean", "ko", "한국어" },
        { "Norwegian", "no", "Norsk" },
        { "Persian", "fa", "فارسی" },
        { "Russian", "ru", "Русский" },
        { "Thai", "th", "ไทย" },
        { "Turkish", "tr", "Türkçe" },
        { "Ukrainian", "uk", "Українська" },
        { "Urdu", "ur", "اردو" },
        { "Vietnamese", "vi", "Tiếng Việt" }
});

} // anon ns

LanguagesModel::LanguagesModel(const QString& settingsKey, bool showAuto, QObject *parent)
    : QAbstractListModel(parent), settings_key_{settingsKey}, show_auto_{showAuto}
{
    auto lngs = std::vector<Entry>(
        defaultLanguages.begin() + (show_auto_ ? 0 : 1)
        , defaultLanguages.end());
    // Sort
    std::sort(lngs.begin(), lngs.end(), [](const Entry& a, const Entry& b) {
        // always keep "auto" first
        if (a.code.compare("auto", Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (b.code.compare("auto", Qt::CaseInsensitive) == 0) {
            return false;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    entries_ = std::move(lngs);

    loadSelection();
}

int LanguagesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(entries_.size());
}

QVariant LanguagesModel::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= static_cast<int>(entries_.size()))
        return {};

    const auto& e = entries_[static_cast<size_t>(row)];

    switch (static_cast<Roles>(role)) {
    case Roles::Name:       return e.name;
    case Roles::Code:       return e.code;
    case Roles::NativeName: return e.nativeName;
    }

    return {};
}

QHash<int, QByteArray> LanguagesModel::roleNames() const
{
    return {
        { static_cast<int>(Roles::Name),       "name" },
        { static_cast<int>(Roles::Code),       "code" },
        { static_cast<int>(Roles::NativeName), "nativeName" }
    };
}

void LanguagesModel::setSelected(int index)
{
    if (index < 0) {
        index = -1;
    }

    if (index >= static_cast<int>(entries_.size())) {
        index = -1;
    }

    const auto& entry = entries_[static_cast<size_t>(index)];

    if (selected_code_ != entry.code) {
        selected_code_ = entry.code;

        // Persist
        saveSelection();
        emit selectedChanged();
    }
}

void LanguagesModel::setSelectedCode(const QString &code)
{
    const int idx = indexOfCode(code);
    if (idx >= 0) {
        setSelected(idx);
    }
}

QString LanguagesModel::selectedCode() const
{
    return selected_code_;
}

QString LanguagesModel::selectedName() const
{
    const auto entry = findEntryByCode(selected_code_);
    if (entry) {
        return entry->name;
    }
    return {};
}

bool LanguagesModel::autoIsSelected() const
{
    return selected_code_.compare("auto", Qt::CaseInsensitive) == 0;
}


int LanguagesModel::indexOfCode(const QString& code) const noexcept
{
    if (code.isEmpty()) return -1;

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[static_cast<size_t>(i)].code.compare(code, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

void LanguagesModel::selectByCode(const QString& code)
{
    const int idx = indexOfCode(code);
    if (idx >= 0) setSelected(idx);
}

void LanguagesModel::showAuto(bool show)
{
    assert(!entries_.empty());
    const bool have_auto = (indexOfCode("auto") >= 0);

    if (show && !have_auto) {
        entries_.emplace(entries_.begin(), defaultLanguages.at(0));
        emit dataChanged(index(0), index(static_cast<int>(entries_.size()) - 1));
    } else if (have_auto) {
        assert(indexOfCode("auto") == 0);
        entries_.erase(entries_.begin());
        emit dataChanged(index(0), index(static_cast<int>(entries_.size()) - 1));
    }
}

void LanguagesModel::loadSelection()
{
    assert(!settings_key_.isEmpty());

    QSettings s;
    const QString code = s.value(settings_key_).toString().trimmed();
    selectByCode(code);
}

void LanguagesModel::saveSelection() const
{
    if (settings_key_.isEmpty()) return;

    QSettings s;
    s.setValue(settings_key_, selectedCode());
}

int LanguagesModel::fallbackIndex() const noexcept
{
    if (entries_.empty()) return -1;

    // Prefer "auto" if present (useful for source language lists).
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& code = entries_[static_cast<size_t>(i)].code;
        if (code.compare("auto", Qt::CaseInsensitive) == 0)
            return i;
    }

    return 0; // otherwise first entry
}

std::optional<LanguagesModel::Entry> LanguagesModel::findEntryByCode(const QString &code) const noexcept
{
    if (code.isEmpty()) return std::nullopt;

    for (const auto& e : entries_) {
        if (e.code.compare(code, Qt::CaseInsensitive) == 0)
            return e;
    }
    return {};
}
