//
//  Avatars.h
//  libraries/vircadia-client/src/internal
//
//  Created by Nshan G. on 14 May 2022.
//  Copyright 2022 Vircadia contributors.
//  Copyright 2022 DigiSomni LLC.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef LIBRARIES_VIRCADIA_CLIENT_SRC_INTERNAL_AVATARS_H
#define LIBRARIES_VIRCADIA_CLIENT_SRC_INTERNAL_AVATARS_H

#include <vector>
#include <memory>

#include <AvatarDataStream.h>

#include "Common.h"
#include "../avatars.h"

class ConicalViewFrustumData;

namespace vircadia::client {

    struct AvatarData;
    class AvatarManager;

    /// @private
    class Avatars {

    public:

        void enable();
        void update();
        void updateManager();
        bool isEnabled() const;
        int set(const AvatarData&);

        void destroy();

        AvatarData& myAvatar();
        const std::vector<AvatarData>& all() const;
        const std::vector<std::pair<UUID, KillAvatarReason>>& epitaphs() const;
        std::vector<ConicalViewFrustumData>& views();
    };


} // namespace vircadia::client

#endif /* end of include guard */
