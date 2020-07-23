//
//  DomainAccountManager.cpp
//  libraries/networking/src
//
//  Created by David Rowe on 23 Jul 2020.
//  Copyright 2020 Vircadia contributors.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "DomainAccountManager.h"

bool DomainAccountManager::hasValidAccessToken() {

    // #######: TODO

    return false;
}

bool DomainAccountManager::checkAndSignalForAccessToken() {
    bool hasToken = hasValidAccessToken();

    if (!hasToken) {
        // Emit a signal so somebody can call back to us and request an access token given a user name and password.
        emit authRequired();
    }

    return hasToken;
}
