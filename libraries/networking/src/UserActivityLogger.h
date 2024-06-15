//
//  UserActivityLogger.h
//
//
//  Created by Clement on 5/21/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_UserActivityLogger_h
#define hifi_UserActivityLogger_h

#include "AccountManager.h"

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkReply>
#include <QElapsedTimer>

#include <SettingHandle.h>
#include "AddressManager.h"

const QString USER_ACTIVITY_URL = "/api/v1/user_activities";

class UserActivityLogger : public QObject {
    Q_OBJECT
    
public:
    static UserActivityLogger& getInstance();
    
public slots:
    bool isEnabled() { return !_disabled.get(); }
    bool isDisabledSettingSet() const { return _disabled.isSet(); }

    bool isCrashMonitorEnabled() { return !_crashMonitorDisabled.get(); }
    bool isCrashMonitorDisabledSettingSet() const { return _crashMonitorDisabled.isSet(); }	// Is this actually used?

    void disable(bool disable);
    void crashMonitorDisable(bool disable);
    void logAction(QString action, QJsonObject details = QJsonObject(), JSONCallbackParameters params = JSONCallbackParameters());
    
    void launch(QString applicationVersion, bool previousSessionCrashed, int previousSessionRuntime);

    void insufficientGLVersion(const QJsonObject& glData);
    
    void changedDisplayName(QString displayName);
    void changedModel(QString typeOfModel, QString modelURL);
    void changedDomain(QString domainURL);
    void connectedDevice(QString typeOfDevice, QString deviceName);
    void loadedScript(QString scriptName);
    void wentTo(AddressManager::LookupTrigger trigger, QString destinationType, QString destinationName);
    
private slots:
    void requestError(QNetworkReply* errorReply);
    
private:
    UserActivityLogger();
    Setting::Handle<bool> _disabled { "UserActivityLoggerDisabled", true };
    Setting::Handle<bool> _crashMonitorDisabled { "CrashMonitorDisabled2", true };

    QElapsedTimer _timer;
};

#endif // hifi_UserActivityLogger_h
