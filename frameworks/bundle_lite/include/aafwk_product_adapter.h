/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
#ifndef AAFWK_PRODUCT_ADAPTER_H #define AAFWK_PRODUCT_ADAPTER_H
#define AAFWK_PRODUCT_ADAPTER_H #define AAFWK_PRODUCT_ADAPTER_H

#include <cstdint>

namespace OHOS {
/**
 * This handler type is so specific for product, should be designed from OS dfx level.
 */
typedef void (*EventPrintHandler)(uint8_t info1, uint8_t info2, uint8_t info3, uint8_t info4);
typedef void (*ErrCodePrintHandler)(uint8_t info1, uint8_t info2, uint8_t info3, uint16_t info4);

/**
 * The wrapper class for some product feature implementation, since some interfaces are provided by specific product.
 */
class AafwkProductAdapter final {
public:
    AafwkProductAdapter(const AafwkProductAdapter &) = delete;
    AafwkProductAdapter &operator=(const AafwkProductAdapter &) = delete;
    AafwkProductAdapter(AafwkProductAdapter &&) = delete;
    AafwkProductAdapter &operator=(AafwkProductAdapter &&) = delete;
    AafwkProductAdapter() = delete;
    ~AafwkProductAdapter() = delete;

    // initialization functions
    static void InitTraceHandlers(EventPrintHandler eventHandler, ErrCodePrintHandler errCodeHandler);
    static void InitAafwkTags(uint8_t *aceTags, uint8_t tagCount);

    // wrapper functions, for aafwk internal calling
    static void PrintEventTrace(uint8_t info2, uint8_t info3, uint8_t info4);
    static void PrintErrCode(uint8_t info2, uint16_t rfu);
};
} // namespace OHOS
#endif