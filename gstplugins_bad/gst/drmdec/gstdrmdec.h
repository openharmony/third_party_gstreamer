/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef GST_DRM_DECRYPT_H
#define GST_DRM_DECRYPT_H

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS
#define GST_TYPE_DRM_DECRYPT   (gst_drm_decrypt_get_type())
#define GST_DRM_DECRYPT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DRM_DECRYPT, GstDrmDecrypt))
#define GST_DRM_DECRYPT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DRM_DECRYPT, GstDrmDecryptClass))
#define GST_IS_DRM_DECRYPT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DRM_DECRYPT))
#define GST_IS_DRM_DECRYPT_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DRM_DECRYPT))
typedef struct _GstDrmDecrypt GstDrmDecrypt;
typedef struct _GstDrmDecryptClass GstDrmDecryptClass;


GType gst_drm_decrypt_get_type (void);

G_END_DECLS
#endif // GST_DRM_DECRYPT_H
