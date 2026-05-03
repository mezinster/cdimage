#include "i18n.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {
    // Single instances we own — swap their loaded .qm on switch instead of
    // newing/deleting, so install/remove is symmetric and we don't leak.
    QTranslator* g_appTranslator = nullptr;
    QTranslator* g_qtTranslator  = nullptr;
    QString      g_currentLocale = QStringLiteral("en");

    // Locales we ship a translation for. "en" is the source language and
    // never has a .qm — it just means "uninstall translators".
    const QStringList& shippedLocales() {
        static const QStringList kCodes = {
            QStringLiteral("en"),
            QStringLiteral("de"),
            QStringLiteral("es"),
            QStringLiteral("fr"),
            QStringLiteral("ru"),
            QStringLiteral("zh_CN"),
        };
        return kCodes;
    }

    // Pick the best shipped locale for the user. Order: explicit setting →
    // system locale (full code, then language part) → English.
    QString resolveStartupLocale() {
        QSettings s;
        const QString persisted = s.value("ui/locale").toString();
        if (!persisted.isEmpty() && shippedLocales().contains(persisted))
            return persisted;

        const QString sysFull = QLocale::system().name();           // e.g. "ru_RU"
        if (shippedLocales().contains(sysFull)) return sysFull;
        const QString sysLang = sysFull.section('_', 0, 0);         // "ru"
        if (shippedLocales().contains(sysLang)) return sysLang;
        return QStringLiteral("en");
    }

    void uninstallCurrent() {
        if (g_appTranslator) QCoreApplication::removeTranslator(g_appTranslator);
        if (g_qtTranslator)  QCoreApplication::removeTranslator(g_qtTranslator);
    }

    // Install the .qm files for `code`. "en" → uninstall only.
    void installFor(const QString& code) {
        uninstallCurrent();
        if (code == QLatin1String("en")) {
            g_currentLocale = code;
            return;
        }
        if (!g_appTranslator) g_appTranslator = new QTranslator(qApp);
        if (!g_qtTranslator)  g_qtTranslator  = new QTranslator(qApp);

        // App translations are embedded by qmake's `embed_translations` into
        // the Qt resource system at :/i18n/<basename>_<locale>.qm. The
        // basename is the .pro TARGET (cdimage).
        if (g_appTranslator->load(QStringLiteral("cdimage_%1").arg(code),
                                  QStringLiteral(":/i18n")))
            QCoreApplication::installTranslator(g_appTranslator);

        // Qt's own translations (standard dialogs, wizard buttons) ship
        // alongside Qt — load them by short-form locale, with prefix qtbase_.
        if (g_qtTranslator->load(QStringLiteral("qtbase_%1").arg(code),
                                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QCoreApplication::installTranslator(g_qtTranslator);

        g_currentLocale = code;
    }
}

namespace I18n {

QStringList availableLocales() { return shippedLocales(); }

QString displayName(const QString& code) {
    if (code == QLatin1String("en"))    return QStringLiteral("English");
    if (code == QLatin1String("de"))    return QStringLiteral("Deutsch");
    if (code == QLatin1String("es"))    return QStringLiteral("Español");
    if (code == QLatin1String("fr"))    return QStringLiteral("Français");
    if (code == QLatin1String("ru"))    return QStringLiteral("Русский");
    if (code == QLatin1String("zh_CN")) return QStringLiteral("中文 (简体)");
    return code;
}

QString currentLocale() { return g_currentLocale; }

QString persistedLocale() {
    QSettings s;
    return s.value("ui/locale").toString();
}

void installInitial() {
    installFor(resolveStartupLocale());
}

void switchTo(const QString& code) {
    if (!shippedLocales().contains(code)) return;
    if (code == g_currentLocale) return;
    installFor(code);
    QSettings s;
    s.setValue("ui/locale", code);
}

} // namespace I18n
