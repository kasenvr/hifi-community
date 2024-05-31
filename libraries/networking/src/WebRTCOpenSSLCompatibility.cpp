//
//  WebRTCOpenSSLCompatibility.cpp
//  libraries/networking/src
//
//  Created by Dale Glass on 25/05/2022
//  Copyright 2022 Vircadia contributors.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//



#include <openssl/opensslv.h>

/*
 * Starting at OpenSSL 3.0, the names for some of the functions changed.
 *
 * This breaks WebRTC. We have a problem:
 *
 * WebRTC is a pain to build and has its own build environment. Rather than building it
 * locally we provide a prebuilt binary. Said binary is generated against some Google-provided
 * chroot of Debian, so for the most part the result is independent of what you build it
 * on anyway.
 *
 * WebRTC prefers to build and use its own BoringSSL. But we use OpenSSL, and symbol
 * names collide between what comes from the BoringSSL linked into webrtc, and our
 * system library.
 *
 * Therefore, we use the option to link WebRTC against the system OpenSSL. However,
 * starting on OpenSSL 3.0, some symbols were renamed. Backwards compatibility is kept
 * through #defines in the OpenSSL headers, but of course this has no effect on compiled
 * code. We have a WebRTC that calls SSL_get_peer_certificate, but the new OpenSSL now calls
 * this SSL_get1_peer_certificate.
 *
 * New distros using OpenSSL 3.0 now have problems.
 *
 *
 * We have these possible solutions:
 *
 * 1. Have an OpenSSL 3.0 build of WebRTC, and choose which we need. Possible, but a bit painful
 * while OpenSSL 1.x is still in use.
 *
 * 2. Follow the recommendation here:
 * https://stackoverflow.com/questions/71107066/how-to-integrate-part-of-webrtc-as-a-static-dynamic-library-with-the-existing
 * and create a wrapper for WebRTC to make sure the library only exposes a minimal interface and stops
 * conflicting with other things. Then it can be built with BoringSSL and not bother anything else.
 *
 * 3. Try to hack around the issue, which is what we do here, by just providing compatibility wrappers
 * for the missing functions. This is a very simple solution that should work perfectly fine for
 * the time being, so long binary compatibility hasn't been broken somewhere.
 */
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3

#include <openssl/ssl.h>

// These are defined in the OpenSSL headers as aliases for the new names.
// We have to get rid of them so that we can make actual functions with such names.
#undef SSL_get_peer_certificate
#undef EVP_MD_size
#undef EVP_MD_type

extern "C" {
    X509 *SSL_get_peer_certificate(const SSL *ssl) {
        return SSL_get1_peer_certificate(ssl);
    }

    int EVP_MD_size(const EVP_MD *md) {
        return EVP_MD_get_size(md);
    }

    int EVP_MD_type(const EVP_MD *md) {
        return EVP_MD_get_type(md);
    }
}

#endif
