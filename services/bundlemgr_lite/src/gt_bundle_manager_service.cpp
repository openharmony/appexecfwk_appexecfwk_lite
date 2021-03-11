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

#include "gt_bundle_manager_service.h"

#include "ability_info_utils.h"
#include "ability_message_id.h"
#include "appexecfwk_errors.h"
#include "bundle_app_info.h"
#include "bundle_common.h"
#include "bundle_message_id.h"
#include "bundle_util.h"
#include "bundlems_log.h"
#include "cmsis_os2.h"
#include "dirent.h"
#include "fcntl.h"
#include "generate-bytecode.h"
#include "gt_bundle_extractor.h"
#include "gt_bundle_parser.h"
#include "gt_extractor_util.h"
#include "los_tick.h"
#include "stdio.h"
#include "sys/stat.h"
#include "unistd.h"
#include "utils.h"
#include "want.h"

namespace OHOS {
const uint8_t PAIR_VALUE = 2;
const uint8_t FINISHED_PROCESS = 100;

static char *g_currentBundle = nullptr;
const int32_t BUNDLENAME_MUTEX_TIMEOUT = 2000;
static osMutexId_t g_currentBundleMutex;

GtManagerService::GtManagerService()
{
    installer_ = new GtBundleInstaller();
    bundleResList_ = new List<BundleRes *>();
    bundleMap_ = BundleMap::GetInstance();
    systemBundleInstallMsg_ = nullptr;
    jsEngineVer_ = nullptr;
    installedThirdBundleNum_ = 0;
}

GtManagerService::~GtManagerService()
{
    delete installer_;
    installer_ = nullptr;
    delete bundleResList_;
    bundleResList_ = nullptr;
}

void SetCurrentBundle(const char *name)
{
    MutexAcquire(&g_currentBundleMutex, BUNDLENAME_MUTEX_TIMEOUT);
    AdapterFree(g_currentBundle);
    if (name == nullptr) {
        MutexRelease(&g_currentBundleMutex);
        return;
    }

    int len = strlen(name);
    g_currentBundle = (char *) AdapterMalloc(len + 1);
    if (g_currentBundle == nullptr || strncpy_s(g_currentBundle, len + 1, name, len) < 0) {
        AdapterFree(g_currentBundle);
    }
    MutexRelease(&g_currentBundleMutex);
}

bool GtManagerService::Install(const char *hapPath, const InstallParam *installParam,
    InstallerCallback installerCallback)
{
    if (!BundleUtil::IsDir(JSON_PATH_NO_SLASH_END)) {
        BundleUtil::MkDirs(JSON_PATH_NO_SLASH_END);
    }
    if (installer_ == nullptr) {
        installer_ = new GtBundleInstaller();
    }
    if (hapPath == nullptr) {
        return false;
    }
    char *path = reinterpret_cast<char *>(AdapterMalloc(strlen(hapPath) + 1));
    if (path == nullptr) {
        return false;
    }
    if (strncpy_s(path, strlen(hapPath) + 1, hapPath, strlen(hapPath)) != EOK) {
        AdapterFree(path);
        return false;
    }

    char *name = strrchr(path, '/');
    if (name == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Parsed install path is null");
        AdapterFree(path);
        return false;
    }
    DisableServiceWdg();
    uint8_t ret = installer_->Install(path);
    EnableServiceWdg();
    if (installerCallback != nullptr) {
        (*installerCallback)(ret, nullptr);
    }
    AdapterFree(path);
    return true;
}

bool GtManagerService::Uninstall(const char *bundleName, const InstallParam *installParam,
    InstallerCallback installerCallback)
{
    if (installer_ == nullptr) {
        installer_ = new GtBundleInstaller();
    }
    if (bundleName == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] Parsed bundleName to be uninstalled is null");
        return false;
    }
    char *innerBundleName = reinterpret_cast<char *>(AdapterMalloc(strlen(bundleName) + 1));
    if (innerBundleName == nullptr) {
        return false;
    }
    if (strncpy_s(innerBundleName, strlen(bundleName) + 1, bundleName, strlen(bundleName)) != EOK) {
        AdapterFree(innerBundleName);
        return false;
    }
    uint8_t bResult = installer_->Uninstall(innerBundleName);
    if (installerCallback != nullptr) {
        (*installerCallback)(bResult, nullptr);
    }
    SetCurrentBundle(nullptr);
    AdapterFree(innerBundleName);
    return true;
}

uint8_t GtManagerService::QueryAbilityInfo(const Want *want, AbilityInfo *abilityInfo)
{
    if (want->element == nullptr) {
        return 0;
    }
    const char *bundleName = want->element->bundleName;
    BundleInfo *bundleInfo = OHOS::GtManagerService::GetInstance().QueryBundleInfo(bundleName);
    if (bundleInfo == nullptr) {
        return 0;
    }
    AbilityInfo *ability = bundleInfo->abilityInfo;
    if (ability == nullptr) {
        return 0;
    }
    OHOS::AbilityInfoUtils::SetAbilityInfoBundleName(abilityInfo, bundleName);
    OHOS::AbilityInfoUtils::SetAbilityInfoSrcPath(abilityInfo, ability->srcPath);
    return 1;
}

uint8_t GtManagerService::GetBundleInfo(const char *bundleName, int32_t flags, BundleInfo& bundleInfo)
{
    if (bundleMap_ == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    return bundleMap_->GetBundleInfo(bundleName, flags, bundleInfo);
}

uint8_t GtManagerService::GetBundleInfos(const int flags, BundleInfo **bundleInfos, int32_t *len)
{
    if (bundleMap_ == nullptr) {
        return ERR_APPEXECFWK_OBJECT_NULL;
    }
    return bundleMap_->GetBundleInfos(flags, bundleInfos, len);
}

void GtManagerService::InstallAllSystemBundle()
{
    AppInfoList *list = APP_InitAllAppInfo();
    if (list == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] InstallAllSystemBundle InitAllAppInfo fail, list is nullptr");
        return;
    }

    AppInfoList *currentNode = nullptr;
    AppInfoList *nextNode = nullptr;
    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(currentNode, nextNode, &list->appDoubleList, AppInfoList, appDoubleList) {
        if ((strcmp(((AppInfoList *)currentNode)->filePath, ".") == 0) ||
            (strcmp(((AppInfoList *)currentNode)->filePath, "..") == 0)) {
            continue;
        }
        InstallSystemBundle(((AppInfoList *)currentNode)->filePath);
    }
    APP_FreeAllAppInfo(list);
}

void GtManagerService::InstallSystemBundle(const char *systemAppPath)
{
    if (installer_ == nullptr) {
        return;
    }

    if (!BundleUtil::IsFile(systemAppPath) || !BundleUtil::EndWith(systemAppPath, INSTALL_FILE_SUFFIX)) {
        return;
    }

    // delete resource temp dir first
    (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
    // create new bundleInstallMsg
    systemBundleInstallMsg_ = reinterpret_cast<BundleInstallMsg *>(AdapterMalloc(sizeof(BundleInstallMsg)));
    if (systemBundleInstallMsg_ == nullptr) {
        return;
    }
    if (memset_s(systemBundleInstallMsg_, sizeof(BundleInstallMsg), 0, sizeof(BundleInstallMsg)) != EOK) {
        AdapterFree(systemBundleInstallMsg_);
        return;
    }
    // set bundleName、label、smallIconPath、bigIconPath in systemBundleInstallMsg_
    bool ret = GtBundleExtractor::ExtractInstallMsg(systemAppPath, &(systemBundleInstallMsg_->bundleName),
        &(systemBundleInstallMsg_->label), &(systemBundleInstallMsg_->smallIconPath),
        &(systemBundleInstallMsg_->bigIconPath));
    if (!ret) {
        // delete resource temp dir
        (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
        ClearSystemBundleInstallMsg();
        return;
    }
    uint8_t result = installer_->Install(systemAppPath);
    SetCurrentBundle(nullptr);
    HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] install system result is %d", result);
    if (result != ERR_OK) {
        ReportSystemBundleInstallProcess(0, nullptr, BUNDLE_INSTALL_FAIL);
    } else {
        ReportSystemBundleInstallProcess(FINISHED_PROCESS, nullptr, BUNDLE_INSTALL_OK);
    }
    // delete resource temp dir
    ClearSystemBundleInstallMsg();
    (void) BundleUtil::RemoveDir(TMP_RESOURCE_DIR);
}

void GtManagerService::ClearSystemBundleInstallMsg()
{
    if (systemBundleInstallMsg_ == nullptr) {
        return;
    }

    if (systemBundleInstallMsg_->bundleName != nullptr) {
        AdapterFree(systemBundleInstallMsg_->bundleName);
        systemBundleInstallMsg_->bundleName = nullptr;
    }

    if (systemBundleInstallMsg_->label != nullptr) {
        AdapterFree(systemBundleInstallMsg_->label);
        systemBundleInstallMsg_->label = nullptr;
    }

    if (systemBundleInstallMsg_->smallIconPath != nullptr) {
        AdapterFree(systemBundleInstallMsg_->smallIconPath);
        systemBundleInstallMsg_->smallIconPath = nullptr;
    }

    if (systemBundleInstallMsg_->bigIconPath != nullptr) {
        AdapterFree(systemBundleInstallMsg_->bigIconPath);
        systemBundleInstallMsg_->bigIconPath = nullptr;
    }

    AdapterFree(systemBundleInstallMsg_);
    systemBundleInstallMsg_ = nullptr;
}

void GtManagerService::ScanPackages()
{
    if (!BundleUtil::IsDir(JSON_PATH_NO_SLASH_END)) {
        BundleUtil::MkDirs(JSON_PATH_NO_SLASH_END);
        return;
    }

    // get third system bundle uninstall record
    cJSON *uninstallRecord = BundleUtil::GetJsonStream(UNINSTALL_THIRD_SYSTEM_BUNDLE_JSON);
    if (uninstallRecord == nullptr) {
        (void) unlink(UNINSTALL_THIRD_SYSTEM_BUNDLE_JSON);
    }

    List<ToBeInstalledApp *> systemPathList;
    // scan system apps and third system apps
    ScanSystemApp(uninstallRecord, &systemPathList);
    if (uninstallRecord != nullptr) {
        cJSON_Delete(uninstallRecord);
    }
    // scan third apps
    ScanThirdApp(INSTALL_PATH, &systemPathList);
    // install new system app or app which is to be updated
    for (auto node = systemPathList.Begin(); node != systemPathList.End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        InstallSystemBundle(toBeInstalledApp->path);
    }
    RemoveSystemAppPathList(&systemPathList);
}

void GtManagerService::RemoveSystemAppPathList(List<ToBeInstalledApp *> *systemPathList)
{
    if (systemPathList == nullptr) {
        return;
    }

    for (auto node = systemPathList->Begin(); node != systemPathList->End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        AdapterFree(toBeInstalledApp->installedPath);
        AdapterFree(toBeInstalledApp->path);
        UI_Free(toBeInstalledApp);
    }
}

void GtManagerService::ScanSystemApp(const cJSON *uninstallRecord, List<ToBeInstalledApp *> *systemPathList)
{
    AppInfoList *list = APP_InitAllAppInfo();
    if (list == nullptr) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] ScanSystemApp InitAllAppInfo fail, list is nullptr");
        return;
    }

    uint8_t scanFlag = 0;
    char *bundleName = nullptr;
    int32_t versionCode = -1;
    AppInfoList *currentNode = nullptr;
    AppInfoList *nextNode = nullptr;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(currentNode, nextNode, &list->appDoubleList, AppInfoList, appDoubleList) {
        if ((strcmp(((AppInfoList *)currentNode)->filePath, ".") == 0) ||
            (strcmp(((AppInfoList *)currentNode)->filePath, "..") == 0)) {
            continue;
        }

        if (BundleUtil::StartWith(((AppInfoList *)currentNode)->filePath, SYSTEM_BUNDLE_PATH)) {
            scanFlag = SYSTEM_APP_FLAG;
        } else if (BundleUtil::StartWith(((AppInfoList *)currentNode)->filePath, THIRD_SYSTEM_BUNDLE_PATH)) {
            scanFlag = THIRD_SYSTEM_APP_FLAG;
        } else {
            continue; // skip third app
        }

        // scan system app
        bool res = CheckSystemBundleIsValid(((AppInfoList *)currentNode)->filePath, &bundleName, versionCode);
        if (!res) {
            APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_INVALID_SYSTEM_APP);
            AdapterFree(bundleName);
            continue;
        }

        // third system app cannot restore after uninstall
        if (scanFlag == THIRD_SYSTEM_APP_FLAG &&
            CheckThirdSystemBundleHasUninstalled(bundleName, uninstallRecord)) {
            AdapterFree(bundleName);
            continue;
        }

        ReloadEntireBundleInfo(((AppInfoList *)currentNode)->filePath, bundleName,
            systemPathList, versionCode, scanFlag);
        AdapterFree(bundleName);
    }
    APP_FreeAllAppInfo(list);
}

void GtManagerService::ScanThirdApp(const char *appDir, const List<ToBeInstalledApp *> *systemPathList)
{
    dirent *ent = nullptr;

    if (appDir == nullptr) {
        return;
    }

    DIR *dir = opendir(appDir);
    if (dir == nullptr) {
        return;
    }
    while ((ent = readdir(dir)) != nullptr) {
        if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..")) == 0) {
            continue;
        }

        int32_t len = strlen(appDir) + 1 + strlen(ent->d_name) + 1;
        char *appPath = reinterpret_cast<char *>(UI_Malloc(len));
        if (appPath == nullptr) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] malloc fail when reload third app!");
            break;
        }

        if (sprintf_s(appPath, len, "%s/%s", appDir, ent->d_name) < 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] strcat fail when reload third app!");
            UI_Free(appPath);
            break;
        }

        if (!BundleUtil::IsDir(appPath)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] app path is not dir when reload third app!");
            UI_Free(appPath);
            continue;
        }

        if (IsSystemBundleInstalledPath(appPath, systemPathList)) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] app path is not third bundle path!");
            UI_Free(appPath);
            continue;
        }

        if (installedThirdBundleNum_ >= MAX_THIRD_BUNDLE_NUMBER) {
            HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] third bundle reload number is to MAX_THIRD_BUNDLE_NUMBER!");
            UI_Free(appPath);
            continue;
        }

        ReloadEntireBundleInfo(appPath, ent->d_name, nullptr, -1, THIRD_APP_FLAG);
        UI_Free(appPath);
    }
    closedir(dir);
}

bool GtManagerService::IsSystemBundleInstalledPath(const char *appPath, const List<ToBeInstalledApp *> *systemPathList)
{
    if (appPath == nullptr || systemPathList == nullptr) {
        return false;
    }

    for (auto node = systemPathList->Begin(); node != systemPathList->End(); node = node->next_) {
        ToBeInstalledApp *toBeInstalledApp = node->value_;
        if (toBeInstalledApp->installedPath != nullptr &&
            strcmp(appPath, toBeInstalledApp->installedPath) == 0) {
            return true;
        }
    }
    return false;
}

bool GtManagerService::CheckSystemBundleIsValid(const char *appPath, char **bundleName, int32_t &versionCode)
{
    if (appPath == nullptr || bundleName == nullptr) {
        return false;
    }

    if (!BundleUtil::EndWith(appPath, INSTALL_FILE_SUFFIX)) {
        return false;
    }

    if (!GtBundleParser::ParseBundleAttr(appPath, bundleName, versionCode)) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] parse bundle attr fail!");
        return false;
    }

    if (*bundleName != nullptr && strlen(*bundleName) >= MAX_BUNDLE_NAME_LEN) {
        return false;
    }
    return true;
}

void GtManagerService::ReloadEntireBundleInfo(const char *appPath, const char *bundleName,
    List<ToBeInstalledApp *> *systemPathList, int32_t versionCode, uint8_t scanFlag)
{
    char *codePath = nullptr;
    char *appId = nullptr;
    int32_t oldVersionCode = -1;

    if (appPath == nullptr || bundleName == nullptr) {
        APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_UNKNOWN_BUNDLE_INFO);
        return;
    }

    if (QueryBundleInfo(bundleName) != nullptr) {
        HILOG_INFO(HILOG_MODULE_AAFWK, "[BMS] bundle has been reloaded!");
        return;
    }

    bool res = BundleUtil::CheckBundleJsonIsValid(bundleName, &codePath, &appId, oldVersionCode);
    bool isSystemApp = (scanFlag == SYSTEM_APP_FLAG);
    if (scanFlag != THIRD_APP_FLAG) {
        if (!res) {
            AddSystemAppPathList(nullptr, appPath, systemPathList, isSystemApp, false);
            AdapterFree(appId);
            AdapterFree(codePath);
            return;
        }
        if (oldVersionCode < versionCode) {
            AddSystemAppPathList(codePath, appPath, systemPathList, isSystemApp, true);
            AdapterFree(appId);
            AdapterFree(codePath);
            return;
        }
    } else {
        if (!res && !BundleUtil::CheckBundleJsonIsValid(bundleName, &codePath, &appId, oldVersionCode)) {
            RecordAbiityInfoEvt(bundleName);
            APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_PARSE_JSON_FALIED);
            AdapterFree(appId);
            AdapterFree(codePath);
            return;
        }
    }
#ifdef BC_TRANS_ENABLE
    TransformJsToBcWhenRestart(codePath, bundleName);
#endif
    bool ret = ReloadBundleInfo(codePath, appId, isSystemApp);
    if (ret && (scanFlag == THIRD_APP_FLAG)) {
        installedThirdBundleNum_++;
    }
    AdapterFree(appId);
    AdapterFree(codePath);
}

void GtManagerService::AddSystemAppPathList(const char *installedPath, const char *path,
    List<ToBeInstalledApp *> *systemPathList, bool isSystemApp, bool isUpdated)
{
    if (path == nullptr || systemPathList == nullptr) {
        return;
    }

    ToBeInstalledApp *toBeInstalledApp =
        reinterpret_cast<ToBeInstalledApp *>(UI_Malloc(sizeof(ToBeInstalledApp)));
    if (toBeInstalledApp == nullptr) {
        return;
    }
    toBeInstalledApp->installedPath = Utils::Strdup(installedPath);
    toBeInstalledApp->path = Utils::Strdup(path);
    toBeInstalledApp->isSystemApp = isSystemApp;
    toBeInstalledApp->isUpdated = isUpdated;
    systemPathList->PushBack(toBeInstalledApp);
}

bool GtManagerService::ReloadBundleInfo(const char *profileDir, const char *appId, bool isSystemApp)
{
    if (profileDir == nullptr) {
        return false;
    }
    BundleRes *bundleRes = reinterpret_cast<BundleRes *>(AdapterMalloc(sizeof(BundleRes)));
    if (bundleRes == nullptr) {
        return false;
    }
    if (memset_s(bundleRes, sizeof(BundleRes), 0, sizeof(BundleRes)) != EOK) {
        AdapterFree(bundleRes);
        return false;
    }
    BundleInfo *bundleInfo = GtBundleParser::ParseHapProfile(profileDir, bundleRes);
    if (bundleInfo != nullptr) {
        bundleInfo->isSystemApp = isSystemApp;
        bundleInfo->appId = Utils::Strdup(appId);
        if (bundleInfo->appId == nullptr) {
            BundleInfoUtils::FreeBundleInfo(bundleInfo);
            AdapterFree(bundleRes->abilityRes);
            AdapterFree(bundleRes);
            return false;
        }
        bundleMap_->Add(bundleInfo);
        if (bundleRes->abilityRes == nullptr ||
            (bundleRes->abilityRes->labelId == 0 && bundleRes->abilityRes->iconId == 0)) {
            AdapterFree(bundleRes->abilityRes);
            AdapterFree(bundleRes);
        } else {
            bundleRes->bundleName = bundleInfo->bundleName;
            AddBundleResList(bundleRes);
        }
        SendBundleListChangedToLauncher(BUNDLE_UPDATE, bundleInfo->bundleName);
        return true;
    }
    AdapterFree(bundleRes->abilityRes);
    AdapterFree(bundleRes);
    APP_ERRCODE_EXTRA(EXCE_ACE_APP_SCAN, EXCE_ACE_APP_SCAN_PARSE_PROFILE_FALIED);
    HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] reload bundle info fail!, isSystemApp is %d", isSystemApp);
    BundleUtil::RemoveDir(profileDir);
    return false;
}

void GtManagerService::AddBundleResList(const BundleRes *bundleRes)
{
    if (bundleRes == nullptr || bundleRes->bundleName == nullptr || bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res != nullptr && res->bundleName != nullptr && strcmp(res->bundleName, bundleRes->bundleName) == 0) {
            return;
        }
    }
    bundleResList_->PushFront(const_cast<BundleRes *>(bundleRes));
}

void GtManagerService::RemoveBundleResList(const char *bundleName)
{
    if (bundleName == nullptr || bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res->bundleName != nullptr && strcmp(bundleName, res->bundleName) == 0) {
            AdapterFree(res->abilityRes);
            AdapterFree(res);
            bundleResList_->Remove(node);
            return;
        }
    }
}

void GtManagerService::UpdateBundleInfoList()
{
    if (bundleResList_ == nullptr) {
        return;
    }

    for (auto node = bundleResList_->Begin(); node != bundleResList_->End(); node = node->next_) {
        BundleRes *res = node->value_;
        if (res->bundleName == nullptr || res->abilityRes == nullptr) {
            continue;
        }

        BundleInfo *bundleInfo = bundleMap_->Get(res->bundleName);
        if (bundleInfo == nullptr) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get no bundleInfo when change bundle res!");
            continue;
        }

        int32_t len = strlen(INSTALL_PATH) + 1 + strlen(res->bundleName);
        char *path = reinterpret_cast<char *>(UI_Malloc(len + 1));
        if (path == nullptr) {
            continue;
        }

        if (sprintf_s(path, len + 1, "%s/%s", INSTALL_PATH, res->bundleName) < 0) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] change bundle res failed! because sprintf_s fail");
            UI_Free(path);
            continue;
        }

        uint8_t errorCode = GtBundleParser::ConvertResInfoToBundleInfo(path, res->abilityRes->labelId,
            res->abilityRes->iconId, bundleInfo);
        UI_Free(path);
        if (errorCode != ERR_OK) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] change bundle res failed! errorCode is %d", errorCode);
            return;
        }
    }
}

void GtManagerService::TransformJsToBcWhenRestart(const char *codePath, const char *bundleName)
{
    if (codePath == nullptr) {
        return;
    }

    char *bundleJsonPathComp[] = {
        const_cast<char *>(JSON_PATH), const_cast<char *>(bundleName), const_cast<char *>(JSON_SUFFIX)
    };
    char *bundleJsonPath = BundleUtil::Strscat(bundleJsonPathComp, sizeof(bundleJsonPathComp) / sizeof(char *));
    if (bundleJsonPath == nullptr) {
        return;
    }

    cJSON *installRecordJson = BundleUtil::GetJsonStream(bundleJsonPath);
    if (installRecordJson == nullptr) {
        AdapterFree(bundleJsonPath);
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] get installRecord fail when restart!");
        return;
    }

    cJSON *jsEngineVerObj = cJSON_CreateString(jsEngineVer_);
    if (jsEngineVerObj == nullptr) {
        cJSON_Delete(installRecordJson);
        AdapterFree(bundleJsonPath);
        HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] create string object fail when restart!");
        return;
    }

    cJSON *oldJsEngineVerObj = cJSON_GetObjectItem(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION);
    if (cJSON_IsString(oldJsEngineVerObj) && strcmp(oldJsEngineVerObj->valuestring, jsEngineVer_) == 0) {
        cJSON_Delete(jsEngineVerObj);
        cJSON *transformResultObj = cJSON_GetObjectItem(installRecordJson, JSON_SUB_KEY_TRANSFORM_RESULT);
        if (cJSON_IsNumber(transformResultObj) && transformResultObj->valueint == 0) {
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    } else if (oldJsEngineVerObj == nullptr) {
        if (!cJSON_AddItemToObject(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION, jsEngineVerObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] add js engine version record fail when restart!");
            cJSON_Delete(jsEngineVerObj);
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    } else {
        if (!cJSON_ReplaceItemInObject(installRecordJson, JSON_SUB_KEY_JSENGINE_VERSION, jsEngineVerObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] refresh js engine version fail when restart!");
            cJSON_Delete(jsEngineVerObj);
            cJSON_Delete(installRecordJson);
            AdapterFree(bundleJsonPath);
            return;
        }
    }

    TransformJsToBc(codePath, bundleJsonPath, installRecordJson);
    cJSON_Delete(installRecordJson);
    AdapterFree(bundleJsonPath);
}

void GtManagerService::TransformJsToBc(const char *codePath, const char *bundleJsonPath, cJSON *installRecordObj)
{
    if (codePath == nullptr || installRecordObj == nullptr || bundleJsonPath == nullptr) {
        return;
    }

    char *jsPathComp[] = {const_cast<char *>(codePath), const_cast<char *>(ASSET_JS_PATH)};
    char *jsPath = BundleUtil::Strscat(jsPathComp, sizeof(jsPathComp) / sizeof(char *));
    if (jsPath == nullptr) {
        return;
    }

    AdapterFree(jsPath);

    cJSON *resultObj = cJSON_CreateNumber(0);
    if (resultObj == nullptr) {
        return;
    }
    cJSON *oldResultObj = cJSON_GetObjectItem(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT);
    if (oldResultObj == nullptr) {
        if (!cJSON_AddItemToObject(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT, resultObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] add transform result record fail when restart!");
            cJSON_Delete(resultObj);
            return;
        }
    } else {
        if (!cJSON_ReplaceItemInObject(installRecordObj, JSON_SUB_KEY_TRANSFORM_RESULT, resultObj)) {
            HILOG_ERROR(HILOG_MODULE_AAFWK, "[BMS] refresh transform result record fail when restart!");
            cJSON_Delete(resultObj);
            return;
        }
    }
    (void) BundleUtil::StoreJsonContentToFile(bundleJsonPath, installRecordObj);
}

bool GtManagerService::CheckThirdSystemBundleHasUninstalled(const char *bundleName, const cJSON *object)
{
    if (object == nullptr || bundleName == nullptr) {
        return false;
    }

    cJSON *array = cJSON_GetObjectItem(object, JSON_MAIN_KEY);
    if (!cJSON_IsArray(array)) {
        return false;
    }

    int32_t size = cJSON_GetArraySize(array);
    for (int32_t i = 0; i < size; i++) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsString(item)) {
            return false;
        }
        if ((item->valuestring != nullptr) && strcmp(bundleName, item->valuestring) == 0) {
            return true;
        }
    }
    return false;
}

BundleInfo *GtManagerService::QueryBundleInfo(const char *bundleName)
{
    if (bundleName == nullptr || bundleMap_ == nullptr) {
        return nullptr;
    }
    return bundleMap_->Get(bundleName);
}

void GtManagerService::RemoveBundleInfo(const char *bundleName)
{
    if (bundleName == nullptr || bundleMap_ == nullptr) {
        return;
    }
    bundleMap_->Erase(bundleName);
}

void GtManagerService::AddBundleInfo(BundleInfo *info)
{
    if (info == nullptr || info->bundleName == nullptr || bundleMap_ == nullptr) {
        return;
    }
    bundleMap_->Add(info);
}

bool GtManagerService::UpdateBundleInfo(BundleInfo *info)
{
    if (info == nullptr) {
        return false;
    }
    return bundleMap_->Update(info);
}

char *GtManagerService::GetAppIdByBundleName(const char *bundleName)
{
    if (bundleName == nullptr) {
        return nullptr;
    }
    BundleInfo bundleInfo;
    if (GetBundleInfo(bundleName, 0, bundleInfo) != ERR_OK) {
        return nullptr;
    }
    return BundleUtil::GetValueFromBundleJson(bundleName, JSON_SUB_KEY_APPID);
}

uint32_t GtManagerService::GetVersionCodeByBundlePath(const char *path, unsigned char *versionCode, uint8_t length)
{
    if (!BundleUtil::IsFile(path) || versionCode == nullptr || length == 0) {
        return 0;
    }

    char *bundleName = nullptr;
    int32_t code = -1;
    if (!GtBundleParser::ParseBundleAttr(path, &bundleName, code)) {
        AdapterFree(bundleName);
        return 0;
    }
    AdapterFree(bundleName);
    // convert number to char
    uint8_t versionCodeLen = 0;
    while (code != 0) {
        if (versionCodeLen == length - 1) {
            return 0;
        }
        versionCode[versionCodeLen++] = code % MAX_SINGLE_DIGIT_VALUE + ZERO_ASCII_NUM;
        code /= MAX_SINGLE_DIGIT_VALUE;
    }
    versionCode[versionCodeLen] = '\0';
    // pre-post conversion
    for (uint8_t j = 0; j < versionCodeLen / PAIR_VALUE; j++) {
        versionCode[j] = versionCode[j] + versionCode[versionCodeLen - 1 - j];
        versionCode[versionCodeLen - 1 - j] = versionCode[j] - versionCode[versionCodeLen - 1 - j];
        versionCode[j] = versionCode[j] - versionCode[versionCodeLen - 1 - j];
    }
    return versionCodeLen;
}

uint32_t GtManagerService::GetNumOfThirdBundles()
{
    return installedThirdBundleNum_;
}

void GtManagerService::AddNumOfThirdBundles()
{
    installedThirdBundleNum_++;
}

void GtManagerService::ReduceNumOfThirdBundles()
{
    installedThirdBundleNum_--;
}

void GtManagerService::SendBundleListChangedToLauncher(BundleState state, const char *bundleName)
{
    if (bundleName == nullptr) {
        return;
    }
    char *innerBundleName = reinterpret_cast<char *>(SvrMalloc(strlen(bundleName) + 1));
    if (innerBundleName == nullptr) {
        return;
    }
    if (strncpy_s(innerBundleName, strlen(bundleName) + 1, bundleName, strlen(bundleName)) != EOK) {
        SvrFree(innerBundleName);
        return;
    }
}

int32_t GtManagerService::ReportSystemBundleInstallProcess(uint8_t process, const char *bundleName,
    uint8_t installState)
{
    if (systemBundleInstallMsg_ == nullptr) {
        return -1;
    }

    BundleInstallMsg *bundleInstallMsg = reinterpret_cast<BundleInstallMsg *>(SvrMalloc(sizeof(BundleInstallMsg)));
    if (bundleInstallMsg == nullptr) {
        return -1;
    }
    bundleInstallMsg->installState = static_cast<InstallState>(installState);
    bundleInstallMsg->installProcess = process;
    bundleInstallMsg->label = systemBundleInstallMsg_->label;
    bundleInstallMsg->bundleName = systemBundleInstallMsg_->bundleName;
    bundleInstallMsg->smallIconPath = systemBundleInstallMsg_->smallIconPath;
    bundleInstallMsg->bigIconPath = systemBundleInstallMsg_->bigIconPath;
    return 0;
}

void GtManagerService::ReportInstallProcess(const char *bundleName, uint8_t bundleStyle, uint8_t process)
{
    if (bundleName == nullptr) {
        return;
    }
    if (bundleStyle == THIRD_APP_FLAG) {
        (void) BundleCallbackFunc(process, bundleName, 0);
    } else {
        (void) ReportSystemBundleInstallProcess(process, bundleName, BUNDLE_INSTALL_DOING);
    }
}

int8_t GtManagerService::GetInstallState(const char *bundleName) const
{
    BundleInfo *installedInfo = bundleMap_->Get(bundleName);
    if (installedInfo != nullptr) {
        return BMS_INSTALLATION_COMPLETED;
    }
    return installer_->getInstallationProgress(bundleName);
}
} // namespace OHOS
extern "C" {
static char *g_currentBundle = nullptr;
const int32_t BUNDLENAME_MUTEX_TIMEOUT = 2000;
static osMutexId_t g_currentBundleMutex;

char *GetAppIdByBundleName(const char *bundleName)
{
    return OHOS::GtManagerService::GetInstance().GetAppIdByBundleName(bundleName);
}

uint32_t GetVersionCodeByBundlePath(const char *path, unsigned char *versionCode, uint8_t length)
{
    return OHOS::GtManagerService::GetInstance().GetVersionCodeByBundlePath(path, versionCode, length);
}

void UpdateBundleInfoList()
{
    OHOS::GtManagerService::GetInstance().UpdateBundleInfoList();
}

void SetCurrentBundle(const char *name)
{
    MutexAcquire(&g_currentBundleMutex, BUNDLENAME_MUTEX_TIMEOUT);
    AdapterFree(g_currentBundle);
    if (name == nullptr) {
        MutexRelease(&g_currentBundleMutex);
        return;
    }

    int len = strlen(name);
    g_currentBundle = (char *) AdapterMalloc(len + 1);
    if (g_currentBundle == nullptr || strncpy_s(g_currentBundle, len + 1, name, len) < 0) {
        AdapterFree(g_currentBundle);
    }
    MutexRelease(&g_currentBundleMutex);
}

const char *GetCurrentBundle()
{
    MutexAcquire(&g_currentBundleMutex, BUNDLENAME_MUTEX_TIMEOUT);
    if (g_currentBundle == nullptr) {
        MutexRelease(&g_currentBundleMutex);
        return nullptr;
    }

    int len = strlen(g_currentBundle);
    char *bundleName = (char *) AdapterMalloc(len + 1);
    if (bundleName == nullptr || strncpy_s(bundleName, len + 1, g_currentBundle, len) < 0) {
        AdapterFree(bundleName);
        MutexRelease(&g_currentBundleMutex);
        return nullptr;
    }

    MutexRelease(&g_currentBundleMutex);
    return bundleName;
}

int ReportSystemBundleInstallProcess(unsigned char process, const char *bundleName, unsigned char installState)
{
    return 0;
}

int BundleCallbackFunc(unsigned char operationResult, const char *bundleName, unsigned char errorcode)
{
    return 0;
}

void ScanPackages()
{
    OHOS::GtManagerService::GetInstance().ScanPackages();
}

AppInfoList *APP_InitAllAppInfo()
{
    return nullptr;
}

void APP_FreeAllAppInfo(const AppInfoList *list)
{
    return;
}
}