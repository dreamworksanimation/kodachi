// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0



#pragma once

#include <stdint.h>

extern "C" {

using KdAttributeHandle = struct FnAttributeStruct*;

using KdBackendHandle = struct KodachiBackendStruct*;
using KdBackendDataMessageHandle = struct KodachiBackendDataMessageStruct*;

constexpr unsigned int KodachiBackendSuite_version = 1;

struct KodachiBackendSuite_v1
{
    KdBackendHandle(*createBackend)();
    void (*releaseBackend)(KdBackendHandle handle);

    KdAttributeHandle(*getStaticData)(KdAttributeHandle configHandle);

    uint8_t (*initialize)(KdBackendHandle handle, KdAttributeHandle opTreeHandle);

    void (*start)(KdBackendHandle handle);
    void (*stop)(KdBackendHandle handle);

    void (*setData)(KdBackendHandle handle,
                    KdAttributeHandle dataHandle);

    KdBackendDataMessageHandle (*getData)(KdBackendHandle, KdAttributeHandle);
    void (*releaseData)(KdBackendDataMessageHandle handle);

    KdAttributeHandle (*getDataAttr)(KdBackendDataMessageHandle);
    void* (*getDataPayload)(KdBackendDataMessageHandle handle, uint64_t idx);
};

} // extern "C"

