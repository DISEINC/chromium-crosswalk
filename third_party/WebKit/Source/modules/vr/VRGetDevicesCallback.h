// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VRGetDevicesCallback_h
#define VRGetDevicesCallback_h

#include "platform/heap/Handle.h"
#include "public/platform/WebCallbacks.h"
#include "public/platform/WebVector.h"
#include "public/platform/modules/vr/vr_service.mojom-blink.h"

namespace blink {

class VRDisplayCollection;
class ScriptPromiseResolver;

// Success and failure callbacks for getDevices.
using WebVRGetDevicesCallback = WebCallbacks<mojo::WTFArray<mojom::blink::VRDeviceInfoPtr>, void>;
class VRGetDevicesCallback final : public WebVRGetDevicesCallback {
    USING_FAST_MALLOC(VRGetDevicesCallback);
public:
    VRGetDevicesCallback(ScriptPromiseResolver*, VRDisplayCollection*);
    ~VRGetDevicesCallback() override;

    void onSuccess(mojo::WTFArray<mojom::blink::VRDeviceInfoPtr>) override;
    void onError() override;

private:
    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<VRDisplayCollection> m_displays;
};

} // namespace blink

#endif // VRGetDevicesCallback_h
