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

#include "aafwk_product_adapter.h"

namespace OHOS {
/**
 * Used for holding all the related dfx interfaces assigned from specific implementation.
 */
struct AafwkDFXWrapper {
    AafwkDFXWrapper()
        : eventTag_(0),
          eventSubTag_(0),
          errCodeTag_(0),
          errCodeSubTag_(0),
          eventPrintHandler_(nullptr),
          errCodePrintHandler_(nullptr)
    {
    }
    uint8_t eventTag_;
    uint8_t eventSubTag_;
    uint8_t errCodeTag_;
    uint8_t errCodeSubTag_;
    EventPrintHandler eventPrintHandler_;
    ErrCodePrintHandler errCodePrintHandler_;
};

static AafwkDFXWrapper sDfxWrapper;

void AafwkProductAdapter::InitAafwkTags(uint8_t *aafwkTags, uint8_t tagCount)
{
    const uint8_t minCount = 4;
    if (aafwkTags == nullptr || tagCount < minCount) {
        return;
    }
    uint8_t index = 0;
    sDfxWrapper.eventTag_ = aafwkTags[index++];
    sDfxWrapper.eventSubTag_ = aafwkTags[index++];
    sDfxWrapper.errCodeTag_ = aafwkTags[index++];
    sDfxWrapper.errCodeSubTag_ = aafwkTags[index++];
}

void AafwkProductAdapter::InitTraceHandlers(EventPrintHandler eventHandler, ErrCodePrintHandler errCodeHandler)
{
    sDfxWrapper.eventPrintHandler_ = eventHandler;
    sDfxWrapper.errCodePrintHandler_ = errCodeHandler;
}


void AafwkProductAdapter::PrintEventTrace(uint8_t info2, uint8_t info3, uint8_t info4)
{
    if (sDfxWrapper.eventPrintHandler_ == nullptr || sDfxWrapper.eventTag_ == 0 || sDfxWrapper.eventSubTag_ == 0) {
        return;
    }

    uint8_t subTag = (info2 == 0) ? sDfxWrapper.eventSubTag_ : info2;
    sDfxWrapper.eventPrintHandler_(sDfxWrapper.eventTag_, subTag, info3, info4);
}

void AafwkProductAdapter::PrintErrCode(uint8_t info2, uint16_t rfu)
{
    if (sDfxWrapper.errCodePrintHandler_ == nullptr || sDfxWrapper.errCodeTag_ == 0 ||
        sDfxWrapper.errCodeSubTag_ == 0) {
        return;
    }
    sDfxWrapper.errCodePrintHandler_(sDfxWrapper.errCodeTag_, sDfxWrapper.errCodeSubTag_, info2, rfu);
}
} // namespace OHOS