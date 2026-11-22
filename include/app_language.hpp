#pragma once

#include <QString>

namespace bspvision {

enum class UiLanguage {
    English,
    Russian
};

inline UiLanguage &activeUiLanguageStorage() {
    static UiLanguage language = UiLanguage::Russian;
    return language;
}

inline UiLanguage uiLanguage() {
    return activeUiLanguageStorage();
}

inline void setUiLanguage(UiLanguage language) {
    activeUiLanguageStorage() = language;
}

inline QString uiText(const char *english, const char *russian) {
    return QString::fromUtf8(uiLanguage() == UiLanguage::Russian ? russian : english);
}

} // namespace bspvision
