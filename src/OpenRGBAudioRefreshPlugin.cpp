// OpenRGBAudioRefreshPlugin.cpp
// Author: Ronanj Molina (RubyJ / @rjmolina13)
// Repository: https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin
// SPDX-License-Identifier: GPL-2.0-or-later

#include "OpenRGBAudioRefreshPlugin.h"
#include "PluginIconData.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QEvent>
#include <QMetaObject>
#include <QStyle>

#ifdef _WIN32
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <combaseapi.h>
#endif

#ifdef _WIN32
class WasapiNotificationClient : public IMMNotificationClient
{
public:
    explicit WasapiNotificationClient(OpenRGBAudioRefreshPlugin* owner)
        : refCount_(1), owner_(owner)
    {
    }

    virtual ~WasapiNotificationClient() = default;

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&refCount_);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID** ppvInterface) override
    {
        if (ppvInterface == nullptr)
        {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient))
        {
            *ppvInterface = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override
    {
        Q_UNUSED(pwstrDeviceId);
        if (owner_ != nullptr)
        {
            QString stateStr = "state changed";
            if (dwNewState == DEVICE_STATE_ACTIVE)
                stateStr = "device connected/active";
            else if (dwNewState == DEVICE_STATE_UNPLUGGED)
                stateStr = "device unplugged (absent)";
            else if (dwNewState == DEVICE_STATE_DISABLED)
                stateStr = "device disabled (absent)";
            else if (dwNewState == DEVICE_STATE_NOTPRESENT)
                stateStr = "device not present (absent)";

            owner_->NotifyEndpointChangeFromCom(QString("Audio %1").arg(stateStr));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override
    {
        Q_UNUSED(pwstrDeviceId);
        if (owner_ != nullptr)
        {
            owner_->NotifyEndpointChangeFromCom("Audio device added");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override
    {
        Q_UNUSED(pwstrDeviceId);
        if (owner_ != nullptr)
        {
            owner_->NotifyEndpointChangeFromCom("Audio device removed (absent)");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override
    {
        Q_UNUSED(pwstrDefaultDeviceId);
        if (owner_ != nullptr && role == eConsole)
        {
            QString flowName = (flow == eRender) ? "output" : "input";
            owner_->NotifyEndpointChangeFromCom(QString("Default %1 device changed").arg(flowName));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override
    {
        Q_UNUSED(pwstrDeviceId);
        Q_UNUSED(key);
        return S_OK;
    }

private:
    LONG                        refCount_;
    OpenRGBAudioRefreshPlugin*  owner_;
};
#endif

OpenRGBAudioRefreshPlugin::OpenRGBAudioRefreshPlugin()
{
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(350);
    connect(debounceTimer_, &QTimer::timeout, this, &OpenRGBAudioRefreshPlugin::OnDebouncedHardwareEvent);

    discoveryTimer_ = new QTimer(this);
    discoveryTimer_->setInterval(1500);
    connect(discoveryTimer_, &QTimer::timeout, this, &OpenRGBAudioRefreshPlugin::OnPeriodicDiscoveryTick);
}

OpenRGBAudioRefreshPlugin::~OpenRGBAudioRefreshPlugin()
{
    StopWasapiNotifications();
}

#ifndef PLUGIN_VERSION_STR
#define PLUGIN_VERSION_STR "1.0.0"
#endif

#ifndef PLUGIN_COMMIT_STR
#define PLUGIN_COMMIT_STR "5bf9911"
#endif

OpenRGBPluginInfo OpenRGBAudioRefreshPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name           = "OpenRGB Audio Refresh & Priority Plugin";
    info.Description    = "Adds audio device refresh button, auto-refresh and fallback on audio device absence, and toggleable priority audio devices across all effects. Author: Ronanj Molina (RubyJ / @rjmolina13)";
    info.Version        = PLUGIN_VERSION_STR;
    info.Commit         = PLUGIN_COMMIT_STR;
    info.URL            = "https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin";
    info.Location       = OPENRGB_PLUGIN_LOCATION_SETTINGS;
    info.Label          = "Audio Refresh & Priority";
    info.TabIconString  = "";

    QImage iconImg = QImage::fromData(kPluginIconPng, static_cast<int>(kPluginIconPngSize), "PNG");
    if (!iconImg.isNull())
    {
        info.Icon    = iconImg.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        info.TabIcon = iconImg.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return info;
}

unsigned int OpenRGBAudioRefreshPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void OpenRGBAudioRefreshPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resourceManager_ = resource_manager_ptr;
    LoadPersistentConfig();

    if (qApp != nullptr)
    {
        qApp->installEventFilter(this);
    }

    StartWasapiNotifications();

    currentDevices_ = EnumerateWasapiDevicesDirectly();
    lastStatusMessage_ = QString("%1 active audio devices ready").arg(currentDevices_.size());

    discoveryTimer_->start();
    QTimer::singleShot(400, this, &OpenRGBAudioRefreshPlugin::OnPeriodicDiscoveryTick);
    QTimer::singleShot(1200, this, [this]() {
        ScanAndAugmentWidgets();
        if (!initialSyncDone_)
        {
            initialSyncDone_ = true;
            if (usePriorityDevices_ || autoRefreshOnAbsence_)
            {
                RefreshAudioDevices(false, "Initial startup audio sync");
            }
            else
            {
                SyncAllPanelsUi();
            }
        }
    });
}

QWidget* OpenRGBAudioRefreshPlugin::GetWidget()
{
    if (standaloneWidget_.isNull())
    {
        standaloneWidget_ = BuildStandaloneSettingsWidget();
    }
    ScanAndAugmentWidgets();
    SyncAllPanelsUi();
    return standaloneWidget_;
}

QMenu* OpenRGBAudioRefreshPlugin::GetTrayMenu()
{
    if (trayMenu_.isNull())
    {
        trayMenu_ = new QMenu("Audio Devices");

        QAction* refreshAction = trayMenu_->addAction("Refresh Audio Devices");
        connect(refreshAction, &QAction::triggered, this, &OpenRGBAudioRefreshPlugin::OnManualRefreshClicked);

        trayPriorityAction_ = trayMenu_->addAction("Use Priority Audio Devices");
        trayPriorityAction_->setCheckable(true);
        trayPriorityAction_->setChecked(usePriorityDevices_);
        connect(trayPriorityAction_, &QAction::toggled, this, [this](bool checked) {
            if (usePriorityDevices_ == checked)
            {
                return;
            }
            usePriorityDevices_ = checked;
            SavePersistentConfig();
            AppendEventLog(QString("Priority audio devices mode %1").arg(checked ? "enabled" : "disabled"));
            if (usePriorityDevices_)
            {
                RefreshAudioDevices(false, "Priority mode enabled");
            }
            else
            {
                SyncAllPanelsUi();
            }
        });
    }
    return trayMenu_;
}

void OpenRGBAudioRefreshPlugin::Unload()
{
    if (discoveryTimer_ != nullptr)
    {
        discoveryTimer_->stop();
    }
    if (debounceTimer_ != nullptr)
    {
        debounceTimer_->stop();
    }
    if (qApp != nullptr)
    {
        qApp->removeEventFilter(this);
    }
    StopWasapiNotifications();
    SavePersistentConfig();
}

void OpenRGBAudioRefreshPlugin::NotifyEndpointChangeFromCom(const QString& reason)
{
    // Marshal COM background thread notification to the Qt GUI thread.
    QMetaObject::invokeMethod(this, [this, reason]() {
        pendingHardwareReason_ = reason;
        if (debounceTimer_ != nullptr)
        {
            debounceTimer_->start(350);
        }
    }, Qt::QueuedConnection);
}

bool OpenRGBAudioRefreshPlugin::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && watched != nullptr)
    {
        const QEvent::Type t = event->type();
        if (t == QEvent::Show || t == QEvent::Polish)
        {
            if (QWidget* w = qobject_cast<QWidget*>(watched))
            {
                const QString objName = w->objectName();
                const QString clsName = QString::fromLatin1(w->metaObject()->className());
                if (objName == "AudioSettings" || clsName == "AudioSettings")
                {
                    if (QComboBox* combo = w->findChild<QComboBox*>("audio_device"))
                    {
                        AugmentAudioSettingsDialog(w, combo);
                        SyncAllPanelsUi();
                    }
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void OpenRGBAudioRefreshPlugin::OnManualRefreshClicked()
{
    RefreshAudioDevices(true, "Manual refresh button clicked");
}

void OpenRGBAudioRefreshPlugin::OnDebouncedHardwareEvent()
{
    QString reason = pendingHardwareReason_.isEmpty()
        ? QString("Hardware audio endpoint change")
        : pendingHardwareReason_;
    pendingHardwareReason_.clear();

    if (autoRefreshOnAbsence_)
    {
        RefreshAudioDevices(false, reason);
    }
    else
    {
        currentDevices_ = EnumerateWasapiDevicesDirectly();
        lastStatusMessage_ = QString("Device change detected (%1). Click Refresh to update.").arg(reason);
        AppendEventLog(lastStatusMessage_);
        SyncAllPanelsUi();
    }
}

void OpenRGBAudioRefreshPlugin::OnPeriodicDiscoveryTick()
{
    ScanAndAugmentWidgets();

    if (!autoRefreshOnAbsence_ || isRefreshing_)
    {
        return;
    }

    QStringList liveDevices = EnumerateWasapiDevicesDirectly();
    if (liveDevices != currentDevices_)
    {
        RefreshAudioDevices(false, "Detected audio endpoint change");
    }
}

QString OpenRGBAudioRefreshPlugin::GetConfigFilePath() const
{
    QByteArray appDataEnv = qgetenv("APPDATA");
    QString baseDir = appDataEnv.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : QString::fromLocal8Bit(appDataEnv);

    QDir dir(baseDir + "/OpenRGB/plugins/settings");
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    return dir.filePath("AudioRefreshSettings.json");
}

void OpenRGBAudioRefreshPlugin::LoadPersistentConfig()
{
    QFile file(GetConfigFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }

    QJsonObject obj = doc.object();
    autoRefreshOnAbsence_ = obj.value("auto_refresh_on_absence").toBool(true);
    usePriorityDevices_   = obj.value("use_priority_devices").toBool(false);
    lastActiveDeviceName_ = obj.value("last_active_device").toString();

    priorityDevices_.clear();
    QJsonArray arr = obj.value("priority_devices").toArray();
    for (const QJsonValue& val : arr)
    {
        QString dev = val.toString().trimmed();
        if (!dev.isEmpty() && !priorityDevices_.contains(dev))
        {
            priorityDevices_.append(dev);
        }
    }
}

void OpenRGBAudioRefreshPlugin::SavePersistentConfig() const
{
    QJsonObject obj;
    obj["auto_refresh_on_absence"] = autoRefreshOnAbsence_;
    obj["use_priority_devices"]    = usePriorityDevices_;
    obj["last_active_device"]      = lastActiveDeviceName_;

    QJsonArray arr;
    for (const QString& dev : priorityDevices_)
    {
        arr.append(dev);
    }
    obj["priority_devices"] = arr;

    QFile file(GetConfigFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void OpenRGBAudioRefreshPlugin::StartWasapiNotifications()
{
#ifdef _WIN32
    if (pEnumerator_ != nullptr)
    {
        return;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&pEnumerator_));
    if (SUCCEEDED(hr) && pEnumerator_ != nullptr)
    {
        pNotifyClient_ = new WasapiNotificationClient(this);
        pEnumerator_->RegisterEndpointNotificationCallback(pNotifyClient_);
    }
#endif
}

void OpenRGBAudioRefreshPlugin::StopWasapiNotifications()
{
#ifdef _WIN32
    if (pEnumerator_ != nullptr)
    {
        if (pNotifyClient_ != nullptr)
        {
            pEnumerator_->UnregisterEndpointNotificationCallback(pNotifyClient_);
            pNotifyClient_->Release();
            pNotifyClient_ = nullptr;
        }
        pEnumerator_->Release();
        pEnumerator_ = nullptr;
    }
#endif
}

QStringList OpenRGBAudioRefreshPlugin::EnumerateWasapiDevicesDirectly(QString* outDefaultRenderLoopback) const
{
    QStringList result;
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&pEnum));
    if (FAILED(hr) || pEnum == nullptr)
    {
        return result;
    }

    auto readFriendlyName = [](IMMDevice* pEndpoint) -> QString {
        if (pEndpoint == nullptr)
        {
            return QString();
        }
        IPropertyStore* pProps = nullptr;
        if (FAILED(pEndpoint->OpenPropertyStore(STGM_READ, &pProps)) || pProps == nullptr)
        {
            return QString();
        }
        PROPVARIANT varName;
        PropVariantInit(&varName);
        QString name;
        if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.pwszVal != nullptr)
        {
            name = QString::fromWCharArray(varName.pwszVal);
        }
        PropVariantClear(&varName);
        pProps->Release();
        return name;
    };

    if (outDefaultRenderLoopback != nullptr)
    {
        outDefaultRenderLoopback->clear();
        IMMDevice* pDefault = nullptr;
        if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDefault)) && pDefault != nullptr)
        {
            QString defName = readFriendlyName(pDefault);
            if (!defName.isEmpty())
            {
                *outDefaultRenderLoopback = defName + " (Loopback)";
            }
            pDefault->Release();
        }
    }

    IMMDeviceCollection* pRenderCollection = nullptr;
    if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pRenderCollection)) && pRenderCollection != nullptr)
    {
        UINT count = 0;
        pRenderCollection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* pEndpoint = nullptr;
            if (SUCCEEDED(pRenderCollection->Item(i, &pEndpoint)) && pEndpoint != nullptr)
            {
                QString name = readFriendlyName(pEndpoint);
                if (!name.isEmpty())
                {
                    result.append(name + " (Loopback)");
                }
                pEndpoint->Release();
            }
        }
        pRenderCollection->Release();
    }

    IMMDeviceCollection* pCaptureCollection = nullptr;
    if (SUCCEEDED(pEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCaptureCollection)) && pCaptureCollection != nullptr)
    {
        UINT count = 0;
        pCaptureCollection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* pEndpoint = nullptr;
            if (SUCCEEDED(pCaptureCollection->Item(i, &pEndpoint)) && pEndpoint != nullptr)
            {
                QString name = readFriendlyName(pEndpoint);
                if (!name.isEmpty())
                {
                    result.append(name);
                }
                pEndpoint->Release();
            }
        }
        pCaptureCollection->Release();
    }

    pEnum->Release();
#endif
    return result;
}

bool OpenRGBAudioRefreshPlugin::RefreshEffectsPluginAudioManager(QStringList& outDevices)
{
#ifdef _WIN32
    HMODULE hEffects = GetModuleHandleW(L"OpenRGBEffectsPlugin.dll");
    if (hEffects == nullptr)
    {
        return false;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(hEffects);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }
    IMAGE_NT_HEADERS64* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return false;
    }
    size_t imageSize = nt->OptionalHeader.SizeOfImage;

    using FnAudioManagerGet = void* (__fastcall*)();
    using FnInitAudioDeviceList = void (__fastcall*)(void*);

    FnAudioManagerGet fnGet = nullptr;
    FnInitAudioDeviceList fnInitList = nullptr;

    // Check verified RVAs for OpenRGBEffectsPlugin.dll v0.9+/1.0rc3 first.
    const size_t kKnownGetRva  = 0x03F380;
    const size_t kKnownInitRva = 0x03CEF0;

    if (kKnownGetRva + 32 < imageSize &&
        base[kKnownGetRva + 0] == 0x48 &&
        base[kKnownGetRva + 1] == 0x83 &&
        base[kKnownGetRva + 2] == 0xEC &&
        base[kKnownGetRva + 3] == 0x28 &&
        base[kKnownGetRva + 4] == 0x48 &&
        base[kKnownGetRva + 5] == 0x8B &&
        base[kKnownGetRva + 6] == 0x05)
    {
        fnGet = reinterpret_cast<FnAudioManagerGet>(base + kKnownGetRva);
    }

    if (kKnownInitRva + 32 < imageSize &&
        base[kKnownInitRva + 0] == 0x40 &&
        base[kKnownInitRva + 1] == 0x55 &&
        base[kKnownInitRva + 2] == 0x53 &&
        base[kKnownInitRva + 3] == 0x56 &&
        base[kKnownInitRva + 4] == 0x57 &&
        base[kKnownInitRva + 5] == 0x41 &&
        base[kKnownInitRva + 6] == 0x54)
    {
        fnInitList = reinterpret_cast<FnInitAudioDeviceList>(base + kKnownInitRva);
    }

    // Pattern scan fallback if OpenRGBEffectsPlugin.dll was rebuilt.
    if (fnGet == nullptr)
    {
        for (size_t i = 0x1000; i + 32 < imageSize; ++i)
        {
            if (base[i + 0] == 0x48 && base[i + 1] == 0x83 && base[i + 2] == 0xEC && base[i + 3] == 0x28 &&
                base[i + 4] == 0x48 && base[i + 5] == 0x8B && base[i + 6] == 0x05 &&
                base[i + 11] == 0x48 && base[i + 12] == 0x85 && base[i + 13] == 0xC0 &&
                base[i + 14] == 0x75 && base[i + 15] == 0x1F &&
                base[i + 16] == 0xB9 && base[i + 17] == 0xE0 && base[i + 18] == 0x00 && base[i + 19] == 0x00 && base[i + 20] == 0x00)
            {
                fnGet = reinterpret_cast<FnAudioManagerGet>(base + i);
                break;
            }
        }
    }

    if (fnInitList == nullptr)
    {
        static const uint8_t kInitSig[] = {
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00
        };
        for (size_t i = 0x1000; i + sizeof(kInitSig) < imageSize; ++i)
        {
            if (memcmp(base + i, kInitSig, sizeof(kInitSig)) == 0)
            {
                fnInitList = reinterpret_cast<FnInitAudioDeviceList>(base + i);
                break;
            }
        }
    }

    if (fnGet == nullptr || fnInitList == nullptr)
    {
        return false;
    }

    void* am = fnGet();
    if (am == nullptr)
    {
        return false;
    }

    // Offset +0x30 in AudioManager is devices_capture_threads._Mysize.
    // All effects have already been set to index -1 prior to this call, so _Mysize is 0.
    size_t runningThreadCount = *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(am) + 0x30);
    if (runningThreadCount == 0)
    {
        fnInitList(am);
    }

    // Offset +0x00 is std::vector<char*> known_audio_devices (_Myfirst at +0x00, _Mylast at +0x08).
    char** first = *reinterpret_cast<char***>(reinterpret_cast<uint8_t*>(am) + 0x00);
    char** last  = *reinterpret_cast<char***>(reinterpret_cast<uint8_t*>(am) + 0x08);

    outDevices.clear();
    if (first != nullptr && last != nullptr && last >= first)
    {
        size_t count = static_cast<size_t>(last - first);
        if (count <= 256)
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (first[i] != nullptr)
                {
                    outDevices.append(QString::fromUtf8(first[i]));
                }
            }
        }
    }

    return true;
#else
    Q_UNUSED(outDevices);
    return false;
#endif
}

void OpenRGBAudioRefreshPlugin::ScanAndAugmentWidgets()
{
    if (qApp == nullptr)
    {
        return;
    }

    const QWidgetList allWidgets = QApplication::allWidgets();
    for (QWidget* w : allWidgets)
    {
        if (w == nullptr)
        {
            continue;
        }
        const QString objName = w->objectName();
        const QString clsName = QString::fromLatin1(w->metaObject()->className());
        if (objName == "AudioSettings" || clsName == "AudioSettings")
        {
            if (QComboBox* combo = w->findChild<QComboBox*>("audio_device"))
            {
                AugmentAudioSettingsDialog(w, combo);
            }
        }
    }

    // Prune destroyed panels.
    for (int i = panels_.size() - 1; i >= 0; --i)
    {
        if (panels_[i].hostDialog.isNull())
        {
            panels_.removeAt(i);
        }
    }
}

void OpenRGBAudioRefreshPlugin::AugmentAudioSettingsDialog(QWidget* dialog, QComboBox* combo)
{
    if (dialog == nullptr || combo == nullptr)
    {
        return;
    }
    if (dialog->property("orgb_audio_augmented").toBool())
    {
        return;
    }
    dialog->setProperty("orgb_audio_augmented", true);

    if (combo->currentIndex() >= 0 && !combo->currentText().isEmpty())
    {
        combo->setProperty("orgb_preferred_device", combo->currentText());
        if (lastActiveDeviceName_.isEmpty())
        {
            lastActiveDeviceName_ = combo->currentText();
        }
    }

    InjectedAudioPanel panel;
    panel.hostDialog = dialog;
    panel.deviceCombo = combo;
    panel.isStandaloneSettingsTab = false;

    QWidget* parentFrame = combo->parentWidget();
    QGridLayout* gridLayout = (parentFrame != nullptr)
        ? qobject_cast<QGridLayout*>(parentFrame->layout())
        : nullptr;

    panel.refreshButton = new QPushButton("Refresh", parentFrame != nullptr ? parentFrame : dialog);
    panel.refreshButton->setObjectName("orgb_audio_refresh_btn");
    panel.refreshButton->setToolTip("Re-scan Windows audio output and input devices");
    panel.refreshButton->setCursor(Qt::PointingHandCursor);
    panel.refreshButton->setMinimumWidth(68);

    if (gridLayout != nullptr)
    {
        int idx = gridLayout->indexOf(combo);
        if (idx >= 0)
        {
            int row = 1, col = 1, rowSpan = 1, colSpan = 1;
            gridLayout->getItemPosition(idx, &row, &col, &rowSpan, &colSpan);

            QWidget* comboRowContainer = new QWidget(parentFrame);
            QHBoxLayout* rowLayout = new QHBoxLayout(comboRowContainer);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(6);

            gridLayout->removeWidget(combo);
            combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            combo->setMinimumWidth(190);

            rowLayout->addWidget(combo, 1);
            rowLayout->addWidget(panel.refreshButton, 0);

            gridLayout->addWidget(comboRowContainer, row, col, rowSpan, colSpan);
        }

        int extRow = gridLayout->rowCount();
        QWidget* extContainer = new QWidget(parentFrame);
        QVBoxLayout* extLayout = new QVBoxLayout(extContainer);
        extLayout->setContentsMargins(0, 6, 0, 2);
        extLayout->setSpacing(6);

        QFrame* separator = new QFrame(extContainer);
        separator->setFrameShape(QFrame::NoFrame);
        separator->setFixedHeight(1);
        separator->setStyleSheet("background-color: #484848;");
        extLayout->addWidget(separator);

        QHBoxLayout* togglesLayout = new QHBoxLayout();
        togglesLayout->setContentsMargins(0, 0, 0, 0);
        togglesLayout->setSpacing(14);

        panel.autoRefreshCheck = new QCheckBox("Auto-refresh and switch on device absence", extContainer);
        panel.autoRefreshCheck->setObjectName("orgb_auto_refresh_chk");
        panel.autoRefreshCheck->setToolTip("Automatically refresh the device list and switch to an active device if the current audio device is unplugged or disabled");
        panel.autoRefreshCheck->setChecked(autoRefreshOnAbsence_);

        panel.priorityToggleCheck = new QCheckBox("Priority audio devices list", extContainer);
        panel.priorityToggleCheck->setObjectName("orgb_priority_toggle_chk");
        panel.priorityToggleCheck->setToolTip("Enable an ordered list of priority audio devices that effects automatically prefer when available");
        panel.priorityToggleCheck->setChecked(usePriorityDevices_);

        togglesLayout->addWidget(panel.autoRefreshCheck);
        togglesLayout->addWidget(panel.priorityToggleCheck);
        togglesLayout->addStretch(1);
        extLayout->addLayout(togglesLayout);

        panel.statusLabel = new QLabel(extContainer);
        panel.statusLabel->setObjectName("orgb_audio_status_lbl");
        panel.statusLabel->setWordWrap(true);
        extLayout->addWidget(panel.statusLabel);

        panel.priorityContainer = new QFrame(extContainer);
        panel.priorityContainer->setObjectName("orgb_priority_container");
        static_cast<QFrame*>(panel.priorityContainer.data())->setFrameShape(QFrame::StyledPanel);
        QVBoxLayout* prioLayout = new QVBoxLayout(panel.priorityContainer);
        prioLayout->setContentsMargins(8, 8, 8, 8);
        prioLayout->setSpacing(6);

        QHBoxLayout* prioHeaderLayout = new QHBoxLayout();
        QLabel* prioTitle = new QLabel("Priority Audio Devices (top item has highest priority):", panel.priorityContainer);
        QFont boldFont = prioTitle->font();
        boldFont.setBold(true);
        prioTitle->setFont(boldFont);

        panel.applyPriorityButton = new QPushButton("Apply Top Priority", panel.priorityContainer);
        panel.applyPriorityButton->setObjectName("orgb_apply_priority_btn");
        panel.applyPriorityButton->setToolTip("Switch immediately to the highest-priority connected device in this list");
        panel.applyPriorityButton->setMinimumWidth(126);

        prioHeaderLayout->addWidget(prioTitle);
        prioHeaderLayout->addStretch(1);
        prioHeaderLayout->addWidget(panel.applyPriorityButton);
        prioLayout->addLayout(prioHeaderLayout);

        panel.emptyPriorityLabel = new QLabel(
            "No priority devices configured yet. Select a device in the AudioDevice dropdown above and click 'Add Selected' to set your fallback order.",
            panel.priorityContainer);
        panel.emptyPriorityLabel->setWordWrap(true);
        prioLayout->addWidget(panel.emptyPriorityLabel);

        panel.priorityListWidget = new QListWidget(panel.priorityContainer);
        panel.priorityListWidget->setObjectName("orgb_priority_list");
        panel.priorityListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        panel.priorityListWidget->setMinimumHeight(88);
        panel.priorityListWidget->setMaximumHeight(130);
        prioLayout->addWidget(panel.priorityListWidget);

        QHBoxLayout* prioBtnLayout = new QHBoxLayout();
        prioBtnLayout->setContentsMargins(0, 0, 0, 0);
        prioBtnLayout->setSpacing(6);

        panel.addCurrentButton = new QPushButton("Add Selected", panel.priorityContainer);
        panel.addCurrentButton->setObjectName("orgb_priority_add_btn");
        panel.addCurrentButton->setToolTip("Add the currently selected AudioDevice to the priority list");

        panel.moveUpButton = new QPushButton("Move Up", panel.priorityContainer);
        panel.moveUpButton->setObjectName("orgb_priority_up_btn");
        panel.moveUpButton->setToolTip("Increase priority of the selected device");

        panel.moveDownButton = new QPushButton("Move Down", panel.priorityContainer);
        panel.moveDownButton->setObjectName("orgb_priority_down_btn");
        panel.moveDownButton->setToolTip("Decrease priority of the selected device");

        panel.removeButton = new QPushButton("Remove", panel.priorityContainer);
        panel.removeButton->setObjectName("orgb_priority_remove_btn");
        panel.removeButton->setToolTip("Remove the selected device from the priority list");

        prioBtnLayout->addWidget(panel.addCurrentButton);
        prioBtnLayout->addWidget(panel.moveUpButton);
        prioBtnLayout->addWidget(panel.moveDownButton);
        prioBtnLayout->addWidget(panel.removeButton);
        prioBtnLayout->addStretch(1);
        prioLayout->addLayout(prioBtnLayout);

        panel.priorityContainer->setVisible(usePriorityDevices_);
        extLayout->addWidget(panel.priorityContainer);

        gridLayout->addWidget(extContainer, extRow, 0, 1, 4);
    }

    // Wire up interactive signals for this panel.
    connect(panel.refreshButton, &QPushButton::clicked, this, &OpenRGBAudioRefreshPlugin::OnManualRefreshClicked);

    connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo](int index) {
        if (index >= 0 && !combo->itemText(index).isEmpty())
        {
            QString chosen = combo->itemText(index);
            combo->setProperty("orgb_preferred_device", chosen);
            lastActiveDeviceName_ = chosen;
            SavePersistentConfig();
            lastStatusMessage_ = QString("%1 active devices | Selected: %2").arg(currentDevices_.size()).arg(chosen);
            SyncAllPanelsUi();
        }
    });

    if (panel.autoRefreshCheck != nullptr)
    {
        connect(panel.autoRefreshCheck, &QCheckBox::toggled, this, [this](bool checked) {
            if (autoRefreshOnAbsence_ == checked)
            {
                return;
            }
            autoRefreshOnAbsence_ = checked;
            SavePersistentConfig();
            AppendEventLog(QString("Auto-refresh on device absence %1").arg(checked ? "enabled" : "disabled"));
            SyncAllPanelsUi();
        });
    }

    if (panel.priorityToggleCheck != nullptr)
    {
        connect(panel.priorityToggleCheck, &QCheckBox::toggled, this, [this, dialog](bool checked) {
            if (usePriorityDevices_ != checked)
            {
                usePriorityDevices_ = checked;
                SavePersistentConfig();
                AppendEventLog(QString("Priority audio devices list %1").arg(checked ? "enabled" : "disabled"));
                if (usePriorityDevices_ && !priorityDevices_.isEmpty())
                {
                    RefreshAudioDevices(false, "Priority mode enabled");
                }
                else
                {
                    SyncAllPanelsUi();
                }
            }
            if (dialog != nullptr && dialog->isVisible())
            {
                QTimer::singleShot(0, dialog, [dialog]() {
                    dialog->adjustSize();
                });
            }
        });
    }

    if (panel.addCurrentButton != nullptr)
    {
        connect(panel.addCurrentButton, &QPushButton::clicked, this, [this, combo]() {
            if (combo == nullptr)
            {
                return;
            }
            QString devName = combo->currentText().trimmed();
            if (devName.isEmpty() && combo->count() > 0)
            {
                devName = combo->itemText(0).trimmed();
            }
            if (devName.isEmpty())
            {
                return;
            }
            if (!priorityDevices_.contains(devName))
            {
                priorityDevices_.append(devName);
                SavePersistentConfig();
                AppendEventLog(QString("Added to priority list: %1").arg(devName));
                if (usePriorityDevices_)
                {
                    RefreshAudioDevices(false, "Priority list updated");
                }
                else
                {
                    SyncAllPanelsUi();
                }
            }
        });
    }

    if (panel.moveUpButton != nullptr && panel.priorityListWidget != nullptr)
    {
        QListWidget* listW = panel.priorityListWidget;
        connect(panel.moveUpButton, &QPushButton::clicked, this, [this, listW]() {
            int row = listW->currentRow();
            if (row > 0 && row < priorityDevices_.size())
            {
                priorityDevices_.swapItemsAt(row, row - 1);
                SavePersistentConfig();
                SyncAllPanelsUi();
                listW->setCurrentRow(row - 1);
                if (usePriorityDevices_)
                {
                    RefreshAudioDevices(false, "Priority order updated");
                }
            }
        });
    }

    if (panel.moveDownButton != nullptr && panel.priorityListWidget != nullptr)
    {
        QListWidget* listW = panel.priorityListWidget;
        connect(panel.moveDownButton, &QPushButton::clicked, this, [this, listW]() {
            int row = listW->currentRow();
            if (row >= 0 && row + 1 < priorityDevices_.size())
            {
                priorityDevices_.swapItemsAt(row, row + 1);
                SavePersistentConfig();
                SyncAllPanelsUi();
                listW->setCurrentRow(row + 1);
                if (usePriorityDevices_)
                {
                    RefreshAudioDevices(false, "Priority order updated");
                }
            }
        });
    }

    if (panel.removeButton != nullptr && panel.priorityListWidget != nullptr)
    {
        QListWidget* listW = panel.priorityListWidget;
        connect(panel.removeButton, &QPushButton::clicked, this, [this, listW]() {
            int row = listW->currentRow();
            if (row >= 0 && row < priorityDevices_.size())
            {
                QString removed = priorityDevices_.takeAt(row);
                SavePersistentConfig();
                AppendEventLog(QString("Removed from priority list: %1").arg(removed));
                SyncAllPanelsUi();
                if (!priorityDevices_.isEmpty())
                {
                    listW->setCurrentRow(qMin(row, priorityDevices_.size() - 1));
                }
                if (usePriorityDevices_)
                {
                    RefreshAudioDevices(false, "Priority list updated");
                }
            }
        });
    }

    if (panel.applyPriorityButton != nullptr)
    {
        connect(panel.applyPriorityButton, &QPushButton::clicked, this, [this]() {
            if (!usePriorityDevices_)
            {
                usePriorityDevices_ = true;
                SavePersistentConfig();
            }
            RefreshAudioDevices(true, "Applied top priority audio device");
        });
    }

    if (panel.priorityListWidget != nullptr)
    {
        QListWidget* listW = panel.priorityListWidget;
        connect(listW, &QListWidget::itemSelectionChanged, this, [this, listW]() {
            for (InjectedAudioPanel& p : panels_)
            {
                if (p.priorityListWidget == listW)
                {
                    UpdatePanelPriorityButtons(p);
                }
            }
        });
    }

    panels_.append(panel);
    SyncAllPanelsUi();
}

QWidget* OpenRGBAudioRefreshPlugin::BuildStandaloneSettingsWidget()
{
    QWidget* root = new QWidget();
    root->setObjectName("OpenRGBAudioRefreshSettingsWidget");

    QVBoxLayout* mainLayout = new QVBoxLayout(root);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    QLabel* headerLabel = new QLabel("Audio Device Refresh, Auto-Failover & Priority Routing", root);
    QFont headerFont = headerLabel->font();
    headerFont.setBold(true);
    headerFont.setPointSize(headerFont.pointSize() + 1);
    headerLabel->setFont(headerFont);
    mainLayout->addWidget(headerLabel);

    QLabel* subLabel = new QLabel(
        "Controls below synchronize directly with every effect's 'Audio settings' dialog in OpenRGB Effects Plugin.",
        root);
    subLabel->setWordWrap(true);
    mainLayout->addWidget(subLabel);

    QLabel* authorLabel = new QLabel(
        "Author: <a style=\"color:#4EA8DE;\" href=\"https://github.com/rjmolina13\">Ronanj Molina (RubyJ / @rjmolina13)</a>"
        " &nbsp;|&nbsp; Repository: <a style=\"color:#4EA8DE;\" href=\"https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin\">github.com/rjmolina13/OpenRGBAudioRefreshPlugin</a>",
        root);
    authorLabel->setTextFormat(Qt::RichText);
    authorLabel->setOpenExternalLinks(true);
    authorLabel->setWordWrap(true);
    mainLayout->addWidget(authorLabel);

    QFrame* controlFrame = new QFrame(root);
    controlFrame->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* frameLayout = new QVBoxLayout(controlFrame);
    frameLayout->setContentsMargins(10, 10, 10, 10);
    frameLayout->setSpacing(8);

    InjectedAudioPanel panel;
    panel.hostDialog = root;
    panel.isStandaloneSettingsTab = true;

    QHBoxLayout* deviceRow = new QHBoxLayout();
    QLabel* devLabel = new QLabel("Active AudioDevice:", controlFrame);
    panel.deviceCombo = new QComboBox(controlFrame);
    panel.deviceCombo->setObjectName("orgb_standalone_audio_device");
    panel.deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    panel.refreshButton = new QPushButton("Refresh Audio Devices", controlFrame);
    panel.refreshButton->setToolTip("Re-enumerate Windows WASAPI loopback and capture devices across all effects");
    panel.refreshButton->setMinimumWidth(148);

    deviceRow->addWidget(devLabel);
    deviceRow->addWidget(panel.deviceCombo, 1);
    deviceRow->addWidget(panel.refreshButton);
    frameLayout->addLayout(deviceRow);

    QHBoxLayout* togglesRow = new QHBoxLayout();
    panel.autoRefreshCheck = new QCheckBox("Auto-refresh and switch device upon audio device absence", controlFrame);
    panel.autoRefreshCheck->setChecked(autoRefreshOnAbsence_);

    panel.priorityToggleCheck = new QCheckBox("Enable priority audio devices list", controlFrame);
    panel.priorityToggleCheck->setChecked(usePriorityDevices_);

    togglesRow->addWidget(panel.autoRefreshCheck);
    togglesRow->addWidget(panel.priorityToggleCheck);
    togglesRow->addStretch(1);
    frameLayout->addLayout(togglesRow);

    panel.statusLabel = new QLabel(controlFrame);
    panel.statusLabel->setWordWrap(true);
    frameLayout->addWidget(panel.statusLabel);

    panel.priorityContainer = new QFrame(controlFrame);
    static_cast<QFrame*>(panel.priorityContainer.data())->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* prioLayout = new QVBoxLayout(panel.priorityContainer);
    prioLayout->setContentsMargins(8, 8, 8, 8);
    prioLayout->setSpacing(6);

    QHBoxLayout* prioHeader = new QHBoxLayout();
    QLabel* prioLabel = new QLabel("Priority Audio Devices (ordered highest to lowest):", panel.priorityContainer);
    QFont bFont = prioLabel->font();
    bFont.setBold(true);
    prioLabel->setFont(bFont);

    panel.applyPriorityButton = new QPushButton("Apply Top Priority", panel.priorityContainer);
    panel.applyPriorityButton->setMinimumWidth(126);
    prioHeader->addWidget(prioLabel);
    prioHeader->addStretch(1);
    prioHeader->addWidget(panel.applyPriorityButton);
    prioLayout->addLayout(prioHeader);

    panel.emptyPriorityLabel = new QLabel(
        "No priority devices configured yet. Select a device above and click 'Add Selected' to define your priority order.",
        panel.priorityContainer);
    panel.emptyPriorityLabel->setWordWrap(true);
    prioLayout->addWidget(panel.emptyPriorityLabel);

    panel.priorityListWidget = new QListWidget(panel.priorityContainer);
    panel.priorityListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    panel.priorityListWidget->setMinimumHeight(100);
    prioLayout->addWidget(panel.priorityListWidget);

    QHBoxLayout* prioBtns = new QHBoxLayout();
    panel.addCurrentButton = new QPushButton("Add Selected", panel.priorityContainer);
    panel.moveUpButton     = new QPushButton("Move Up", panel.priorityContainer);
    panel.moveDownButton   = new QPushButton("Move Down", panel.priorityContainer);
    panel.removeButton     = new QPushButton("Remove", panel.priorityContainer);

    prioBtns->addWidget(panel.addCurrentButton);
    prioBtns->addWidget(panel.moveUpButton);
    prioBtns->addWidget(panel.moveDownButton);
    prioBtns->addWidget(panel.removeButton);
    prioBtns->addStretch(1);
    prioLayout->addLayout(prioBtns);

    panel.priorityContainer->setVisible(usePriorityDevices_);
    frameLayout->addWidget(panel.priorityContainer);

    mainLayout->addWidget(controlFrame);

    QLabel* logTitle = new QLabel("Audio Device Activity Log:", root);
    logTitle->setFont(bFont);
    mainLayout->addWidget(logTitle);

    eventLogList_ = new QListWidget(root);
    eventLogList_->setWordWrap(true);
    eventLogList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    eventLogList_->setMinimumHeight(85);
    eventLogList_->setMaximumHeight(140);
    mainLayout->addWidget(eventLogList_);
    mainLayout->addStretch(1);

    // Connect standalone panel signals.
    connect(panel.refreshButton, &QPushButton::clicked, this, &OpenRGBAudioRefreshPlugin::OnManualRefreshClicked);

    QComboBox* stCombo = panel.deviceCombo;
    connect(stCombo, QOverload<int>::of(&QComboBox::activated), this, [this, stCombo](int index) {
        if (index < 0 || index >= currentDevices_.size())
        {
            return;
        }
        QString chosen = stCombo->itemText(index);
        lastActiveDeviceName_ = chosen;
        SavePersistentConfig();

        for (InjectedAudioPanel& p : panels_)
        {
            if (!p.isStandaloneSettingsTab && !p.deviceCombo.isNull())
            {
                p.deviceCombo->setProperty("orgb_preferred_device", chosen);
                if (index < p.deviceCombo->count())
                {
                    p.deviceCombo->setCurrentIndex(index);
                }
            }
        }
        AppendEventLog(QString("Switched active audio device across effects to: %1").arg(chosen));
        SyncAllPanelsUi();
    });

    connect(panel.autoRefreshCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (autoRefreshOnAbsence_ == checked)
        {
            return;
        }
        autoRefreshOnAbsence_ = checked;
        SavePersistentConfig();
        AppendEventLog(QString("Auto-refresh on device absence %1").arg(checked ? "enabled" : "disabled"));
        SyncAllPanelsUi();
    });

    connect(panel.priorityToggleCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (usePriorityDevices_ == checked)
        {
            return;
        }
        usePriorityDevices_ = checked;
        SavePersistentConfig();
        AppendEventLog(QString("Priority audio devices list %1").arg(checked ? "enabled" : "disabled"));
        if (usePriorityDevices_ && !priorityDevices_.isEmpty())
        {
            RefreshAudioDevices(false, "Priority mode enabled");
        }
        else
        {
            SyncAllPanelsUi();
        }
    });

    connect(panel.addCurrentButton, &QPushButton::clicked, this, [this, stCombo]() {
        QString devName = stCombo->currentText().trimmed();
        if (!devName.isEmpty() && !priorityDevices_.contains(devName))
        {
            priorityDevices_.append(devName);
            SavePersistentConfig();
            AppendEventLog(QString("Added to priority list: %1").arg(devName));
            if (usePriorityDevices_)
            {
                RefreshAudioDevices(false, "Priority list updated");
            }
            else
            {
                SyncAllPanelsUi();
            }
        }
    });

    QListWidget* listW = panel.priorityListWidget;
    connect(panel.moveUpButton, &QPushButton::clicked, this, [this, listW]() {
        int row = listW->currentRow();
        if (row > 0 && row < priorityDevices_.size())
        {
            priorityDevices_.swapItemsAt(row, row - 1);
            SavePersistentConfig();
            SyncAllPanelsUi();
            listW->setCurrentRow(row - 1);
            if (usePriorityDevices_)
            {
                RefreshAudioDevices(false, "Priority order updated");
            }
        }
    });

    connect(panel.moveDownButton, &QPushButton::clicked, this, [this, listW]() {
        int row = listW->currentRow();
        if (row >= 0 && row + 1 < priorityDevices_.size())
        {
            priorityDevices_.swapItemsAt(row, row + 1);
            SavePersistentConfig();
            SyncAllPanelsUi();
            listW->setCurrentRow(row + 1);
            if (usePriorityDevices_)
            {
                RefreshAudioDevices(false, "Priority order updated");
            }
        }
    });

    connect(panel.removeButton, &QPushButton::clicked, this, [this, listW]() {
        int row = listW->currentRow();
        if (row >= 0 && row < priorityDevices_.size())
        {
            QString removed = priorityDevices_.takeAt(row);
            SavePersistentConfig();
            AppendEventLog(QString("Removed from priority list: %1").arg(removed));
            SyncAllPanelsUi();
            if (!priorityDevices_.isEmpty())
            {
                listW->setCurrentRow(qMin(row, priorityDevices_.size() - 1));
            }
            if (usePriorityDevices_)
            {
                RefreshAudioDevices(false, "Priority list updated");
            }
        }
    });

    connect(panel.applyPriorityButton, &QPushButton::clicked, this, [this]() {
        if (!usePriorityDevices_)
        {
            usePriorityDevices_ = true;
            SavePersistentConfig();
        }
        RefreshAudioDevices(true, "Applied top priority audio device");
    });

    connect(listW, &QListWidget::itemSelectionChanged, this, [this, listW]() {
        for (InjectedAudioPanel& p : panels_)
        {
            if (p.priorityListWidget == listW)
            {
                UpdatePanelPriorityButtons(p);
            }
        }
    });

    panels_.append(panel);
    AppendEventLog("Audio Refresh & Priority plugin initialized.");
    return root;
}

int OpenRGBAudioRefreshPlugin::DetermineTargetDeviceIndex(QComboBox* combo,
                                                          const QStringList& devices,
                                                          const QString& previousDeviceName,
                                                          const QString& defaultLoopbackName,
                                                          bool previousWasAbsent,
                                                          QString* outSelectionReason) const
{
    if (devices.isEmpty())
    {
        if (outSelectionReason != nullptr)
        {
            *outSelectionReason = "No active audio devices detected";
        }
        return -1;
    }

    auto findDeviceIndex = [&devices](const QString& target) -> int {
        if (target.isEmpty())
        {
            return -1;
        }
        int exact = devices.indexOf(target);
        if (exact >= 0)
        {
            return exact;
        }
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].compare(target, Qt::CaseInsensitive) == 0)
            {
                return i;
            }
        }
        return -1;
    };

    // 1. If Priority Audio Devices mode is toggled ON, pick the highest-priority connected device.
    if (usePriorityDevices_ && !priorityDevices_.isEmpty())
    {
        for (int p = 0; p < priorityDevices_.size(); ++p)
        {
            int idx = findDeviceIndex(priorityDevices_[p]);
            if (idx >= 0)
            {
                if (outSelectionReason != nullptr)
                {
                    *outSelectionReason = QString("Priority #%1: %2").arg(p + 1).arg(devices[idx]);
                }
                return idx;
            }
        }
    }

    // 2. If the previously selected device for this effect is still present, keep it by name.
    if (!previousDeviceName.isEmpty())
    {
        int prevIdx = findDeviceIndex(previousDeviceName);
        if (prevIdx >= 0)
        {
            if (outSelectionReason != nullptr)
            {
                *outSelectionReason = QString("Active: %1").arg(devices[prevIdx]);
            }
            return prevIdx;
        }
    }

    // 3. Check combo's remembered preferred device or last saved active device.
    if (combo != nullptr)
    {
        QString pref = combo->property("orgb_preferred_device").toString();
        int prefIdx = findDeviceIndex(pref);
        if (prefIdx >= 0)
        {
            if (outSelectionReason != nullptr)
            {
                *outSelectionReason = QString("Restored preferred: %1").arg(devices[prefIdx]);
            }
            return prefIdx;
        }
    }

    if (!lastActiveDeviceName_.isEmpty())
    {
        int lastIdx = findDeviceIndex(lastActiveDeviceName_);
        if (lastIdx >= 0)
        {
            if (outSelectionReason != nullptr)
            {
                *outSelectionReason = QString("Active: %1").arg(devices[lastIdx]);
            }
            return lastIdx;
        }
    }

    // 4. Device is absent (or none was selected yet). If auto-switch on absence is enabled,
    // check priority list first, then Windows default loopback endpoint, then index 0.
    if (autoRefreshOnAbsence_ || previousWasAbsent)
    {
        for (int p = 0; p < priorityDevices_.size(); ++p)
        {
            int idx = findDeviceIndex(priorityDevices_[p]);
            if (idx >= 0)
            {
                if (outSelectionReason != nullptr)
                {
                    *outSelectionReason = QString("Fallback (Priority #%1): %2").arg(p + 1).arg(devices[idx]);
                }
                return idx;
            }
        }

        int defIdx = findDeviceIndex(defaultLoopbackName);
        if (defIdx >= 0)
        {
            if (outSelectionReason != nullptr)
            {
                *outSelectionReason = QString("Switched on absence to default: %1").arg(devices[defIdx]);
            }
            return defIdx;
        }

        if (outSelectionReason != nullptr)
        {
            *outSelectionReason = QString("Switched on absence to: %1").arg(devices[0]);
        }
        return 0;
    }

    return 0;
}

void OpenRGBAudioRefreshPlugin::RefreshAudioDevices(bool isManual, const QString& triggerReason)
{
    if (isRefreshing_)
    {
        return;
    }
    isRefreshing_ = true;

    ScanAndAugmentWidgets();

    // Record each panel's currently selected device name and unregister active streams cleanly by setting -1.
    QMap<QComboBox*, QString> previousSelections;
    QMap<QComboBox*, bool>    hadActiveSelection;

    for (InjectedAudioPanel& panel : panels_)
    {
        if (panel.deviceCombo.isNull())
        {
            continue;
        }
        QComboBox* combo = panel.deviceCombo;
        int curIdx = combo->currentIndex();
        QString curText = (curIdx >= 0) ? combo->currentText().trimmed() : QString();
        if (curText.isEmpty())
        {
            curText = combo->property("orgb_preferred_device").toString();
        }
        previousSelections.insert(combo, curText);
        hadActiveSelection.insert(combo, curIdx >= 0);

        if (!panel.isStandaloneSettingsTab && curIdx != -1)
        {
            // Setting index to -1 with signals unblocked makes every active effect call Stop() -> UnRegisterClient().
            combo->setCurrentIndex(-1);
        }
    }

    QString defaultLoopbackName;
    QStringList directWasapiDevices = EnumerateWasapiDevicesDirectly(&defaultLoopbackName);

    QStringList refreshedDevices;
    if (!RefreshEffectsPluginAudioManager(refreshedDevices) || refreshedDevices.isEmpty())
    {
        refreshedDevices = directWasapiDevices;
    }

    currentDevices_ = refreshedDevices;
    QString summaryReason;

    for (InjectedAudioPanel& panel : panels_)
    {
        if (panel.deviceCombo.isNull())
        {
            continue;
        }
        QComboBox* combo = panel.deviceCombo;
        QString prevName = previousSelections.value(combo);
        bool wasSelected = hadActiveSelection.value(combo, false);
        bool prevAbsent  = (!prevName.isEmpty() && !refreshedDevices.contains(prevName));

        combo->blockSignals(true);
        combo->clear();
        for (const QString& dev : refreshedDevices)
        {
            combo->addItem(dev);
        }
        // Reset to -1 while signals are blocked so setting targetIdx (including 0) always emits currentIndexChanged.
        combo->setCurrentIndex(-1);
        combo->blockSignals(false);

        QString selectionReason;
        int targetIdx = DetermineTargetDeviceIndex(
            combo, refreshedDevices, prevName, defaultLoopbackName, prevAbsent, &selectionReason);

        if (targetIdx >= 0 && targetIdx < combo->count())
        {
            if (panel.isStandaloneSettingsTab)
            {
                combo->blockSignals(true);
                combo->setCurrentIndex(targetIdx);
                combo->blockSignals(false);
            }
            else if (wasSelected || usePriorityDevices_ || prevAbsent || isManual)
            {
                combo->setCurrentIndex(targetIdx);
                lastActiveDeviceName_ = combo->currentText();
            }
        }

        if (summaryReason.isEmpty() && !selectionReason.isEmpty())
        {
            summaryReason = selectionReason;
        }
    }

    if (refreshedDevices.isEmpty())
    {
        lastStatusMessage_ = "0 active audio devices found. Connect or enable an audio endpoint and click Refresh.";
    }
    else if (!summaryReason.isEmpty())
    {
        lastStatusMessage_ = QString("%1 devices available | %2").arg(refreshedDevices.size()).arg(summaryReason);
    }
    else
    {
        lastStatusMessage_ = QString("%1 devices available").arg(refreshedDevices.size());
    }

    SavePersistentConfig();
    AppendEventLog(QString("%1 -> %2").arg(triggerReason, lastStatusMessage_));
    SyncAllPanelsUi();

    isRefreshing_ = false;
}

void OpenRGBAudioRefreshPlugin::SyncAllPanelsUi()
{
    if (!trayPriorityAction_.isNull())
    {
        trayPriorityAction_->blockSignals(true);
        trayPriorityAction_->setChecked(usePriorityDevices_);
        trayPriorityAction_->blockSignals(false);
    }

    for (InjectedAudioPanel& panel : panels_)
    {
        if (panel.hostDialog.isNull())
        {
            continue;
        }

        if (panel.isStandaloneSettingsTab && !panel.deviceCombo.isNull())
        {
            QComboBox* stCombo = panel.deviceCombo;
            stCombo->blockSignals(true);
            stCombo->clear();
            for (const QString& dev : currentDevices_)
            {
                stCombo->addItem(dev);
            }
            int activeIdx = currentDevices_.indexOf(lastActiveDeviceName_);
            if (activeIdx < 0 && !currentDevices_.isEmpty())
            {
                activeIdx = 0;
            }
            stCombo->setCurrentIndex(activeIdx);
            stCombo->blockSignals(false);
        }

        if (!panel.autoRefreshCheck.isNull())
        {
            panel.autoRefreshCheck->blockSignals(true);
            panel.autoRefreshCheck->setChecked(autoRefreshOnAbsence_);
            panel.autoRefreshCheck->blockSignals(false);
        }

        if (!panel.priorityToggleCheck.isNull())
        {
            panel.priorityToggleCheck->blockSignals(true);
            panel.priorityToggleCheck->setChecked(usePriorityDevices_);
            panel.priorityToggleCheck->blockSignals(false);
        }

        if (!panel.priorityContainer.isNull())
        {
            panel.priorityContainer->setVisible(usePriorityDevices_);
        }

        if (!panel.statusLabel.isNull())
        {
            panel.statusLabel->setText(lastStatusMessage_);
        }

        if (!panel.emptyPriorityLabel.isNull())
        {
            panel.emptyPriorityLabel->setVisible(priorityDevices_.isEmpty());
        }

        if (!panel.priorityListWidget.isNull())
        {
            QListWidget* listW = panel.priorityListWidget;
            int prevRow = listW->currentRow();
            listW->blockSignals(true);
            listW->clear();

            for (int i = 0; i < priorityDevices_.size(); ++i)
            {
                const QString& dev = priorityDevices_[i];
                bool isConnected = currentDevices_.contains(dev);
                QString itemText = QString("%1. %2  [%3]")
                    .arg(i + 1)
                    .arg(dev)
                    .arg(isConnected ? "Connected" : "Absent");
                QListWidgetItem* item = new QListWidgetItem(itemText, listW);
                item->setData(Qt::UserRole, dev);
            }

            if (prevRow >= 0 && prevRow < listW->count())
            {
                listW->setCurrentRow(prevRow);
            }
            else if (listW->count() > 0)
            {
                listW->setCurrentRow(0);
            }
            listW->blockSignals(false);
        }

        UpdatePanelPriorityButtons(panel);
    }
}

void OpenRGBAudioRefreshPlugin::UpdatePanelPriorityButtons(InjectedAudioPanel& panel)
{
    int count = priorityDevices_.size();
    int row = (!panel.priorityListWidget.isNull()) ? panel.priorityListWidget->currentRow() : -1;
    bool hasSelection = (row >= 0 && row < count);

    if (!panel.moveUpButton.isNull())
    {
        panel.moveUpButton->setEnabled(hasSelection && row > 0);
    }
    if (!panel.moveDownButton.isNull())
    {
        panel.moveDownButton->setEnabled(hasSelection && (row + 1 < count));
    }
    if (!panel.removeButton.isNull())
    {
        panel.removeButton->setEnabled(hasSelection);
    }
    if (!panel.applyPriorityButton.isNull())
    {
        panel.applyPriorityButton->setEnabled(count > 0);
    }
    if (!panel.addCurrentButton.isNull() && !panel.deviceCombo.isNull())
    {
        panel.addCurrentButton->setEnabled(panel.deviceCombo->count() > 0);
    }
}

void OpenRGBAudioRefreshPlugin::AppendEventLog(const QString& message)
{
    if (eventLogList_.isNull())
    {
        return;
    }
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    eventLogList_->insertItem(0, QString("[%1] %2").arg(timestamp, message));
    while (eventLogList_->count() > 50)
    {
        delete eventLogList_->takeItem(eventLogList_->count() - 1);
    }
}
