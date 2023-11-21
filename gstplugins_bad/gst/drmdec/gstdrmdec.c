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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/gstprotection.h>
#include <gst/gstelement.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstbytereader.h>

#include "gstdrmdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_drm_decrypt_category);
#define GST_CAT_DEFAULT gst_drm_decrypt_category

#define KID_LENGTH                   16
#define KEY_LENGTH                   16
#define DRM_IV_SIZE                  16
#define DRM_MAX_SUB_SAMPLE_NUM       64
#define DRM_TS_SUB_SAMPLE_NUM        (2)
#define DRM_TS_FLAG_CRYPT_BYTE_BLOCK (2)
#define DRM_CRYPT_BYTE_BLOCK         (1 + 2) // 2: DRM_TS_FLAG_CRYPT_BYTE_BLOCK
#define DRM_SKIP_BYTE_BLOCK          (9)

typedef struct _DRM_SubSample DRM_SubSample;

enum
{
  SIGNAL_ON_MEDIA_DECRYPT,
  NUM_SIGNALS
};

typedef enum {
  DRM_ALG_CENC_UNENCRYPTED = 0x0,
  DRM_ALG_CENC_AES_CTR = 0x1,
  DRM_ALG_CENC_AES_CBC,
  DRM_ALG_CENC_SM4_CBC = 0x4,
  DRM_ALG_CENC_SM4_CTR,
} DRM_CencAlgorithm;

typedef enum
{
  GST_DRM_CLEARPLAY,
  GST_DRM_CDRM,
  GST_DRM_UNKNOWN = -1
} GstDrmType;

struct _GstDrmDecrypt
{
  GstBaseTransform parent;
  GstDrmType drm_type;
  gboolean is_audio;
};

struct _GstDrmDecryptClass
{
  GstBaseTransformClass parent_class;
};

struct _DRM_SubSample
{
  guint clear_header_len;
  guint pay_load_len;
};

static guint signals[NUM_SIGNALS];

/* prototypes */
static void gst_drm_decrypt_dispose (GObject *object);
static void gst_drm_decrypt_finalize (GObject *object);

static gboolean gst_drm_decrypt_start (GstBaseTransform *trans);
static gboolean gst_drm_decrypt_stop (GstBaseTransform *trans);
static gboolean gst_drm_decrypt_append_if_not_duplicate (GstCaps *dest, GstStructure *new_struct);
static GstCaps *gst_drm_decrypt_transform_caps (GstBaseTransform *base,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter);

static GstFlowReturn gst_drm_decrypt_transform_ip (GstBaseTransform *trans,
    GstBuffer *buf);

static gboolean gst_drm_decrypt_sink_event_handler (GstBaseTransform *trans,
    GstEvent *event);

#define CLEARPLAY_PROTECTION_ID "e2719d58-a985-b3c9-781a-b030af78d30e"
#define CDRM_PROTECTION_ID "3d5e6d35-9b9a-41e8-b843-dd3c6e72c42c"

/* pad templates */
static GstStaticPadTemplate gst_drm_decrypt_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    (
     "application/x-cenc, protection-system=(string)" CLEARPLAY_PROTECTION_ID "; "
     "application/x-cenc, protection-system=(string)" CDRM_PROTECTION_ID)
    );

static GstStaticPadTemplate gst_drm_decrypt_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );


static const gchar* gst_drm_decrypt_protection_ids[] = {
  CLEARPLAY_PROTECTION_ID,
  CDRM_PROTECTION_ID,
  NULL
};

/* class initialization */
#define gst_drm_decrypt_parent_class parent_class
G_DEFINE_TYPE (GstDrmDecrypt, gst_drm_decrypt, GST_TYPE_BASE_TRANSFORM);

static void
gst_drm_decrypt_class_init (GstDrmDecryptClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_drm_decrypt_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_drm_decrypt_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Decrypt content encrypted using ISOBMFF Common Encryption",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts media that has been encrypted using ISOBMFF Common Encryption.",
      "drm");

  GST_DEBUG_CATEGORY_INIT (gst_drm_decrypt_category,
      "drmdec", 0, "DRM decryptor");

  gobject_class->dispose = gst_drm_decrypt_dispose;
  gobject_class->finalize = gst_drm_decrypt_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_drm_decrypt_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_drm_decrypt_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_drm_decrypt_transform_ip);
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_drm_decrypt_transform_caps);
  base_transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_drm_decrypt_sink_event_handler);
  base_transform_class->transform_ip_on_passthrough = FALSE;

  signals[SIGNAL_ON_MEDIA_DECRYPT] =
      g_signal_new ("media-decrypt", G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
      0, NULL, NULL, NULL,
      G_TYPE_INT, 13, G_TYPE_INT64, G_TYPE_INT64, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT, // 13:parameter nums
      G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
}

static void
gst_drm_decrypt_init (GstDrmDecrypt *self)
{
  GstBaseTransform *base = GST_BASE_TRANSFORM (self);
  
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_TRANSFORM_SINK_PAD (self));

  gst_base_transform_set_in_place (base, TRUE);
  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (self), FALSE);
  self->drm_type = GST_DRM_UNKNOWN;
  self->is_audio = FALSE;
}

void
gst_drm_decrypt_dispose (GObject *object)
{
  GstDrmDecrypt *self = GST_DRM_DECRYPT (object);

  self->is_audio = FALSE;
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_drm_decrypt_finalize (GObject *object)
{
  /* clean up object here */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_drm_decrypt_start (GstBaseTransform *trans)
{
  GstDrmDecrypt *self = GST_DRM_DECRYPT (trans);
  GST_DEBUG_OBJECT (self, "start");

  return TRUE;
}

static gboolean
gst_drm_decrypt_stop (GstBaseTransform *trans)
{
  GstDrmDecrypt *self = GST_DRM_DECRYPT (trans);
  GST_DEBUG_OBJECT (self, "stop");

  return TRUE;
}

/*
 * Append new_structure to dest, but only if it does not already exist in res.
 * This function takes ownership of new_structure.
 */
static gboolean
gst_drm_decrypt_append_if_not_duplicate (GstCaps *dest, GstStructure *new_struct)
{
  gboolean duplicate = FALSE;
  gint i;

  for (i = 0; (!duplicate) && (i < gst_caps_get_size (dest)); ++i) {
    GstStructure *s = gst_caps_get_structure (dest, i);
    if (gst_structure_is_equal (s, new_struct)) {
      duplicate=TRUE;
    }
  }
  if (!duplicate) {
    gst_caps_append_structure (dest, new_struct);
  } else {
    gst_structure_free (new_struct);
  }
  return duplicate;
}

/* filter out the audio and video related fields from the up-stream caps,
 * because they are not relevant to the input caps of this element and
 * can cause caps negotiation failures with adaptive bitrate streams
 */
static void
gst_drm_remove_codec_fields (GstStructure *gs)
{
  gint i;
  gint n_fields = gst_structure_n_fields (gs);
  for (i = n_fields - 1; i >= 0; --i) {
    const gchar *name;

    name = gst_structure_nth_field_name (gs, i);
    GST_TRACE ("Check field \"%s\"", name);

    if (g_strcmp0 (name, "height") == 0 ||
        g_strcmp0 (name, "width") == 0 ||
        g_strcmp0 (name, "rate") == 0 ||
        g_strcmp0 (name, "framerate") == 0 ||
        g_strcmp0 (name, "pixel-aspect-ratio") == 0 ||
        g_strcmp0 (name, "level") == 0 ||
        g_strcmp0 (name, "codec_data") == 0 ||
        g_strcmp0 (name, "base-profile") == 0 ||
        g_strcmp0 (name, "profile") == 0) {
      gst_structure_remove_field (gs, name);
    }
  }
}

static gboolean
gst_drm_decrypt_transform_sink_caps (GstBaseTransform *base, GstCaps *res, GstStructure *in, GstStructure *out)
{
  gint n_fields;
  gint i;
  GstDrmDecrypt *self = GST_DRM_DECRYPT (base);

  if (!gst_structure_has_field (in, "original-media-type")) {
    return FALSE;
  }

  out = gst_structure_copy (in);
  n_fields = gst_structure_n_fields (in);

  gst_structure_set_name (out,
      gst_structure_get_string (out, "original-media-type"));

  const gchar *media_type;
  if ((media_type = gst_structure_get_string (in, "original-media-type"))) {
    if (g_str_has_prefix (media_type, "audio")) {
      self->is_audio = TRUE;
    }
    if (g_str_has_prefix (media_type, "video")) {
      self->is_audio = FALSE;
    }
  }

  /* filter out the DRM related fields from the down-stream caps */
  for (i = n_fields - 1; i >= 0; --i) {
    const gchar *field_name;

    field_name = gst_structure_nth_field_name (in, i);
    if (g_str_has_prefix(field_name, "protection-system") ||
        g_str_has_prefix(field_name, "original-media-type")) {
      gst_structure_remove_field (out, field_name);
    }
  }
  gst_drm_decrypt_append_if_not_duplicate (res, out);
  return TRUE;
}

static gboolean
gst_drm_decrypt_transform_src_caps (GstBaseTransform *base, GstCaps *res, GstStructure *in, GstStructure *out)
{
  GstStructure *tmp = NULL;
  guint i;
  (void)base;
  tmp = gst_structure_copy (in);
  gst_drm_remove_codec_fields (tmp);
  for (i = 0; gst_drm_decrypt_protection_ids[i]; ++i) {
    /* filter out the audio/video related fields from the down-stream
     * caps, because they are not relevant to the input caps of this
     * element and they can cause caps negotiation failures with
     * adaptive bitrate streams
     */
    out = gst_structure_copy (tmp);
    gst_structure_set (out,
        "protection-system", G_TYPE_STRING, gst_drm_decrypt_protection_ids[i],
        "original-media-type", G_TYPE_STRING, gst_structure_get_name (in), NULL);
    gst_structure_set_name (out, "application/x-cenc");
    gst_drm_decrypt_append_if_not_duplicate (res, out);
  }
  gst_structure_free (tmp);
  return TRUE;
}

static GstCaps *
gst_drm_decrypt_transform_caps (GstBaseTransform *base,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  GstCaps *res = NULL;
  gint i;

  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);
  GST_DEBUG_OBJECT (base, "direction:%s caps:%" GST_PTR_FORMAT " filter:"
      " %" GST_PTR_FORMAT, (direction == GST_PAD_SRC) ? "Src" : "Sink", caps, filter);

  if ((direction == GST_PAD_SRC) && (gst_caps_is_any (caps))) {
    res = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));
    goto beach;
  }

  res = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *in = gst_caps_get_structure (caps, i);
    GstStructure *out = NULL;
    if (direction == GST_PAD_SINK) {
      if (gst_drm_decrypt_transform_sink_caps (base, res, in, out) == FALSE) {
        continue;
      }
    } else {  /* GST_PAD_SRC */
      (void)gst_drm_decrypt_transform_src_caps (base, res, in, out);
    }
  }
  if ((direction == GST_PAD_SINK) && (gst_caps_get_size (res) == 0)) {
    gst_caps_unref (res);
    res = gst_caps_new_any ();
  }
beach:
  if (filter) {
    GstCaps *intersection;
    GST_DEBUG_OBJECT (base, "using caps %" GST_PTR_FORMAT, filter);
    intersection =
      gst_caps_intersect_full (res, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = intersection;
  }
  GST_DEBUG_OBJECT (base, "return %" GST_PTR_FORMAT, res);
  return res;
}

static void
gst_drm_get_algo (const gchar *mode, guint *algo)
{
  if ((strcmp (mode, "sm4c") == 0) || (strcmp (mode, "sm4s") == 0)) {
    *algo = (guint)DRM_ALG_CENC_SM4_CBC;
  } else if ((strcmp (mode, "cbc1") == 0) || (strcmp (mode, "cbcs") == 0)) {
    *algo = (guint)DRM_ALG_CENC_AES_CBC;
  } else if ((strcmp (mode, "cenc") == 0) || (strcmp (mode, "cens") == 0)) {
    *algo = (guint)DRM_ALG_CENC_AES_CTR;
  } else if ((strcmp (mode, "sm4t") == 0) || (strcmp (mode, "sm4r") == 0)) {
    *algo = (guint)DRM_ALG_CENC_SM4_CTR;
  }
  return;
}

static void
gst_drm_decrypt_get_block (const GstProtectionMeta *prot_meta, guint *crypt_byte_block,
    guint *skip_byte_block)
{
  if (!gst_structure_get_uint (prot_meta->info, "crypt_byte_block", crypt_byte_block)) {
    GST_ERROR_OBJECT (prot_meta, "failed to get crypt_byte_block");
    return;
  }
  if (!gst_structure_get_uint (prot_meta->info, "skip_byte_block", skip_byte_block)) {
    GST_ERROR_OBJECT (prot_meta, "failed to get skip_byte_block");
    return;
  }
  return;
}

static void
gst_drm_decrypt_get_algo (const GstProtectionMeta *prot_meta, guint *algo)
{
  const gchar *mode = NULL;
  gboolean encrypted;
  mode = gst_structure_get_string (prot_meta->info, "cipher-mode");
  if (mode == NULL) {
    *algo = DRM_ALG_CENC_UNENCRYPTED;
  } else {
    gst_drm_get_algo (mode, algo);
  }
  if (!gst_structure_get_boolean (prot_meta->info, "encrypted", &encrypted)) {
    if (encrypted == FALSE) {
      *algo = DRM_ALG_CENC_UNENCRYPTED;
    }
  }
  return;
}

static GstFlowReturn
gst_drm_decrypt_get_iv (const GstProtectionMeta *prot_meta, guint8 *iv_data, guint *iv_data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const GValue *value = NULL;
  GstBuffer *iv_buf = NULL;
  GstMapInfo iv_map;
  guint iv_size;

  if (!gst_structure_get_uint (prot_meta->info, "iv_size", &iv_size)) {
    GST_ERROR_OBJECT (prot_meta, "failed to get iv_size");
    return GST_FLOW_NOT_SUPPORTED;
  }
  if (iv_size > *iv_data_size) {
    return GST_FLOW_NOT_SUPPORTED;
  }
  value = gst_structure_get_value (prot_meta->info, "iv");
  if (value == NULL) {
    GST_ERROR_OBJECT (prot_meta, "Failed to get iv for sample");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }
  iv_buf = gst_value_get_buffer (value);
  if (!gst_buffer_map (iv_buf, &iv_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (prot_meta, "Failed to map iv");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }

  if (iv_map.size > *iv_data_size) {
    GST_ERROR_OBJECT (prot_meta, "iv size err\n");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }
  *iv_data_size = iv_map.size;
  memset (iv_data, 0, *iv_data_size);
  memcpy (iv_data, iv_map.data, iv_map.size);
beach:
  if (iv_buf) {
    gst_buffer_unmap (iv_buf, &iv_map);
  }
  return ret;
}

static GstFlowReturn
gst_drm_decrypt_get_keyid (const GstProtectionMeta *prot_meta, guint8 *keyid_data, guint *keyid_data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const GValue *value = NULL;
  GstBuffer *keyid_buf = NULL;
  GstMapInfo keyid_map;

  value = gst_structure_get_value (prot_meta->info, "kid");
  if (value == NULL) {
    GST_ERROR_OBJECT (prot_meta, "Failed to get kid for sample");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }
  keyid_buf = gst_value_get_buffer (value);
  if (!gst_buffer_map (keyid_buf, &keyid_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (prot_meta, "Failed to map key id");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }

  if (keyid_map.size > *keyid_data_size) {
    GST_ERROR_OBJECT (prot_meta, "iv size err\n");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }
  *keyid_data_size = keyid_map.size;
  memset (keyid_data, 0, *keyid_data_size);
  memcpy (keyid_data, keyid_map.data, keyid_map.size);
beach:
  if (keyid_buf) {
      gst_buffer_unmap (keyid_buf, &keyid_map);
  }
  return ret;
}

static GstFlowReturn
gst_drm_decrypt_get_subsample_count (const GstProtectionMeta *prot_meta, guint *subsample_count)
{
  if (!gst_structure_get_uint (prot_meta->info, "subsample_count", subsample_count)) {
    GST_ERROR_OBJECT (prot_meta, "failed to get subsample_count");
    return GST_FLOW_NOT_SUPPORTED;
  }
  if (*subsample_count > DRM_MAX_SUB_SAMPLE_NUM) {
    GST_ERROR_OBJECT (prot_meta, "subsample_count err");
    return GST_FLOW_NOT_SUPPORTED;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_drm_decrypt_get_subsample (const GstProtectionMeta *prot_meta, DRM_SubSample *subsample_info,
    guint subsample_count)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const GValue *value = NULL;
  GstBuffer *subsamples_buf = NULL;
  GstMapInfo subsamples_map;
  GstByteReader *reader = NULL;

  if (subsample_count) {
    value = gst_structure_get_value (prot_meta->info, "subsamples");
    if (value == NULL) {
      GST_ERROR_OBJECT (prot_meta, "Failed to get subsamples");
      return GST_FLOW_NOT_SUPPORTED;
    }
    subsamples_buf = gst_value_get_buffer (value);
    if (!gst_buffer_map (subsamples_buf, &subsamples_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (prot_meta, "Failed to map subsample buffer");
      ret = GST_FLOW_NOT_SUPPORTED;
      goto beach;
    }
    reader = gst_byte_reader_new (subsamples_map.data, subsamples_map.size);
    if (reader == NULL) {
      GST_ERROR_OBJECT (prot_meta, "Failed to new subsample reader");
      ret = GST_FLOW_NOT_SUPPORTED;
      goto beach;
    }
  }
  for (guint i = 0; i < subsample_count; i++) {
    guint16 clear_header_len = 0;
    guint32 pay_load_len = 0;
    if (!gst_byte_reader_get_uint16_be (reader, &clear_header_len)
        || !gst_byte_reader_get_uint32_be (reader, &pay_load_len)) {
        ret = GST_FLOW_NOT_SUPPORTED;
        goto beach;
    }
    subsample_info[i].clear_header_len = (guint)clear_header_len;
    subsample_info[i].pay_load_len = (guint)pay_load_len;
  }
beach:
  if (reader) {
      gst_byte_reader_free (reader);
  }
  if (subsamples_buf) {
      gst_buffer_unmap (subsamples_buf, &subsamples_map);
  }
  return ret;
}

static GstFlowReturn
gst_drm_decrypt_set_ts_subsample (GstMapInfo *map, DRM_SubSample *subsample_info,
    guint *subsample_count, guint skip_byte_block, guint *crypt_byte_block)
{
  if ((*subsample_count == 0) && (*crypt_byte_block + skip_byte_block == DRM_TS_FLAG_CRYPT_BYTE_BLOCK ||
    *crypt_byte_block + skip_byte_block == (DRM_CRYPT_BYTE_BLOCK + DRM_SKIP_BYTE_BLOCK))) {
    *subsample_count = (guint)DRM_TS_SUB_SAMPLE_NUM;
    subsample_info[0].clear_header_len = map->size;
    subsample_info[0].pay_load_len = 0;
    subsample_info[1].clear_header_len = 0;
    subsample_info[1].pay_load_len = 0;
    if (*crypt_byte_block >= DRM_TS_FLAG_CRYPT_BYTE_BLOCK) {
      *crypt_byte_block -= DRM_TS_FLAG_CRYPT_BYTE_BLOCK;
    }
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_drm_decrypt_transform_ip (GstBaseTransform *base, GstBuffer *buf)
{
  GstDrmDecrypt *self = GST_DRM_DECRYPT (base);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  guint algo = 0;
  guint crypt_byte_block = 0;
  guint skip_byte_block = 0;
  guint subsample_count = 0;
  DRM_SubSample subsample_info[DRM_MAX_SUB_SAMPLE_NUM];
  guint iv_size = DRM_IV_SIZE;
  guint8 iv_data[DRM_IV_SIZE];
  guint keyid_size = KID_LENGTH;
  guint8 keyid_data[KID_LENGTH];
  gint res;
  const GstProtectionMeta *prot_meta = (const GstProtectionMeta*) gst_buffer_get_protection_meta (buf);
  if (prot_meta == NULL) {
    goto out;
  }
  if (buf == NULL) {
    GST_ERROR_OBJECT (self, "Failed to get writable buffer\n");
    return GST_FLOW_NOT_SUPPORTED;
  }
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    ret = GST_FLOW_NOT_SUPPORTED;
    goto out;
  }
  gst_drm_decrypt_get_block (prot_meta, &crypt_byte_block, &skip_byte_block);
  gst_drm_decrypt_get_algo (prot_meta, &algo);
  gst_drm_decrypt_get_iv (prot_meta, iv_data, &iv_size);
  gst_drm_decrypt_get_keyid (prot_meta, keyid_data, &keyid_size);
  gst_drm_decrypt_get_subsample_count (prot_meta, &subsample_count);
  gst_drm_decrypt_get_subsample (prot_meta, subsample_info, subsample_count);
  if (iv_size == 0 || algo == 0) {
    /* sample is not encrypted */
    goto beach;
  }
  gst_drm_decrypt_set_ts_subsample (&map, subsample_info, &subsample_count,
        skip_byte_block, &crypt_byte_block);
  if (self->is_audio) {
    g_signal_emit_by_name ((GstElement *) base, "media-decrypt", map.data, map.data, map.size,
        keyid_data, keyid_size, iv_data, iv_size, subsample_count, subsample_info, algo, 0,
        crypt_byte_block, skip_byte_block, &res);
  }
beach:
  gst_buffer_unmap (buf, &map);
out:
  return ret;
}

static gboolean
gst_drm_decrypt_sink_event_handler (GstBaseTransform *trans, GstEvent *event)
{
  gboolean res = TRUE;
  const gchar *system_id;
  GstBuffer *pssi = NULL;
  const gchar *loc;
  GstDrmDecrypt *self = GST_DRM_DECRYPT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_PROTECTION:
        GST_DEBUG_OBJECT (self, "received protection event");
        gst_event_parse_protection (event, &system_id, &pssi, &loc);
        GST_DEBUG_OBJECT (self, "system_id: %s  loc: %s", system_id, loc);
        if (g_str_has_prefix (loc, "isobmff/") && g_ascii_strcasecmp(system_id, CDRM_PROTECTION_ID) == 0) {
          GST_DEBUG_OBJECT (self, "event carries pssh data from tsdemux or qtdemux");
          self->drm_type = GST_DRM_CDRM;
        }
        gst_event_unref (event);
      break;
    default:
      res = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
      break;
  }
  return res;
}
