/*
 * Copyright (c) 2020 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bundle_callback.h"

#include "adapter.h"
#include "bundle_callback_utils.h"
#include "bundle_inner_interface.h"
#include "bundle_manager.h"
#include "iproxy_client.h"
#include "log.h"
#include "ohos_types.h"
#include "samgr_lite.h"
#include "rpc_errno.h"

namespace OHOS {
BundleCallback::~BundleCallback()
{
    if (svcIdentity_ != nullptr) {
        TransmitServiceId(*svcIdentity_, false);
        AdapterFree(svcIdentity_);
    }
    callbackMap_.clear();
}

static int Notify(IOwner owner, int code, IpcIo *reply)
{
    if ((reply == nullptr) || (owner == nullptr)) {
        return EC_INVALID;
    }
    uint8_t cmd;
    ReadUint8(reply, &cmd);
    if (cmd != CHANGE_CALLBACK_SERVICE_IDENTITY) {
        return EC_INVALID;
    }
    uint8_t *result = reinterpret_cast<uint8_t *>(owner);
    ReadUint8(reply, result);
    return EC_SUCCESS;
}

int32_t InnerCallback(uint32_t installType, uint8_t resultCode, const char *bundleName)
{
    if (bundleName == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    BundleCallbackInfo callbackInfo = BundleCallback::GetInstance().GetCallbackInfoByName(bundleName);
    BundleStateCallback callback = callbackInfo.bundleStateCallback;
    if (callback == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "InnerCallback is nullptr");
        return ERR_APPEXECFWK_CALLBACK_NULL_CORRESPONDING_CALLBACK;
    }

    if (resultCode != ERR_OK) {
        callback(installType, resultCode, ObtainErrorMessage(resultCode).c_str(), bundleName, callbackInfo.data);
        return ERR_OK;
    }

    if (installType == INSTALL_CALLBACK) {
        callback(installType, resultCode, INSTALL_SUCCESS, bundleName, callbackInfo.data);
        return ERR_OK;
    }

    if (installType == UNINSTALL_CALLBACK) {
        callback(installType, resultCode, UNINSTALL_SUCCESS, bundleName, callbackInfo.data);
        return ERR_OK;
    }
    HILOG_ERROR(HILOG_MODULE_APP, "param installType is invalid!");
    return ERR_APPEXECFWK_CALLBACK_GET_ERROR_INSTALLTYPE;
}

static int32_t Callback(uint32_t code, IpcIo* data, IpcIo* reply, MessageOption option)
{
    if (data == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "BundleCallback data is nullptr");
        return ERR_APPEXECFWK_OBJECT_NULL;
    }

    if ((code == INSTALL_CALLBACK) || (code == UNINSTALL_CALLBACK)) {
        int32_t readCode;
        ReadInt32(data, &readCode);
        uint8_t resultCode = static_cast<uint8_t>(readCode);
        size_t size = 0;
        char *bundleName = reinterpret_cast<char *>(ReadString(data, &size));
        int32_t ret = InnerCallback(code, resultCode, bundleName);
        return ret;
    }
    HILOG_ERROR(HILOG_MODULE_APP, "BundleSelfCallback get error install type");
    return ERR_APPEXECFWK_CALLBACK_GET_ERROR_INSTALLTYPE;
}

int32_t BundleCallback::TransmitServiceId(const SvcIdentity &svc, bool flag)
{
    IUnknown *iUnknown = SAMGR_GetInstance()->GetFeatureApi(BMS_SERVICE, BMS_FEATURE);
    if (iUnknown == nullptr) {
        return ERR_APPEXECFWK_CALLBACK_SERVICEID_TRANSMITTED_FAILED;
    }
    IClientProxy *bmsClient = nullptr;
    int32_t result = iUnknown->QueryInterface(iUnknown, CLIENT_PROXY_VER, reinterpret_cast<void **>(&bmsClient));
    if ((result != 0) || (bmsClient == nullptr)) {
        return ERR_APPEXECFWK_CALLBACK_SERVICEID_TRANSMITTED_FAILED;
    }
    IpcIo ipcIo;
    uint32_t data[MAX_IO_SIZE];
    IpcIoInit(&ipcIo, data, MAX_IO_SIZE, 1);
    WriteBool(&ipcIo, flag);
    bool writeRemote = WriteRemoteObject(&ipcIo, &svc);
    if (!writeRemote) {
        HILOG_ERROR(HILOG_MODULE_APP, "BundleCallback TransmitServiceId ipc failed");
        return ERR_APPEXECFWK_IPCIO_UNAVAILABLED;
    }
    uint8_t errorCode = 0;
    int32_t ret = bmsClient->Invoke(bmsClient, CHANGE_CALLBACK_SERVICE_IDENTITY, &ipcIo, &errorCode, Notify);
    if (ret != OHOS_SUCCESS) {
        HILOG_ERROR(HILOG_MODULE_APP, "TransmitServiceId calling Invoke function failed: %{public}d", ret);
        return ERR_APPEXECFWK_INVOKE_ERROR;
    }

    return errorCode;
}

int32_t BundleCallback::GenerateLocalServiceId()
{
    if (svcIdentity_ != nullptr) {
        return ERR_OK;
    }
    svcIdentity_ = reinterpret_cast<SvcIdentity *>(AdapterMalloc(sizeof(SvcIdentity)));
    if (svcIdentity_ == nullptr) {
        return ERR_APPEXECFWK_CALLBACK_GENERATE_LOCAL_SERVICEID_FAILED;
    }
    
    objectStub_.func = Callback;
    objectStub_.args = nullptr;
    objectStub_.isRemote = false;

    svcIdentity_->handle = IPC_INVALID_HANDLE;
    svcIdentity_->token = SERVICE_TYPE_ANONYMOUS;
    svcIdentity_->cookie = (uintptr_t)&objectStub_;

    int32_t ret = TransmitServiceId(*svcIdentity_, true);
    if (ret != ERR_OK) {
        AdapterFree(svcIdentity_);
        return ERR_APPEXECFWK_CALLBACK_SERVICEID_TRANSMITTED_FAILED;
    }
    return ERR_OK;
}

// register callback by bundle name
int32_t BundleCallback::RegisterBundleStateCallback(const BundleStateCallback &callback,
    const char *bundleName, void *data)
{
    int32_t ret = GenerateLocalServiceId();
    if (ret != ERR_OK) {
        return ERR_APPEXECFWK_CALLBACK_GENERATE_LOCAL_SERVICEID_FAILED;
    }

    if ((bundleName == nullptr) || (strlen(bundleName) == 0)) {
        // monitor all applications by the callback function bundleStateCallback_
        bundleStateCallback_ = callback;
        innerData_ = data;
        callbackMap_.clear();
        return ERR_OK;
    }
    // already monitor all applications
    if (bundleStateCallback_ != nullptr) {
        return ERR_OK;
    }
    std::string innerBundleName = bundleName;
    auto it = callbackMap_.find(innerBundleName);
    if (it != callbackMap_.end()) {
        // already monitor the application
        it->second.bundleStateCallback = callback;
        it->second.data = data;
        return ERR_OK;
    }
    BundleCallbackInfo bundleCallbackInfo;
    bundleCallbackInfo.bundleStateCallback = callback;
    bundleCallbackInfo.data = data;
    callbackMap_.emplace(bundleName, bundleCallbackInfo);
    return ERR_OK;
}

// unregister callback of all application
int32_t BundleCallback::UnregisterBundleStateCallback()
{
    if (svcIdentity_ == nullptr) {
        return ERR_APPEXECFWK_CALLBACK_UNREGISTER_FAILED;
    }

    bundleStateCallback_ = nullptr;
    innerData_ = nullptr;
    callbackMap_.clear();
    (void) TransmitServiceId(*svcIdentity_, false);
    AdapterFree(svcIdentity_);
    return ERR_OK;
}

BundleCallbackInfo BundleCallback::GetCallbackInfoByName(const char *bundleName)
{
    BundleCallbackInfo innerCallbackInfo = { nullptr, nullptr };
    if ((bundleName == nullptr) || (strlen(bundleName) == 0)) {
        return innerCallbackInfo;
    }

    if (bundleStateCallback_ != nullptr) {
        innerCallbackInfo.bundleStateCallback = bundleStateCallback_;
        innerCallbackInfo.data = innerData_;
        return innerCallbackInfo;
    }
    std::string innerBundleName = bundleName;
    auto it = callbackMap_.find(innerBundleName);
    if (it == callbackMap_.end()) {
        return innerCallbackInfo;
    }
    return it->second;
}
} // namespace OHOS
