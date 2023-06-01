/*
 * Copyright (C) 2023 Huawei Device Co., Ltd.
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

#include "gst_trace.h"
#include "hitrace_meter.h"

void GstStartTrace(char *name)
{
    StartTrace(HITRACE_TAG_ZMEDIA, std::string(name));
}

void GstStartTraceExt(char *name, char *ext_name)
{
    std::string trace_name = name;
    trace_name = trace_name + "_" + ext_name;
    StartTrace(HITRACE_TAG_ZMEDIA, trace_name);
}

void GstFinishTrace()
{
    FinishTrace(HITRACE_TAG_ZMEDIA);
}

void GstCounterTrace(char *name, int count)
{
    CountTrace(HITRACE_TAG_ZMEDIA, std::string(name), count);
}