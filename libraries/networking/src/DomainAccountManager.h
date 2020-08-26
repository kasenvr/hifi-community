//
//  DomainAccountManager.h
//  libraries/networking/src
//
//  Created by David Rowe on 23 Jul 2020.
//  Copyright 2020 Vircadia contributors.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_DomainAccountManager_h
#define hifi_DomainAccountManager_h

#include <QtCore/QObject>
#include <QtCore/QUrl>

#include <DependencyManager.h>


class DomainAccountManager : public QObject, public Dependency {
    Q_OBJECT
public:
    DomainAccountManager();

    void setAuthURL(const QUrl& authURL);
    void setClientID(const QString& clientID) { _clientID = clientID; }

    QString getUsername() { return _username; }
    QString getAccessToken() { return _access_token; }
    QString getRefreshToken() { return _refresh_token; }
    QString getAuthedDomain() { return _domain_name; }

    bool isLoggedIn();

    Q_INVOKABLE bool checkAndSignalForAccessToken();

public slots:
    void requestAccessToken(const QString& username, const QString& password);
    
    void requestAccessTokenFinished();

signals:
    void authRequired(const QString& domain);
    void loginComplete();
    void loginFailed();
    void logoutComplete();
    void newTokens();

private slots:

private:
    bool hasValidAccessToken();
    bool accessTokenIsExpired();
    void setTokensFromJSON(const QJsonObject&, const QUrl& url);
    void sendInterfaceAccessTokenToServer();

    QUrl _authURL;
    QString _clientID;
    QString _username;      // ####### TODO: Store elsewhere?
    QString _access_token;  // ####... ""
    QString _refresh_token; // ####... ""
    QString _domain_name;   // ####... ""
};

#endif  // hifi_DomainAccountManager_h
