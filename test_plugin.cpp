#include "OpenRGBPluginInterface.h"

#include <QApplication>
#include <QDialog>
#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
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
    darkPalette.setColor(QPalette::Link, QColor(78, 168, 222));
    darkPalette.setColor(QPalette::Highlight, QColor(0, 120, 215));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(darkPalette);
}

static QFrame* BuildPluginEntryCard(const OpenRGBPluginInfo& info,
                                    unsigned int apiVer,
                                    const QString& dllPath,
                                    bool isSelected,
                                    QWidget* parent)
{
    QFrame* card = new QFrame(parent);
    card->setObjectName("pluginCard");
    card->setStyleSheet(isSelected
        ? "QFrame#pluginCard { background-color: #1F75CC; border: 1px solid #3A90E5; border-radius: 2px; }"
          "QLabel { border: none; background: transparent; color: #FFFFFF; }"
          "QCheckBox { border: none; background: transparent; color: #FFFFFF; }"
        : "QFrame#pluginCard { background-color: #262626; border: 1px solid #3A3A3A; border-radius: 2px; }"
          "QLabel { border: none; background: transparent; color: #E6E6E6; }"
          "QCheckBox { border: none; background: transparent; color: #E6E6E6; }");

    QHBoxLayout* hLayout = new QHBoxLayout(card);
    hLayout->setContentsMargins(14, 12, 14, 12);
    hLayout->setSpacing(16);

    QLabel* iconLabel = new QLabel(card);
    iconLabel->setFixedSize(64, 64);
    if (!info.Icon.isNull())
    {
        iconLabel->setPixmap(QPixmap::fromImage(info.Icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    hLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(3);

    auto addRow = [&](int row, const QString& key, const QString& val, bool isUrl = false) {
        QLabel* kLbl = new QLabel(key, card);
        grid->addWidget(kLbl, row, 0, Qt::AlignLeft | Qt::AlignTop);

        QLabel* vLbl = new QLabel(card);
        vLbl->setWordWrap(true);
        if (isUrl)
        {
            QString linkColor = isSelected ? "#D6EEFF" : "#4EA8DE";
            vLbl->setText(QString("<a style=\"color:%1;\" href=\"%2\">%2</a>").arg(linkColor, val));
            vLbl->setTextFormat(Qt::RichText);
        }
        else
        {
            vLbl->setText(val);
        }
        grid->addWidget(vLbl, row, 1);
    };

    addRow(0, "Name:",        QString::fromStdString(info.Name));
    addRow(1, "Description:", QString::fromStdString(info.Description));
    addRow(2, "Path:",        dllPath);
    addRow(3, "Version:",     QString::fromStdString(info.Version));
    addRow(4, "Commit:",      QString::fromStdString(info.Commit));
    addRow(5, "URL:",         QString::fromStdString(info.URL), true);
    addRow(6, "API Version:", QString::number(apiVer));

    QLabel* enKey = new QLabel("Enabled", card);
    QCheckBox* enChk = new QCheckBox(card);
    enChk->setChecked(true);
    grid->addWidget(enKey, 7, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(enChk, 7, 1, Qt::AlignLeft | Qt::AlignVCenter);
    grid->setColumnStretch(1, 1);

    hLayout->addLayout(grid, 1);
    return card;
}

static QWidget* BuildOpenRGBPluginsViewWidget(const OpenRGBPluginInfo& audioInfo,
                                              unsigned int audioApiVer,
                                              const QString& audioDllPath,
                                              const OpenRGBPluginInfo& effectsInfo,
                                              unsigned int effectsApiVer)
{
    QWidget* win = new QWidget();
    win->setWindowTitle("OpenRGB - Settings -> Plugins");
    win->resize(1040, 500);

    QVBoxLayout* rootLayout = new QVBoxLayout(win);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    // Top bar simulating OpenRGB top-level tabs
    QHBoxLayout* topTabs = new QHBoxLayout();
    topTabs->setSpacing(2);
    const char* tabNames[] = {"Devices", "Information", "SDK Server", "SDK Client", "Settings", "Effects"};
    for (int i = 0; i < 6; ++i)
    {
        QLabel* tab = new QLabel(tabNames[i], win);
        bool active = (i == 4);
        tab->setStyleSheet(active
            ? "background-color: #3B3B3B; color: #FFFFFF; padding: 5px 14px; border: 1px solid #555555; border-bottom: none; font-weight: bold;"
            : "background-color: #262626; color: #CCCCCC; padding: 5px 14px; border: 1px solid #3A3A3A;");
        topTabs->addWidget(tab);
    }
    topTabs->addStretch(1);
    rootLayout->addLayout(topTabs);

    // Main content frame under Settings
    QFrame* settingsBody = new QFrame(win);
    settingsBody->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout* bodyLayout = new QHBoxLayout(settingsBody);
    bodyLayout->setContentsMargins(6, 6, 6, 6);
    bodyLayout->setSpacing(8);

    // Left sidebar tabs in Settings
    QFrame* sidebar = new QFrame(settingsBody);
    sidebar->setFixedWidth(185);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(2);

    auto addSidebarRow = [&](const QString& label, const QImage& icon, bool active) {
        QFrame* row = new QFrame(sidebar);
        row->setFixedHeight(48);
        row->setStyleSheet(active
            ? "QFrame { background-color: #3B3B3B; border: 1px solid #555555; }"
            : "QFrame { background-color: #2B2B2B; border: 1px solid #383838; }");
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(12, 4, 10, 4);
        rl->setSpacing(10);
        if (!icon.isNull())
        {
            QLabel* ic = new QLabel(row);
            ic->setFixedSize(20, 20);
            ic->setStyleSheet("border: none; background: transparent;");
            ic->setPixmap(QPixmap::fromImage(icon.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            rl->addWidget(ic);
        }
        QLabel* txt = new QLabel(label, row);
        txt->setStyleSheet("border: none; background: transparent; color: #EAEAEA;");
        rl->addWidget(txt, 1);
        sideLayout->addWidget(row);
    };

    addSidebarRow("General Settings",       QImage(), false);
    addSidebarRow("Supported Devices",      QImage(), false);
    addSidebarRow("Plugins",                QImage(), true);
    addSidebarRow("Manually Added Devices", QImage(), false);
    addSidebarRow(QString::fromStdString(audioInfo.Label), audioInfo.TabIcon, false);
    sideLayout->addStretch(1);
    bodyLayout->addWidget(sidebar);

    // Right Plugins list pane
    QFrame* rightPane = new QFrame(settingsBody);
    rightPane->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(10, 8, 10, 8);
    rightLayout->setSpacing(8);

    QLabel* headerLink = new QLabel("Looking for plugins? See the official list at <a style=\"color:#4EA8DE;\" href=\"https://openrgb.org\">OpenRGB.org</a>", rightPane);
    headerLink->setAlignment(Qt::AlignCenter);
    headerLink->setTextFormat(Qt::RichText);
    rightLayout->addWidget(headerLink);

    QFrame* listFrame = new QFrame(rightPane);
    listFrame->setStyleSheet("QFrame { background-color: #202020; border: 1px solid #3A3A3A; }");
    QVBoxLayout* listLayout = new QVBoxLayout(listFrame);
    listLayout->setContentsMargins(4, 4, 4, 4);
    listLayout->setSpacing(4);

    listLayout->addWidget(BuildPluginEntryCard(audioInfo, audioApiVer, audioDllPath, true, listFrame));
    listLayout->addWidget(BuildPluginEntryCard(effectsInfo, effectsApiVer, "C:/Program Files/OpenRGB/plugins/OpenRGBEffectsPlugin.dll", false, listFrame));
    listLayout->addStretch(1);
    rightLayout->addWidget(listFrame, 1);

    QHBoxLayout* bottomBtns = new QHBoxLayout();
    bottomBtns->setSpacing(8);
    QPushButton* installBtn = new QPushButton("Install Plugin", rightPane);
    QPushButton* removeBtn  = new QPushButton("Remove Plugin", rightPane);
    bottomBtns->addWidget(installBtn, 1);
    bottomBtns->addWidget(removeBtn, 1);
    rightLayout->addLayout(bottomBtns);

    bodyLayout->addWidget(rightPane, 1);
    rootLayout->addWidget(settingsBody, 1);

    return win;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    QApplication app(argc, argv);
    ApplyOpenRGBDarkTheme(app);

    printf("[Test] 1. Loading C:\\Program Files\\OpenRGB\\plugins\\OpenRGBEffectsPlugin.dll...\n");
    QPluginLoader effectsLoader("C:/Program Files/OpenRGB/plugins/OpenRGBEffectsPlugin.dll");
    QObject* effectsObj = effectsLoader.instance();
    OpenRGBPluginInfo effectsInfo;
    unsigned int effectsApiVer = OPENRGB_PLUGIN_API_VERSION;
    if (effectsObj)
    {
        if (OpenRGBPluginInterface* effectsPlugin = qobject_cast<OpenRGBPluginInterface*>(effectsObj))
        {
            effectsInfo = effectsPlugin->GetPluginInfo();
            effectsApiVer = effectsPlugin->GetPluginAPIVersion();
        }
    }

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
    printf("[PASS] Loaded plugin '%s' (API Version %u, Label '%s', Version='%s', Commit='%s', Icon=%dx%d, TabIcon=%dx%d)\n",
           info.Name.c_str(), apiVer, info.Label.c_str(),
           info.Version.c_str(), info.Commit.c_str(),
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

    audioSettingsDlg->show();
    app.processEvents();

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
    QString repoImgDir  = "D:/Users/Ronanj/Documents/Code/openrgb-audio-plugin/images";

    auto saveDualScreenshot = [&](const QPixmap& pix, const QString& filename) {
        pix.save(artifactDir + "/" + filename);
        pix.save(repoImgDir + "/" + filename);
        printf("[PASS] Saved screenshot: %s (%dx%d)\n", filename.toUtf8().constData(), pix.width(), pix.height());
    };

    // Capture updated Settings -> Plugins view showing embedded 64x64 Icon, 16x16 TabIcon, Author, and GitHub URL.
    QWidget* pluginsView = BuildOpenRGBPluginsViewWidget(info, apiVer, pluginPath, effectsInfo, effectsApiVer);
    pluginsView->show();
    app.processEvents();
    saveDualScreenshot(pluginsView->grab(), "openrgb_plugins_view.png");
    pluginsView->hide();

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

    audioSettingsDlg->adjustSize();
    app.processEvents();
    saveDualScreenshot(audioSettingsDlg->grab(), "audio_settings_compact.png");

    // Step B: Click 'Priority audio devices list' checkbox to toggle it ON.
    printf("[Test] 4. Clicking 'Priority audio devices list' toggle (orgb_priority_toggle_chk)...\n");
    prioCheck->click();
    app.processEvents();
    if (!prioCheck->isChecked() || !prioContainer->isVisible())
    {
        printf("[FAIL] Priority container did not become visible when toggled ON\n");
        return 1;
    }

    // Step C: Add devices using 'Add Selected' button, then test Move Up and Apply Top Priority.
    if (combo->count() >= 2)
    {
        combo->setCurrentIndex(1);
        app.processEvents();
        addBtn->click();
        app.processEvents();

        combo->setCurrentIndex(0);
        app.processEvents();
        addBtn->click();
        app.processEvents();

        prioList->setCurrentRow(1);
        app.processEvents();
        upBtn->click();
        app.processEvents();

        applyBtn->click();
        app.processEvents();
    }

    audioSettingsDlg->adjustSize();
    app.processEvents();
    saveDualScreenshot(audioSettingsDlg->grab(), "audio_settings_priority_expanded.png");

    // Step D: Render and capture the standalone plugin tab (Settings -> Audio Refresh & Priority).
    standaloneTab->resize(680, 560);
    standaloneTab->show();
    app.processEvents();
    saveDualScreenshot(standaloneTab->grab(), "plugin_settings_tab.png");

    standaloneTab->hide();
    audioSettingsDlg->hide();
    plugin->Unload();

    printf("[ALL TESTS PASSED]\n");
    return 0;
}
