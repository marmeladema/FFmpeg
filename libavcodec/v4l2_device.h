/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef AVCODEC_V4L2_DEVICE_H
#define AVCODEC_V4L2_DEVICE_H

#include <stdbool.h>

#include <linux/media.h>
#include <linux/videodev2.h>

#include "libavcodec/avcodec.h"

typedef struct V4L2DeviceVideo {
    char devname[PATH_MAX];
    int fd;
    struct v4l2_capability capability;
} V4L2DeviceVideo;

typedef struct V4L2DeviceMedia {
    char devname[PATH_MAX];
    int fd;
    struct media_device_info info;
} V4L2DeviceMedia;

/*
 * Probes for a valid video device.
 *
 * @return 0 in case of success, a negative value representing the error otherwise.
 */
int ff_v4l2_device_probe_video(AVCodecContext *avctx,
                               struct V4L2DeviceVideo *device,
                               void *opaque,
                               bool (*checkdev)(struct V4L2DeviceVideo *device, void *opaque));

/*
 * Retrieves the media device associated with a video device.
 *
 * @return 0 in case of success, a negative value representing the error otherwise.
 */

int ff_v4l2_device_retrieve_media(AVCodecContext *avctx,
                                  const struct V4L2DeviceVideo *video_device,
                                  struct V4L2DeviceMedia *media_device);

#endif /* AVCODEC_V4L2_DEVICE_H */
