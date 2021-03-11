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

#ifndef OHOS_GT_BUNDLE_MANAGER_SERVICE_H
#define OHOS_GT_BUNDLE_MANAGER_SERVICE_H

#include "adapter.h"
#include "bundle_app_info.h"
#include "bundle_info.h"
#include "bundle_map.h"
#include "cJSON.h"
#include "gt_bundle_installer.h"
#include "stdint.h"
#include "want.h"
#include "install_param.h"
#include "bundle_manager.h"
#include "los_list.h"
#include "ohos_types.h"

namespace OHOS {
struct ToBeInstalledApp {
    bool isSystemApp;
    bool isUpdated;
    char *path;
    char *installedPath;
};

class GtManagerService {
public:
    static GtManagerService &GetInstance()
    {
        static GtManagerService instance;
        return instance;
    }
    bool Install(const char *hapPath, const InstallParam *installParam, InstallerCallback installerCallback);
    bool Uninstall(const char *bundleName, const InstallParam *installParam, InstallerCallback installerCallback);
    uint8_t QueryAbilityInfo(const Want *want, AbilityInfo *abilityInfo);
    uint8_t GetBundleInfo(const char *bundleName, int32_t flags, BundleInfo& bundleInfo);
    uint8_t GetBundleInfos(const int flags, BundleInfo **bundleInfos, int32_t *len);
    void ScanPackages();
    void SendBundleListChangedToLauncher(BundleState state, const char *bundleName);
    BundleInfo *QueryBundleInfo(const char *bundleName);
    void RemoveBundleInfo(const char *bundleName);
    void AddBundleInfo(BundleInfo *info);
    bool UpdateBundleInfo(BundleInfo *info);
    char *GetAppIdByBundleName(const char *bundleName);
    uint32_t GetVersionCodeByBundlePath(const char *path, unsigned char *versionCode, uint8_t length);
    uint32_t GetNumOfThirdBundles();
    void RemoveBundleResList(const char *bundleName);
    void AddBundleResList(const BundleRes *bundleRes);
    void UpdateBundleInfoList();
    int32_t ReportSystemBundleInstallProcess(uint8_t process, const char *bundleName, uint8_t installState);
    void ReportInstallProcess(const char *bundleName, uint8_t bundleStyle, uint8_t process);
    void AddNumOfThirdBundles();
    void ReduceNumOfThirdBundles();
    int8_t GetInstallState(const char *bundleName) const;

private:
    GtManagerService();
    ~GtManagerService();
    void ScanSystemApp(const cJSON *uninstallRecord, List<ToBeInstalledApp *> *systemPathList);
    void ScanThirdApp(const char *appDir, const List<ToBeInstalledApp *> *systemPathList);
    void InstallAllSystemBundle();
    void InstallSystemBundle(const char *systemAppPath);
    bool ReloadBundleInfo(const char *profileDir, const char *appId, bool isSystemApp);
    void ReloadEntireBundleInfo(const char *appPath, const char *bundleName, List<ToBeInstalledApp *> *systemPathList,
        int32_t versionCode, uint8_t scanFlag);
    bool CheckSystemBundleIsValid(const char *appPath, char **bundleName, int32_t &versionCode);
    bool CheckThirdSystemBundleHasUninstalled(const char *bundleName, const cJSON *object);
    void AddSystemAppPathList(const char *installedPath, const char *path, List<ToBeInstalledApp *> *systemPathList,
        bool isSystemApp, bool isUpdated);
    void RemoveSystemAppPathList(List<ToBeInstalledApp *> *systemPathList);
    void ClearSystemBundleInstallMsg();
    void TransformJsToBcWhenRestart(const char *codePath, const char *bundleName);
    void TransformJsToBc(const char *codePath, const char *bundleJsonPath, cJSON *installRecordObj);
    bool IsSystemBundleInstalledPath(const char *appPath, const List<ToBeInstalledApp *> *systemPathList);

    GtBundleInstaller *installer_;
    BundleMap *bundleMap_;
    List<BundleRes *> *bundleResList_;
    BundleInstallMsg *systemBundleInstallMsg_;
    char *jsEngineVer_;
    uint32_t installedThirdBundleNum_;
};
}

extern "C" {
#define JS_FILE_PATH_FLASH_LIGHT "system/ace/sys/flashlight.bin"
#define JS_FILE_PATH_HEALTH_ECG "system/ace/sys/health_ecg.bin"
#define MAX_APP_FILE_PATH_LEN 100
typedef struct {
    LOS_DL_LIST appDoubleList;
    char filePath[MAX_APP_FILE_PATH_LEN];
} AppInfoList;

AppInfoList *APP_InitAllAppInfo(void);
void APP_FreeAllAppInfo(const AppInfoList *list);
void EnableServiceWdg(void);
void DisableServiceWdg(void);
}

#endif // OHOS_GT_BUNDLE_MANAGER_SERVICE_H