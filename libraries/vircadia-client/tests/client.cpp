//
//  client.cpp
//  libraries/vircadia-client/tests
//
//  Created by Nshan G. on 17 Mar 2022.
//  Copyright 2022 Vircadia contributors.
//  Copyright 2022 DigiSomni LLC.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "../src/context.h"
#include "../src/node_list.h"
#include "../src/error.h"

#include <thread>
#include <chrono>
#include <catch2/catch.hpp>

#include "common.h"

using namespace std::literals;

TEST_CASE("Client API context creation and domain server connection APIs.", "[client-api-context]") {

    const int context = vircadia_create_context(vircadia_context_defaults());
    REQUIRE(context >= 0);

    vircadia_connect(context, "localhost");

    {
        auto cleanup = defer([context](){
            REQUIRE(vircadia_destroy_context(context) == 0);
        });

        for (int i = 0; i < 10; ++i) {
            int status = vircadia_connection_status(context);
            REQUIRE((status == 1 || status == 0));
            if (status == 1) {
                for (int i = 0; i < 10; ++i) {
                    REQUIRE(vircadia_update_nodes(context) == 0);
                    REQUIRE(vircadia_node_count(context) >= 0);
                    for (int i = 0; i < vircadia_node_count(context) - 1; ++i) {
                        for (int j = i + 1; j < vircadia_node_count(context); ++j) {
                            auto one = vircadia_node_uuid(context, i);
                            auto other = vircadia_node_uuid(context, j);
                            REQUIRE_FALSE(std::equal(one, one + 16, other));
                        }
                    }
                    if (vircadia_node_count(context) > 3) {
                        break;
                    }
                    std::this_thread::sleep_for(1s);
                }
                break;
            }

            std::this_thread::sleep_for(1s);

        }

        const int secondContext = vircadia_create_context(vircadia_context_defaults());
        REQUIRE(secondContext == vircadia_error_context_exists());

        const int secondDestroy = vircadia_destroy_context(secondContext);
        REQUIRE(secondDestroy == vircadia_error_context_invalid());

        const int badDestroy = vircadia_destroy_context(context + 1);
        REQUIRE(badDestroy == vircadia_error_context_invalid());

    }

    const int doubleDestroy = vircadia_destroy_context(context);
    REQUIRE(doubleDestroy == vircadia_error_context_invalid());

}
