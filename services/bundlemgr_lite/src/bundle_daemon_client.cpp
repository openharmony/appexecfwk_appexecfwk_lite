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

#include "bundle_daemon_client.h"

#include <cstring>
#include <string>

#include "adapter.h"
#include "bundle_daemon_interface.h"
#include "bundle_log.h"
#include "iproxy_client.h"
#include "ohos_errno.h"
#include "samgr_lite.h"
#include "utils.h"
#include "rpc_errno.h"

namespace OHOS {
namespace {
constexpr unsigned SLEEP_TIME = 200000;
}
#ifdef __LINUX__
int BundleDaemonClient::Notify(IOwner owner, int code, IpcIo *reply)
{
    if ((reply == nullptr) || (owner == nullptr)) {
        HILOG_ERROR(HILOG_MODULE_APP, "BundleManager Notify ipc is nullptr");
        return OHOS_FAILURE;
    }
    BundleDaemonClient *client = reinterpret_cast<BundleDaemonClient *>(owner);
    if (client == nullptr) {
        return EC_INVALID;
    }
    ReadInt32(reply, &(client->result_));
    int value;
    sem_getvalue(&client->sem_, &value);
    if (value <= 0) {
        sem_post(&client->sem_);
    }
    return EC_SUCCESS;
}
#else
int32_t BundleDaemonClient::BundleDaemonCallback(uint32_t code, IpcIo* data, IpcIo* reply, MessageOption option)
{
    BundleDaemonClient *client = reinterpret_cast<BundleDaemonClient *>(option.args);
    if (client == nullptr) {
        return EC_INVALID;
    }

    ReadInt32(data, &(client->result_));
    int value;
    sem_getvalue(&client->sem_, &value);
    if (value <= 0) {
        sem_post(&client->sem_);
    }
    return EC_SUCCESS;
}
#endif

void BundleDaemonClient::DeathCallback(void* arg)
{
    pthread_t pid;

    if (pthread_create(&pid, nullptr, RegisterDeathCallback, arg) != 0) {
        BundleDaemonClient *client = reinterpret_cast<BundleDaemonClient *>(arg);
        if (client != nullptr) {
            client->result_ = EC_CANCELED;
            int value;
            sem_getvalue(&client->sem_, &value);
            if (value <= 0) {
                sem_post(&client->sem_);
            }
        }
    }
}

BundleDaemonClient::~BundleDaemonClient()
{
    if (initialized_) {
        RemoveDeathRecipient(bdsSvcIdentity_, cbid_);
        bdsClient_->Release(reinterpret_cast<IUnknown *>(bdsClient_));
        bdsClient_ = nullptr;
        sem_destroy(&sem_);
    }
}

bool BundleDaemonClient::Initialize()
{
    if (initialized_) {
        PRINTI("BundleDaemonClient", "already initialized");
        return true;
    }
    if (sem_init(&sem_, 0, 0) != 0) {
        PRINTE("BundleDaemonClient", "sem_init fail");
        return false;
    }

    while (bdsClient_ == nullptr) {
        IUnknown *iUnknown = SAMGR_GetInstance()->GetDefaultFeatureApi(BDS_SERVICE);
        if (iUnknown == nullptr) {
            usleep(SLEEP_TIME);
            continue;
        }

        (void)iUnknown->QueryInterface(iUnknown, CLIENT_PROXY_VER, (void **)&bdsClient_);
    }

    // register bundle_daemon callback
#ifndef __LINUX__
    objectStub_.func = BundleDaemonClient::BundleDaemonCallback;
    objectStub_.args = this;
    objectStub_.isRemote = false;

    svcIdentity_.handle = IPC_INVALID_HANDLE;
    svcIdentity_.token = SERVICE_TYPE_ANONYMOUS;
    svcIdentity_.cookie = (uintptr_t)&objectStub_;
#endif
    if (RegisterCallback() != ERR_NONE) {
        PRINTE("BundleDaemonClient", "register bundle_daemon callback fail");
        sem_destroy(&sem_);
        return false;
    }

    // register bundle_daemon death callback
    bdsSvcIdentity_ = SAMGR_GetRemoteIdentity(BDS_SERVICE, nullptr);    
    if (::AddDeathRecipient(bdsSvcIdentity_, &BundleDaemonClient::DeathCallback, this, &cbid_) != ERR_NONE) {
        PRINTW("BundleDaemonClient", "register bundle_daemon death callback fail");
        // Keep running if register death callback fail
    }

    initialized_ = true;
    return true;
}

void *BundleDaemonClient::RegisterDeathCallback(void *arg)
{
    BundleDaemonClient *client = reinterpret_cast<BundleDaemonClient *>(arg);
    if (client == nullptr) {
        return nullptr;
    }
    client->result_ = EC_CANCELED;
    int value;
    sem_getvalue(&client->sem_, &value);
    if (value <= 0) {
        sem_post(&client->sem_);
    }
    // Register invoke callback and death callback again
    Lock<Mutex> lock(client->mutex_);
    client->RegisterCallback();
    RemoveDeathRecipient(client->bdsSvcIdentity_, client->cbid_);

    client->cbid_ = INVALID_INDEX;
    client->bdsSvcIdentity_.handle = INVALID_INDEX;
    client->bdsSvcIdentity_.token = INVALID_INDEX;

    client->bdsSvcIdentity_ = SAMGR_GetRemoteIdentity(BDS_SERVICE, nullptr);
    if (::AddDeathRecipient(client->bdsSvcIdentity_, &BundleDaemonClient::DeathCallback,
        client, &client->cbid_) != ERR_NONE) {
        PRINTW("BundleDeamonClient", "register death callback fail");
        // Keep running if register death callback fail
    }

    return nullptr;
}

int32_t BundleDaemonClient::WaitResultSync(int32_t result)
{
    if (result == EC_SUCCESS) {
        sem_wait(&sem_);
        result = result_;
        result_ = EC_FAILURE;
    }
    return result;
}

int32_t BundleDaemonClient::RegisterCallback()
{
    IpcIo request;
    char data[MAX_IO_SIZE];
    IpcIoInit(&request, data, MAX_IO_SIZE, 1);
    bool writeRemote = WriteRemoteObject(&request, &svcIdentity_);
    if (!writeRemote) {
        return EC_FAILURE;
    }
#ifdef __LINUX__
    while (bdsClient_->Invoke(bdsClient_, REGISTER_CALLBACK, &request, this, Notify) != EC_SUCCESS) {
#else
    while (bdsClient_->Invoke(bdsClient_, REGISTER_CALLBACK, &request, nullptr, nullptr) != EC_SUCCESS) {
#endif
        PRINTI("BundleDaemonClient", "register bundle_daemon callback fail");
        usleep(SLEEP_TIME);
    }
    return WaitResultSync(EC_SUCCESS);
}

int32_t BundleDaemonClient::CallClientInvoke(int32_t funcId, const char *firstPath, const char *secondPath)
{
    IpcIo request;
    char data[MAX_IO_SIZE];
    IpcIoInit(&request, data, MAX_IO_SIZE, 0);
    std::string innerStr = firstPath;
    innerStr += secondPath;
    WriteString(&request, innerStr.c_str());

    WriteUint16(&request, strlen(firstPath));

    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, funcId, &request, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, funcId, &request, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::ExtractHap(const char *hapFile, const char *codePath)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (hapFile == nullptr || codePath == nullptr) {
        PRINTE("BundleDaemonClient", "invalid params: hapFile or codePath is nullptr");
        return EC_INVALID;
    }

    return CallClientInvoke(EXTRACT_HAP, hapFile, codePath);
}

int32_t BundleDaemonClient::RenameFile(const char *oldFile, const char *newFile)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (oldFile == nullptr || newFile == nullptr) {
        PRINTE("BundleDaemonClient", "invalid params: oldDir or newDir is nullptr");
        return EC_INVALID;
    }

    return CallClientInvoke(RENAME_DIR, oldFile, newFile);
}

int32_t BundleDaemonClient::CreatePermissionDir()
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, CREATE_PERMISSION_DIR, nullptr, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, CREATE_PERMISSION_DIR, nullptr, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::CreateDataDirectory(const char *dataPath, int32_t uid, int32_t gid, bool isChown)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (dataPath == nullptr) {
        PRINTE("BundleDaemonClient", "invalid params: bundleName is nullptr");
        return EC_INVALID;
    }
    IpcIo request;
    char data[MAX_IO_SIZE];
    IpcIoInit(&request, data, MAX_IO_SIZE, 0);
    WriteString(&request, dataPath);
    WriteInt32(&request, uid);
    WriteInt32(&request, gid);
    WriteBool(&request, isChown);

    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, CREATE_DATA_DIRECTORY, &request, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, CREATE_DATA_DIRECTORY, &request, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::StoreContentToFile(const char *file, const void *buffer, uint32_t size)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (file == nullptr || buffer == nullptr || size == 0) {
        PRINTE("BundleDaemonClient", "invalid params");
        return EC_INVALID;
    }
    IpcIo request;
    char data[MAX_IO_SIZE];

    IpcIoInit(&request, data, MAX_IO_SIZE, 0);

    WriteString(&request, file);
    WriteString(&request, static_cast<const char *>(buffer));
    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, STORE_CONTENT_TO_FILE, &request, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, STORE_CONTENT_TO_FILE, &request, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::MoveFile(const char *oldFile, const char *newFile)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if ((oldFile == nullptr) || (newFile == nullptr)) {
        PRINTE("BundleDaemonClient", "invalid params");
        return EC_INVALID;
    }
    IpcIo request;
    char data[MAX_IO_SIZE];
    IpcIoInit(&request, data, MAX_IO_SIZE, 0);
    WriteString(&request, oldFile);
    WriteString(&request, newFile);

    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, MOVE_FILE, &request, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, MOVE_FILE, &request, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::RemoveFile(const char *file)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (file == nullptr) {
        PRINTE("BundleDaemonClient", "invalid params");
        return EC_INVALID;
    }
    IpcIo request;
    char data[MAX_IO_SIZE];
    IpcIoInit(&request, data, MAX_IO_SIZE, 0);
    WriteString(&request, file);

    Lock<Mutex> lock(mutex_);
#ifdef __LINUX__
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, REMOVE_FILE, &request, this, Notify));
#else
    return WaitResultSync(bdsClient_->Invoke(bdsClient_, REMOVE_FILE, &request, nullptr, nullptr));
#endif
}

int32_t BundleDaemonClient::RemoveInstallDirectory(const char *codePath, const char *dataPath, bool keepData)
{
    if (!initialized_) {
        return EC_NOINIT;
    }
    if (codePath == nullptr || dataPath == nullptr) {
        PRINTE("BundleDaemonClient", "invalid params: bundleName is nullptr");
        return EC_INVALID;
    }

    return CallClientInvoke(REMOVE_INSTALL_DIRECTORY, codePath, dataPath);
}
} // OHOS
