#ifndef I18N_H
#define I18N_H

#include <QString>
#include <QStringList>

// Manages app + Qt translators for live language switching.
//
// Locale codes are short forms ("en", "ru", "de", "fr", "es", "zh_CN").
// "en" means "no translator installed" — source strings are English.
// Switching emits QEvent::LanguageChange to every QWidget; widgets must
// override changeEvent() to call their retranslateUi() to actually update.
namespace I18n {
    // Codes for which a cdimage_<code>.qm exists in :/i18n/. Always includes
    // "en" as the first entry (representing "no translation, source language").
    QStringList availableLocales();

    // Human-readable name for a locale, e.g. "English", "Русский", "Deutsch".
    QString displayName(const QString& code);

    // Currently active locale code (last switchTo or initial value).
    QString currentLocale();

    // Persisted user choice (QSettings "ui/locale"); empty if never set.
    QString persistedLocale();

    // Called from main() before MainWindow is constructed. Reads QSettings
    // (or QLocale::system().name() as fallback) and installs translators.
    void installInitial();

    // Switches to the given locale, persists the choice, and posts
    // QEvent::LanguageChange to all widgets via QApplication.
    void switchTo(const QString& code);
}

#endif
