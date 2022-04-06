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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "libavutil/error.h"

#include "v4l2_device.h"

static int v4l2_device_open_video(const char *devname, struct v4l2_capability *cap)
{
    int fd, ret;

    fd = open(devname, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0)
        return AVERROR(errno);

    memset(cap, 0, sizeof(*cap));
    ret = ioctl(fd, VIDIOC_QUERYCAP, cap);
    if (ret < 0) {
        ret = errno;
        close(fd);
        return AVERROR(ret);
    }

    return fd;
}

int ff_v4l2_device_probe_video(AVCodecContext *avctx,
                               struct V4L2DeviceVideo *device,
                               void *opaque,
                               bool (*checkdev)(struct V4L2DeviceVideo *device, void *opaque))
{
    int ret = AVERROR(EINVAL);
    struct dirent *entry;
    DIR *dirp;
    struct V4L2DeviceVideo tmpdev = {
        .devname = { 0 },
    };

    dirp = opendir("/dev");
    if (!dirp)
        return AVERROR(errno);

    for (entry = readdir(dirp); entry; entry = readdir(dirp)) {

        if (strncmp(entry->d_name, "video", 5))
            continue;

        snprintf(tmpdev.devname, sizeof(tmpdev.devname), "/dev/%s", entry->d_name);
        av_log(avctx, AV_LOG_INFO, "Probing device %s\n", tmpdev.devname);
        ret = v4l2_device_open_video(tmpdev.devname, &tmpdev.capability);
        if (ret < 0)
            continue;
        tmpdev.fd = ret;

        if (checkdev(&tmpdev, opaque)) {
            break;
        } else {
            close(tmpdev.fd);
            ret = AVERROR(EINVAL);
        }
    }

    closedir(dirp);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not find a valid video device\n");
        return ret;
    }

    *device = tmpdev;
    av_log(avctx, AV_LOG_INFO, "Using video device %s\n", device->devname);

    return 0;
}

static int v4l2_device_open_media(const char *devname, struct media_device_info *info)
{
    int fd, ret;

    fd = open(devname, O_RDWR, 0);
    if (fd < 0)
        return AVERROR(errno);

    memset(info, 0, sizeof(*info));
    ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, info);
    if (ret < 0) {
        ret = errno;
        close(fd);
        return AVERROR(ret);
    }

    return fd;
}

static int v4l2_device_check_media(AVCodecContext *avctx, int media_fd,
                            uint32_t video_major, uint32_t video_minor)
{
    int ret;
    struct media_v2_topology topology = {0};
    struct media_v2_interface *interfaces = NULL;

    ret = ioctl(media_fd, MEDIA_IOC_G_TOPOLOGY, &topology);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "%s: get media topology failed, %s (%d)\n",
               __func__, av_err2str(AVERROR(errno)), errno);
        return AVERROR(errno);
    }

    if (topology.num_interfaces <= 0) {
        av_log(avctx, AV_LOG_ERROR, "%s: media device has no interfaces\n", __func__);
        return AVERROR(EINVAL);
    }

    interfaces = av_mallocz(topology.num_interfaces * sizeof(struct media_v2_interface));
    if (!interfaces) {
        av_log(avctx, AV_LOG_ERROR, "%s: allocating media interface struct failed\n", __func__);
        return AVERROR(ENOMEM);
    }

    topology.ptr_interfaces = (__u64)(uintptr_t)interfaces;
    ret = ioctl(media_fd, MEDIA_IOC_G_TOPOLOGY, &topology);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "%s: get media topology failed, %s (%d)\n",
               __func__, av_err2str(AVERROR(errno)), errno);
        av_freep(&interfaces);
        return AVERROR(errno);
    }

    ret = AVERROR(EINVAL);
    for (int i = 0; i < topology.num_interfaces; i++) {
        if (interfaces[i].intf_type != MEDIA_INTF_T_V4L_VIDEO)
            continue;

        av_log(avctx, AV_LOG_INFO, "%s: media device number %d:%d\n", __func__, interfaces[i].devnode.major, interfaces[i].devnode.minor);
        if (interfaces[i].devnode.major == video_major && interfaces[i].devnode.minor == video_minor) {
            ret = 0;
            break;
        }
    }

    av_freep(&interfaces);
    return ret;
}

int ff_v4l2_device_retrieve_media(AVCodecContext *avctx,
                                  const struct V4L2DeviceVideo *video_device,
                                  struct V4L2DeviceMedia *media_device)
{
    int fd, ret = AVERROR(EINVAL);
    struct dirent *entry;
    DIR *dirp;
    char devname[PATH_MAX] = { 0 };
    struct media_device_info info;

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(statbuf));
    ret = fstat(video_device->fd, &statbuf);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "%s: get video device stats failed, %s (%d)\n",
               __func__, av_err2str(AVERROR(errno)), errno);
        return AVERROR(errno);
    }

    av_log(avctx, AV_LOG_INFO, "%s: video device number %d:%d\n", __func__, major(statbuf.st_dev), minor(statbuf.st_dev));

    dirp = opendir("/dev");
    if (!dirp)
        return AVERROR(errno);

    ret = AVERROR(EINVAL);
    for (entry = readdir(dirp); entry; entry = readdir(dirp)) {

        if (strncmp(entry->d_name, "media", 5))
            continue;

        snprintf(devname, sizeof(devname), "/dev/%s", entry->d_name);
        av_log(avctx, AV_LOG_INFO, "Probing device %s\n", devname);
        ret = v4l2_device_open_media(devname, &info);
        if (ret < 0)
            continue;

        fd = ret;
        ret = v4l2_device_check_media(avctx, fd, major(statbuf.st_rdev), minor(statbuf.st_rdev));
        if (ret == 0)
            break;

        close(ret);
    }

    closedir(dirp);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not find a valid media device\n");
        return ret;
    }

    av_log(avctx, AV_LOG_INFO, "Using media device %s\n", devname);
    strcpy(media_device->devname, devname);
    media_device->fd = fd;
    media_device->info = info;

    return 0;
}
