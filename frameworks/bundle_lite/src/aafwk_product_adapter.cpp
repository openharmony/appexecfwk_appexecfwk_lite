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
#include "event.h"
#include "errorcode.h"

namespace OHOS {
void AafwkProductAdapter::PrintEventTrace(uint8_t info1)
{
    EVENT(EC_PUBLIC, 0x3f, 0, info1, 0);
}

void AafwkProductAdapter::PrintErrCode(uint8_t info2, uint16_t rfu)
{
    ERROR_CODE(EC_PUBLIC, 0x3f, 0, info2, rfu);
}
} // namespace OHOS