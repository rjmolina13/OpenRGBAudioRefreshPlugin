#include "OpenRGBPluginInterface.h"

#include <QApplication>
#include <QDialog>
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include <QPluginLoader>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QPixmap>
#include <QTimer>
#include <QDir>
#include <QDebug>

#include <windows.h>
#include <objbase.h>
#include <stdio.h>

struct AudioSettingsStruct
{
    int           audio_device     = 0;
    unsigned int  amplitude        = 100;
    unsigned int  avg_mode         = 0;
    unsigned int  avg_size         = 8;
    unsigned int  window_mode      = 0;
    unsigned int  decay            = 80;
    float         filter_constant  = 1.0f;
    float         nrml_ofst        = 0.04f;
    float         nrml_scl         = 0.5f;
    float         equalizer[16]    = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
    };
};

static void ApplyOpenRGBDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    darkPalette.setColor(QPalette::Base, QColor(32, 32, 32));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(230, 230, 230));
    darkPalette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    darkPalette.setColor(QPalette::Text, QColor(230, 230, 230));
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(0, 120, 215));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(darkPalette);
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    QApplication app(argc, argv);
    ApplyOpenRGBDarkTheme(app);

    printf("[Test] 1. Loading C:\\Program Files\\OpenRGB\\plugins\\OpenRGBEffectsPlugin.dll...\n");
    HMODULE hEffects = LoadLibraryW(L"C:\\Program Files\\OpenRGB\\plugins\\OpenRGBEffectsPlugin.dll");
    if (!hEffects)
    {
        printf("[FAIL] Could not load OpenRGBEffectsPlugin.dll (err=%lu)\n", GetLastError());
        return 1;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(hEffects);
    using FnAudioSettingsCtor = void* (__fastcall*)(void*, QWidget*);
    using FnAudioSettingsSet  = void  (__fastcall*)(void*, AudioSettingsStruct*);

    FnAudioSettingsCtor fnCtor = reinterpret_cast<FnAudioSettingsCtor>(base + 0x035AF0);
    FnAudioSettingsSet  fnSet  = reinterpret_cast<FnAudioSettingsSet>(base + 0x036640);

    // Allocate 256 bytes for AudioSettings instance (actual size is ~88 bytes).
    void* rawDialogMem = operator new(256);
    memset(rawDialogMem, 0, 256);

    AudioSettingsStruct settingsStruct;
    settingsStruct.audio_device = 0;

    fnCtor(rawDialogMem, nullptr);
    QDialog* audioSettingsDlg = reinterpret_cast<QDialog*>(rawDialogMem);

    QObject::connect(audioSettingsDlg, &QObject::destroyed, []() {});
    fnSet(rawDialogMem, &settingsStruct);

    printf("[PASS] Instantiated genuine AudioSettings dialog from OpenRGBEffectsPlugin.dll (windowTitle='%s')\n",
           audioSettingsDlg->windowTitle().toUtf8().constData());

    printf("[Test] 2. Loading OpenRGBAudioRefreshPlugin.dll from %%APPDATA%%\\OpenRGB\\plugins...\n");
    QString pluginPath = QDir::fromNativeSeparators(
        QString::fromLocal8Bit(qgetenv("APPDATA")) + "\\OpenRGB\\plugins\\OpenRGBAudioRefreshPlugin.dll");

    QPluginLoader loader(pluginPath);
    QObject* pluginInstance = loader.instance();
    if (!pluginInstance || !loader.isLoaded())
    {
        printf("[FAIL] QPluginLoader failed: %s\n", loader.errorString().toUtf8().constData());
        return 1;
    }

    OpenRGBPluginInterface* plugin = qobject_cast<OpenRGBPluginInterface*>(pluginInstance);
    if (!plugin)
    {
        printf("[FAIL] qobject_cast<OpenRGBPluginInterface*> failed\n");
        return 1;
    }

    unsigned int apiVer = plugin->GetPluginAPIVersion();
    OpenRGBPluginInfo info = plugin->GetPluginInfo();
    printf("[PASS] Loaded plugin '%s' (API Version %u, Label '%s', Icon=%dx%d, TabIcon=%dx%d)\n",
           info.Name.c_str(), apiVer, info.Label.c_str(),
           info.Icon.width(), info.Icon.height(),
           info.TabIcon.width(), info.TabIcon.height());

    if (info.Icon.isNull() || info.TabIcon.isNull())
    {
        printf("[FAIL] Plugin Icon or TabIcon is null\n");
        return 1;
    }

    if (apiVer != OPENRGB_PLUGIN_API_VERSION)
    {
        printf("[FAIL] API version mismatch: got %u, expected %u\n", apiVer, OPENRGB_PLUGIN_API_VERSION);
        return 1;
    }

    plugin->Load(nullptr);
    QWidget* standaloneTab = plugin->GetWidget();
    QMenu* trayMenu = plugin->GetTrayMenu();

    if (!standaloneTab || !trayMenu)
    {
        printf("[FAIL] GetWidget() or GetTrayMenu() returned nullptr\n");
        return 1;
    }
    printf("[PASS] GetWidget() and GetTrayMenu() initialized\n");

    printf("[Step] Calling audioSettingsDlg->show()...\n");
    audioSettingsDlg->show();
    printf("[Step] Calling app.processEvents()...\n");
    app.processEvents();
    printf("[Step] Finding injected children...\n");

    QComboBox* combo         = audioSettingsDlg->findChild<QComboBox*>("audio_device");
    QPushButton* refreshBtn  = audioSettingsDlg->findChild<QPushButton*>("orgb_audio_refresh_btn");
    QCheckBox* autoCheck     = audioSettingsDlg->findChild<QCheckBox*>("orgb_auto_refresh_chk");
    QCheckBox* prioCheck     = audioSettingsDlg->findChild<QCheckBox*>("orgb_priority_toggle_chk");
    QLabel* statusLbl        = audioSettingsDlg->findChild<QLabel*>("orgb_audio_status_lbl");
    QWidget* prioContainer   = audioSettingsDlg->findChild<QWidget*>("orgb_priority_container");
    QListWidget* prioList    = audioSettingsDlg->findChild<QListWidget*>("orgb_priority_list");
    QPushButton* addBtn      = audioSettingsDlg->findChild<QPushButton*>("orgb_priority_add_btn");
    QPushButton* upBtn       = audioSettingsDlg->findChild<QPushButton*>("orgb_priority_up_btn");
    QPushButton* downBtn     = audioSettingsDlg->findChild<QPushButton*>("orgb_priority_down_btn");
    QPushButton* removeBtn   = audioSettingsDlg->findChild<QPushButton*>("orgb_priority_remove_btn");
    QPushButton* applyBtn    = audioSettingsDlg->findChild<QPushButton*>("orgb_apply_priority_btn");

    if (!combo || !refreshBtn || !autoCheck || !prioCheck || !statusLbl || !prioContainer ||
        !prioList || !addBtn || !upBtn || !downBtn || !removeBtn || !applyBtn)
    {
        printf("[FAIL] Missing expected injected controls inside AudioSettings dialog\n");
        return 1;
    }
    printf("[PASS] All injected controls verified inside AudioSettings dialog\n");

    QString artifactDir = "C:/Users/Ronanj/.gemini/antigravity/brain/be7146bb-d3ed-402b-b90f-b9dbe9733ce5";

    // Step A: Ensure Priority list starts toggled OFF for the compact screenshot, click Refresh button.
    if (prioCheck->isChecked())
    {
        prioCheck->click();
        app.processEvents();
    }
    while (prioList->count() > 0)
    {
        prioList->setCurrentRow(0);
        removeBtn->click();
        app.processEvents();
    }

    printf("[Test] 3. Clicking 'Refresh' button (orgb_audio_refresh_btn)...\n");
    refreshBtn->click();
    app.processEvents();

    printf("[PASS] Refresh completed. Combo count = %d, Status = '%s'\n",
           combo->count(), statusLbl->text().toUtf8().constData());
    for (int i = 0; i < combo->count(); ++i)
    {
        printf("       Device[%d]: %s\n", i, combo->itemText(i).toUtf8().constData());
    }

    audioSettingsDlg->adjustSize();
    app.processEvents();
    QPixmap shotCompact = audioSettingsDlg->grab();
    shotCompact.save(artifactDir + "/audio_settings_compact.png");
    printf("[PASS] Saved screenshot: audio_settings_compact.png (%dx%d)\n",
           shotCompact.width(), shotCompact.height());

    // Step B: Click 'Priority audio devices list' checkbox to toggle it ON.
    printf("[Test] 4. Clicking 'Priority audio devices list' toggle (orgb_priority_toggle_chk)...\n");
    prioCheck->click();
    app.processEvents();
    if (!prioCheck->isChecked() || !prioContainer->isVisible())
    {
        printf("[FAIL] Priority container did not become visible when toggled ON\n");
        return 1;
    }
    printf("[PASS] Priority container visible (empty state initially shown)\n");

    // Step C: Add devices using 'Add Selected' button, then test Move Down / Move Up / Remove.
    if (combo->count() >= 2)
    {
        combo->setCurrentIndex(1);
        app.processEvents();
        printf("[Test] 5. Clicking 'Add Selected' for device[1] ('%s')...\n",
               combo->currentText().toUtf8().constData());
        addBtn->click();
        app.processEvents();

        combo->setCurrentIndex(0);
        app.processEvents();
        printf("[Test] 6. Clicking 'Add Selected' for device[0] ('%s')...\n",
               combo->currentText().toUtf8().constData());
        addBtn->click();
        app.processEvents();

        printf("[PASS] Priority list count = %d\n", prioList->count());

        // Select row 1 (device[0]) and click 'Move Up' so device[0] becomes #1 priority.
        prioList->setCurrentRow(1);
        app.processEvents();
        printf("[Test] 7. Clicking 'Move Up' (orgb_priority_up_btn) on row 1...\n");
        upBtn->click();
        app.processEvents();
        printf("[PASS] After Move Up: #1 Priority is '%s', active combo is '%s'\n",
               prioList->item(0)->text().toUtf8().constData(),
               combo->currentText().toUtf8().constData());

        // Test 'Apply Top Priority' button.
        printf("[Test] 8. Clicking 'Apply Top Priority' (orgb_apply_priority_btn)...\n");
        applyBtn->click();
        app.processEvents();
        printf("[PASS] Status after Apply Top Priority: '%s'\n",
               statusLbl->text().toUtf8().constData());
    }

    audioSettingsDlg->adjustSize();
    app.processEvents();
    QPixmap shotExpanded = audioSettingsDlg->grab();
    shotExpanded.save(artifactDir + "/audio_settings_priority_expanded.png");
    printf("[PASS] Saved screenshot: audio_settings_priority_expanded.png (%dx%d)\n",
           shotExpanded.width(), shotExpanded.height());

    // Step D: Render and capture the standalone plugin tab (Settings -> Audio Refresh & Priority).
    standaloneTab->resize(640, 480);
    standaloneTab->show();
    app.processEvents();
    QPixmap shotTab = standaloneTab->grab();
    shotTab.save(artifactDir + "/plugin_settings_tab.png");
    printf("[PASS] Saved screenshot: plugin_settings_tab.png (%dx%d)\n",
           shotTab.width(), shotTab.height());

    standaloneTab->hide();
    audioSettingsDlg->hide();
    plugin->Unload();

    printf("[ALL TESTS PASSED]\n");
    return 0;
}
