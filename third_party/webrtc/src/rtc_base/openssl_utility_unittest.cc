/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>
#include <vector>

#if defined(WEBRTC_POSIX)
#include <unistd.h>
#endif

#if defined(WEBRTC_WIN)
// Must be included first before openssl headers.
#include "rtc_base/win32.h"  // NOLINT
#endif                       // WEBRTC_WIN

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/pool.h>
#else
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#include "rtc_base/arraysize.h"
#include "rtc_base/checks.h"
#include "rtc_base/gunit.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/openssl.h"
#include "rtc_base/openssl_utility.h"
#include "rtc_base/ssl_roots.h"
#include "test/gmock.h"

namespace rtc {
namespace {
// Fake P-256 key for use with the test certificates below.
const unsigned char kFakeSSLPrivateKey[] = {
    0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
    0x03, 0x01, 0x07, 0x04, 0x6d, 0x30, 0x6b, 0x02, 0x01, 0x01, 0x04, 0x20,
    0x07, 0x0f, 0x08, 0x72, 0x7a, 0xd4, 0xa0, 0x4a, 0x9c, 0xdd, 0x59, 0xc9,
    0x4d, 0x89, 0x68, 0x77, 0x08, 0xb5, 0x6f, 0xc9, 0x5d, 0x30, 0x77, 0x0e,
    0xe8, 0xd1, 0xc9, 0xce, 0x0a, 0x8b, 0xb4, 0x6a, 0xa1, 0x44, 0x03, 0x42,
    0x00, 0x04, 0xe6, 0x2b, 0x69, 0xe2, 0xbf, 0x65, 0x9f, 0x97, 0xbe, 0x2f,
    0x1e, 0x0d, 0x94, 0x8a, 0x4c, 0xd5, 0x97, 0x6b, 0xb7, 0xa9, 0x1e, 0x0d,
    0x46, 0xfb, 0xdd, 0xa9, 0xa9, 0x1e, 0x9d, 0xdc, 0xba, 0x5a, 0x01, 0xe7,
    0xd6, 0x97, 0xa8, 0x0a, 0x18, 0xf9, 0xc3, 0xc4, 0xa3, 0x1e, 0x56, 0xe2,
    0x7c, 0x83, 0x48, 0xdb, 0x16, 0x1a, 0x1c, 0xf5, 0x1d, 0x7e, 0xf1, 0x94,
    0x2d, 0x4b, 0xcf, 0x72, 0x22, 0xc1};

// A self-signed certificate with CN *.webrtc.org and SANs foo.test, *.bar.test,
// and test.webrtc.org.
const unsigned char kFakeSSLCertificate[] = {
    0x30, 0x82, 0x02, 0x9e, 0x30, 0x82, 0x02, 0x42, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x09, 0x00, 0xc8, 0x83, 0x59, 0x4d, 0x90, 0xc3, 0x5f, 0xc8,
    0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02,
    0x05, 0x00, 0x30, 0x81, 0x8d, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55,
    0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x08, 0x0c, 0x02, 0x57, 0x41, 0x31, 0x2c, 0x30, 0x2a, 0x06,
    0x03, 0x55, 0x04, 0x0a, 0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57,
    0x65, 0x62, 0x52, 0x54, 0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65,
    0x73, 0x74, 0x69, 0x6e, 0x67, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55,
    0x04, 0x0b, 0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62,
    0x52, 0x54, 0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74,
    0x69, 0x6e, 0x67, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x0c, 0x0c, 0x2a, 0x2e, 0x77, 0x65, 0x62, 0x72, 0x74, 0x63, 0x2e, 0x6f,
    0x72, 0x67, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x38, 0x30, 0x34, 0x30, 0x33,
    0x32, 0x31, 0x35, 0x34, 0x30, 0x38, 0x5a, 0x17, 0x0d, 0x31, 0x39, 0x30,
    0x34, 0x30, 0x33, 0x32, 0x31, 0x35, 0x34, 0x30, 0x38, 0x5a, 0x30, 0x81,
    0x8d, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02,
    0x55, 0x53, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c,
    0x02, 0x57, 0x41, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62, 0x52, 0x54,
    0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74,
    0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74, 0x69, 0x6e,
    0x67, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x0c, 0x23,
    0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62, 0x52, 0x54, 0x43, 0x20,
    0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x20,
    0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74, 0x69, 0x6e, 0x67, 0x31,
    0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x0c, 0x2a, 0x2e,
    0x77, 0x65, 0x62, 0x72, 0x74, 0x63, 0x2e, 0x6f, 0x72, 0x67, 0x30, 0x59,
    0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    0x04, 0xe6, 0x2b, 0x69, 0xe2, 0xbf, 0x65, 0x9f, 0x97, 0xbe, 0x2f, 0x1e,
    0x0d, 0x94, 0x8a, 0x4c, 0xd5, 0x97, 0x6b, 0xb7, 0xa9, 0x1e, 0x0d, 0x46,
    0xfb, 0xdd, 0xa9, 0xa9, 0x1e, 0x9d, 0xdc, 0xba, 0x5a, 0x01, 0xe7, 0xd6,
    0x97, 0xa8, 0x0a, 0x18, 0xf9, 0xc3, 0xc4, 0xa3, 0x1e, 0x56, 0xe2, 0x7c,
    0x83, 0x48, 0xdb, 0x16, 0x1a, 0x1c, 0xf5, 0x1d, 0x7e, 0xf1, 0x94, 0x2d,
    0x4b, 0xcf, 0x72, 0x22, 0xc1, 0xa3, 0x81, 0x86, 0x30, 0x81, 0x83, 0x30,
    0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xb7, 0xc0,
    0x9a, 0xa7, 0x22, 0xaf, 0xf8, 0x7d, 0xff, 0x68, 0xdb, 0x80, 0xac, 0x0a,
    0xb6, 0xdc, 0x64, 0x89, 0xdb, 0xd4, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d,
    0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xb7, 0xc0, 0x9a, 0xa7, 0x22,
    0xaf, 0xf8, 0x7d, 0xff, 0x68, 0xdb, 0x80, 0xac, 0x0a, 0xb6, 0xdc, 0x64,
    0x89, 0xdb, 0xd4, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01,
    0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30, 0x30, 0x06, 0x03,
    0x55, 0x1d, 0x11, 0x04, 0x29, 0x30, 0x27, 0x82, 0x08, 0x66, 0x6f, 0x6f,
    0x2e, 0x74, 0x65, 0x73, 0x74, 0x82, 0x0a, 0x2a, 0x2e, 0x62, 0x61, 0x72,
    0x2e, 0x74, 0x65, 0x73, 0x74, 0x82, 0x0f, 0x74, 0x65, 0x73, 0x74, 0x2e,
    0x77, 0x65, 0x62, 0x72, 0x74, 0x63, 0x2e, 0x6f, 0x72, 0x67, 0x30, 0x0c,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x05, 0x00,
    0x03, 0x48, 0x00, 0x30, 0x45, 0x02, 0x21, 0x00, 0x81, 0xcb, 0xe2, 0xf9,
    0x04, 0xba, 0xf7, 0xfd, 0x3f, 0x0d, 0x56, 0x37, 0xdb, 0x65, 0x68, 0x07,
    0x28, 0x8d, 0xc5, 0xe1, 0x73, 0xb7, 0xce, 0xa5, 0x20, 0x65, 0x15, 0xb2,
    0xc6, 0x37, 0x8c, 0x5a, 0x02, 0x20, 0x24, 0x62, 0x74, 0xe8, 0xd9, 0x80,
    0x78, 0x2a, 0xbb, 0x87, 0xff, 0x49, 0x99, 0xdb, 0x94, 0xab, 0x06, 0x91,
    0xc0, 0x7a, 0xa4, 0x62, 0x61, 0x98, 0x97, 0x47, 0xb7, 0x64, 0x2b, 0x99,
    0xc3, 0x71};

// A self-signed SSL certificate with only the legacy CN field *.webrtc.org.
const unsigned char kFakeSSLCertificateLegacy[] = {
    0x30, 0x82, 0x02, 0x6a, 0x30, 0x82, 0x02, 0x0e, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x09, 0x00, 0xc8, 0x83, 0x59, 0x4d, 0x90, 0xc3, 0x5f, 0xc8,
    0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02,
    0x05, 0x00, 0x30, 0x81, 0x8d, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55,
    0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x08, 0x0c, 0x02, 0x57, 0x41, 0x31, 0x2c, 0x30, 0x2a, 0x06,
    0x03, 0x55, 0x04, 0x0a, 0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57,
    0x65, 0x62, 0x52, 0x54, 0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65,
    0x73, 0x74, 0x69, 0x6e, 0x67, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55,
    0x04, 0x0b, 0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62,
    0x52, 0x54, 0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74,
    0x69, 0x6e, 0x67, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x0c, 0x0c, 0x2a, 0x2e, 0x77, 0x65, 0x62, 0x72, 0x74, 0x63, 0x2e, 0x6f,
    0x72, 0x67, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x38, 0x30, 0x34, 0x30, 0x33,
    0x32, 0x31, 0x35, 0x34, 0x30, 0x38, 0x5a, 0x17, 0x0d, 0x31, 0x39, 0x30,
    0x34, 0x30, 0x33, 0x32, 0x31, 0x35, 0x34, 0x30, 0x38, 0x5a, 0x30, 0x81,
    0x8d, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02,
    0x55, 0x53, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c,
    0x02, 0x57, 0x41, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x0c, 0x23, 0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62, 0x52, 0x54,
    0x43, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74,
    0x65, 0x20, 0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74, 0x69, 0x6e,
    0x67, 0x31, 0x2c, 0x30, 0x2a, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x0c, 0x23,
    0x46, 0x61, 0x6b, 0x65, 0x20, 0x57, 0x65, 0x62, 0x52, 0x54, 0x43, 0x20,
    0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x20,
    0x46, 0x6f, 0x72, 0x20, 0x54, 0x65, 0x73, 0x74, 0x69, 0x6e, 0x67, 0x31,
    0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x0c, 0x2a, 0x2e,
    0x77, 0x65, 0x62, 0x72, 0x74, 0x63, 0x2e, 0x6f, 0x72, 0x67, 0x30, 0x59,
    0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    0x04, 0xe6, 0x2b, 0x69, 0xe2, 0xbf, 0x65, 0x9f, 0x97, 0xbe, 0x2f, 0x1e,
    0x0d, 0x94, 0x8a, 0x4c, 0xd5, 0x97, 0x6b, 0xb7, 0xa9, 0x1e, 0x0d, 0x46,
    0xfb, 0xdd, 0xa9, 0xa9, 0x1e, 0x9d, 0xdc, 0xba, 0x5a, 0x01, 0xe7, 0xd6,
    0x97, 0xa8, 0x0a, 0x18, 0xf9, 0xc3, 0xc4, 0xa3, 0x1e, 0x56, 0xe2, 0x7c,
    0x83, 0x48, 0xdb, 0x16, 0x1a, 0x1c, 0xf5, 0x1d, 0x7e, 0xf1, 0x94, 0x2d,
    0x4b, 0xcf, 0x72, 0x22, 0xc1, 0xa3, 0x53, 0x30, 0x51, 0x30, 0x1d, 0x06,
    0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xb7, 0xc0, 0x9a, 0xa7,
    0x22, 0xaf, 0xf8, 0x7d, 0xff, 0x68, 0xdb, 0x80, 0xac, 0x0a, 0xb6, 0xdc,
    0x64, 0x89, 0xdb, 0xd4, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04,
    0x18, 0x30, 0x16, 0x80, 0x14, 0xb7, 0xc0, 0x9a, 0xa7, 0x22, 0xaf, 0xf8,
    0x7d, 0xff, 0x68, 0xdb, 0x80, 0xac, 0x0a, 0xb6, 0xdc, 0x64, 0x89, 0xdb,
    0xd4, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04,
    0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x05, 0x00, 0x03, 0x48, 0x00, 0x30,
    0x45, 0x02, 0x21, 0x00, 0xae, 0x51, 0xbc, 0x0f, 0x28, 0x29, 0xd9, 0x35,
    0x95, 0xcc, 0x68, 0xf1, 0xc6, 0x3e, 0xfe, 0x56, 0xfd, 0x7f, 0xd2, 0x03,
    0x6d, 0x09, 0xc7, 0x9b, 0x83, 0x93, 0xd6, 0xd0, 0xfe, 0x45, 0x34, 0x7c,
    0x02, 0x20, 0x6b, 0xaa, 0x95, 0x8c, 0xfc, 0x29, 0x5e, 0x5e, 0xc9, 0xf5,
    0x84, 0x0b, 0xc7, 0x15, 0x86, 0xc3, 0xfc, 0x48, 0x55, 0xb5, 0x81, 0x94,
    0x73, 0xbd, 0x18, 0xcd, 0x9d, 0x92, 0x47, 0xaa, 0xfd, 0x18};

#ifdef OPENSSL_IS_BORINGSSL
enum ssl_verify_result_t DummyVerifyCallback(SSL* ssl, uint8_t* out_alert) {
  return ssl_verify_ok;
}
#endif

// Creates a client SSL that has completed handshaking with a server that uses
// the specified certificate (which must have private key kFakeSSLPrivateKey).
// The server is deallocated. This client will have a peer certificate available
// and is thus suitable for testing VerifyPeerCertMatchesHost.
SSL* CreateSSLWithPeerCertificate(const unsigned char* cert, size_t cert_len) {

  const unsigned char* key_ptr = kFakeSSLPrivateKey;
  EVP_PKEY* key = d2i_PrivateKey(
      EVP_PKEY_EC, nullptr, &key_ptr,
      checked_cast<long>(arraysize(kFakeSSLPrivateKey)));  // NOLINT
  RTC_CHECK(key);

#ifdef OPENSSL_IS_BORINGSSL
  SSL_CTX* ctx = SSL_CTX_new(TLS_with_buffers_method());
#else
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
#endif
  SSL* client = SSL_new(ctx);
  SSL* server = SSL_new(ctx);
  SSL_set_connect_state(client);
  SSL_set_accept_state(server);

#ifdef OPENSSL_IS_BORINGSSL
  bssl::UniquePtr<CRYPTO_BUFFER> cert_buffer(CRYPTO_BUFFER_new(
      static_cast<const uint8_t*>(cert), cert_len, openssl::GetBufferPool()));
  RTC_CHECK(cert_buffer);
  std::vector<CRYPTO_BUFFER*> cert_buffers;
  cert_buffers.push_back(cert_buffer.get());
  RTC_CHECK(1 == SSL_set_chain_and_key(server, cert_buffers.data(),
                                       cert_buffers.size(), key, nullptr));
  // When using crypto buffers we don't get any built-in verification.
  SSL_set_custom_verify(client, SSL_VERIFY_PEER, DummyVerifyCallback);
#else
  X509* x509 =
      d2i_X509(nullptr, &cert, checked_cast<long>(cert_len));  // NOLINT
  RTC_CHECK(x509);
  RTC_CHECK(SSL_use_certificate(server, x509));
  RTC_CHECK(SSL_use_PrivateKey(server, key));
#endif

  BIO* bio1;
  BIO* bio2;
  BIO_new_bio_pair(&bio1, 0, &bio2, 0);
  // SSL_set_bio takes ownership of the BIOs.
  SSL_set_bio(client, bio1, bio1);
  SSL_set_bio(server, bio2, bio2);

  for (;;) {
    int client_ret = SSL_do_handshake(client);
    int client_err = SSL_get_error(client, client_ret);
    RTC_CHECK(client_err == SSL_ERROR_NONE ||
              client_err == SSL_ERROR_WANT_READ ||
              client_err == SSL_ERROR_WANT_WRITE);

    int server_ret = SSL_do_handshake(server);
    int server_err = SSL_get_error(server, server_ret);
    RTC_CHECK(server_err == SSL_ERROR_NONE ||
              server_err == SSL_ERROR_WANT_READ ||
              server_err == SSL_ERROR_WANT_WRITE);

    if (client_ret == 1 && server_ret == 1) {
      break;
    }
  }

  SSL_free(server);
  SSL_CTX_free(ctx);
  EVP_PKEY_free(key);
#ifndef OPENSSL_IS_BORINGSSL
  X509_free(x509);
#endif
  return client;
}
}  // namespace

TEST(OpenSSLUtilityTest, VerifyPeerCertMatchesHostFailsOnNoPeerCertificate) {
#ifdef OPENSSL_IS_BORINGSSL
  SSL_CTX* ssl_ctx = SSL_CTX_new(DTLS_with_buffers_method());
#else
  SSL_CTX* ssl_ctx = SSL_CTX_new(DTLS_method());
#endif
  SSL* ssl = SSL_new(ssl_ctx);

  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "webrtc.org"));

  SSL_free(ssl);
  SSL_CTX_free(ssl_ctx);
}

TEST(OpenSSLUtilityTest, VerifyPeerCertMatchesHost) {
  SSL* ssl = CreateSSLWithPeerCertificate(kFakeSSLCertificate,
                                          arraysize(kFakeSSLCertificate));

  // Each of the names in the SAN list is valid.
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "foo.test"));
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "a.bar.test"));
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "b.bar.test"));
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "test.webrtc.org"));

  // If the SAN list is present, the CN is not checked for hosts.
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "www.webrtc.org"));

  // Additional cases around wildcards.
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "a.b.bar.test"));
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "notbar.test"));
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "bar.test"));

  SSL_free(ssl);
}

TEST(OpenSSLUtilityTest, VerifyPeerCertMatchesHostLegacy) {
  SSL* ssl = CreateSSLWithPeerCertificate(kFakeSSLCertificateLegacy,
                                          arraysize(kFakeSSLCertificateLegacy));

  // If there is no SAN list, WebRTC still implements the legacy mechanism which
  // checks the CN, no longer supported by modern browsers.
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "www.webrtc.org"));
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "alice.webrtc.org"));
  EXPECT_TRUE(openssl::VerifyPeerCertMatchesHost(ssl, "bob.webrtc.org"));

  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "a.b.webrtc.org"));
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "notwebrtc.org"));
  EXPECT_FALSE(openssl::VerifyPeerCertMatchesHost(ssl, "webrtc.org"));

  SSL_free(ssl);
}

}  // namespace rtc
