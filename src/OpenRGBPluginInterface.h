/*---------------------------------------------------------*\
| OpenRGBPluginInterface.h                                  |
|                                                           |
|   OpenRGB Plugin Interface (API Version 4 - OpenRGB 1.0)  |
|   Author: Ronanj Molina (RubyJ / @rjmolina13)             |
|   Repo:   https://github.com/rjmolina13/openrgb-audio-plugin
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <QtPlugin>
#include <QLabel>
#include <QMenu>
#include <QImage>
#include <QWidget>

class ResourceManagerInterface;

#define OpenRGBPluginInterface_IID  "com.OpenRGBPluginInterface"
#define OPENRGB_PLUGIN_API_VERSION  4

enum
{
    OPENRGB_PLUGIN_LOCATION_TOP         = 0,
    OPENRGB_PLUGIN_LOCATION_DEVICES     = 1,
    OPENRGB_PLUGIN_LOCATION_INFORMATION = 2,
    OPENRGB_PLUGIN_LOCATION_SETTINGS    = 3,
};

struct OpenRGBPluginInfo
{
    std::string                 Name;
    std::string                 Description;
    std::string                 Version;
    std::string                 Commit;
    std::string                 URL;
    QImage                      Icon;

    unsigned int                Location;
    std::string                 Label;
    std::string                 TabIconString;
    QImage                      TabIcon;
};

class OpenRGBPluginInterface
{
public:
    virtual                    ~OpenRGBPluginInterface() {}

    virtual OpenRGBPluginInfo   GetPluginInfo()                                                     = 0;
    virtual unsigned int        GetPluginAPIVersion()                                               = 0;

    virtual void                Load(ResourceManagerInterface* resource_manager_ptr)                = 0;
    virtual QWidget*            GetWidget()                                                         = 0;
    virtual QMenu*              GetTrayMenu()                                                       = 0;
    virtual void                Unload()                                                            = 0;
};

Q_DECLARE_INTERFACE(OpenRGBPluginInterface, OpenRGBPluginInterface_IID)
