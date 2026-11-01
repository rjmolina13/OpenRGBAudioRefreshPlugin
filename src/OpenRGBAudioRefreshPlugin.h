// OpenRGBAudioRefreshPlugin.h
// Author: Ronanj Molina (RubyJ / @rjmolina13)
// Repository: https://github.com/rjmolina13/openrgb-audio-plugin
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "OpenRGBPluginInterface.h"

#include <QObject>
#include <QWidget>
#include <QDialog>
#include <QMenu>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QStringList>
#include <QSet>
#include <QVector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#endif

class WasapiNotificationClient;

struct InjectedAudioPanel
{
    QPointer<QWidget>     hostDialog;
    QPointer<QComboBox>   deviceCombo;
    QPointer<QPushButton> refreshButton;
    QPointer<QCheckBox>   autoRefreshCheck;
    QPointer<QCheckBox>   priorityToggleCheck;
    QPointer<QLabel>      statusLabel;
    QPointer<QWidget>     priorityContainer;
    QPointer<QListWidget> priorityListWidget;
    QPointer<QLabel>      emptyPriorityLabel;
    QPointer<QPushButton> addCurrentButton;
    QPointer<QPushButton> moveUpButton;
    QPointer<QPushButton> moveDownButton;
    QPointer<QPushButton> removeButton;
    QPointer<QPushButton> applyPriorityButton;
    bool                  isStandaloneSettingsTab = false;
};

class OpenRGBAudioRefreshPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBAudioRefreshPlugin();
    ~OpenRGBAudioRefreshPlugin() override;

    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;

    void                Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

    void                NotifyEndpointChangeFromCom(const QString& reason);

protected:
    bool                eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void                OnManualRefreshClicked();
    void                OnDebouncedHardwareEvent();
    void                OnPeriodicDiscoveryTick();

private:
    void                LoadPersistentConfig();
    void                SavePersistentConfig() const;
    QString             GetConfigFilePath() const;

    void                StartWasapiNotifications();
    void                StopWasapiNotifications();

    bool                RefreshEffectsPluginAudioManager(QStringList& outDevices);
    QStringList         EnumerateWasapiDevicesDirectly(QString* outDefaultRenderLoopback = nullptr) const;

    void                ScanAndAugmentWidgets();
    void                AugmentAudioSettingsDialog(QWidget* dialog, QComboBox* combo);
    QWidget*            BuildStandaloneSettingsWidget();

    void                RefreshAudioDevices(bool isManual, const QString& triggerReason);
    int                 DetermineTargetDeviceIndex(QComboBox* combo,
                                                   const QStringList& devices,
                                                   const QString& previousDeviceName,
                                                   const QString& defaultLoopbackName,
                                                   bool previousWasAbsent,
                                                   QString* outSelectionReason) const;

    void                SyncAllPanelsUi();
    void                UpdatePanelPriorityButtons(InjectedAudioPanel& panel);
    void                AppendEventLog(const QString& message);

    ResourceManagerInterface*       resourceManager_        = nullptr;
    QPointer<QWidget>               standaloneWidget_;
    QPointer<QMenu>                 trayMenu_;
    QPointer<QAction>               trayPriorityAction_;
    QPointer<QListWidget>           eventLogList_;

    QTimer*                         debounceTimer_          = nullptr;
    QTimer*                         discoveryTimer_         = nullptr;

    QVector<InjectedAudioPanel>     panels_;
    QStringList                     currentDevices_;
    QString                         lastStatusMessage_;
    QString                         pendingHardwareReason_;

    bool                            autoRefreshOnAbsence_   = true;
    bool                            usePriorityDevices_     = false;
    QStringList                     priorityDevices_;
    QString                         lastActiveDeviceName_;
    bool                            isRefreshing_           = false;
    bool                            initialSyncDone_        = false;

#ifdef _WIN32
    IMMDeviceEnumerator*            pEnumerator_            = nullptr;
    WasapiNotificationClient*       pNotifyClient_          = nullptr;
#endif
};
