/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2006 Wim Taymans <wim@fluendo.com>
 *                    2006 David A. Schleef <ds@schleef.org>
 *
 * gstmultifilesink.c:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

/* ohos.ext.func.0002 */
#ifdef OHOS_EXT_FUNC
#include "gstsplitmuxsink.h"
#else
#include "gstmultifilesink.h"
#include "gstmultifilesrc.h"
#include "gstsplitfilesrc.h"
#include "gstsplitmuxsink.h"
#include "gstsplitmuxsrc.h"
#include "gstimagesequencesrc.h"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

/* ohos.ext.func.0002: The media service need splitmuxsink element to support recording file split feature.
 * But the other elements have compilation dependence. So, only enable the compilation of necessary elements.
 */
#ifdef OHOS_EXT_FUNC
  ret |= GST_ELEMENT_REGISTER (splitmuxsink, plugin);
#else
  ret |= GST_ELEMENT_REGISTER (multifilesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (multifilesink, plugin);
  ret |= GST_ELEMENT_REGISTER (splitfilesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (imagesequencesrc, plugin);
  ret |= GST_ELEMENT_REGISTER (splitmuxsink, plugin);
  ret |= GST_ELEMENT_REGISTER (splitmuxsrc, plugin);
#endif

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    multifile,
    "Reads/Writes buffers from/to sequentially named files",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
