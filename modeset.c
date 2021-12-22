#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <errno.h>
#include <signal.h>
#include <libdrm/drm_fourcc.h>

struct buffer_object {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
	uint32_t size;
	uint8_t *vaddr;
	uint32_t fb_id;
};

struct buffer_object buf[4];
uint32_t valid_plane_id[10];
static int fd;
static uint32_t crtc_id;
uint32_t property_crtc_id;
drmModeAtomicReq *req;
static int terminate = 0;

static void sigint_handler(int arg)
{
	arg = 1;
	terminate = 1;
	printf("\nstop\n");
}

static int modeset_create_fb(int fd, struct buffer_object *bo, uint32_t color)
{
	//external/libdrm/include/drm/drm_mode.h
	struct drm_mode_create_dumb create = {};
	struct drm_mode_map_dumb map = {};
	int ret;
	int i;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	/* create a dumb-buffer, the pixel
	 * format is
	 * XRGB888 */
	create.width = bo->width;
	create.height = bo->height;
	create.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if (ret < 0) {
		printf("cannot create dumb buffer (%s)\n",
				strerror(errno));
		return -errno;
	}

	/* bind the dumb-buffer to an FB object*/
	bo->pitch = create.pitch;
	bo->size = create.size;
	bo->handle = create.handle;

	handles[0] = bo->handle;
	pitches[0] = bo->pitch;
	offsets[0] = 0;
	ret = drmModeAddFB(fd, bo->width, bo->height, 32, create.bpp, bo->pitch,
			bo->handle, &bo->fb_id);
	//ret = drmModeAddFB2(fd, bo->width, bo->height, DRM_FORMAT_ARGB8888, handles,
	//		pitches, offsets, &bo->fb_id, 0);
	if (ret) {
		printf("cannot create framebuffer (%d): %m\n",
				errno);
		ret = -errno;
		return -errno;
	}
	printf("w=%d h=%d pitch=%d size=%d, handle=%d\n", bo->width,
			bo->height, bo->pitch, bo->size, bo->handle);


	/* map the dumb-buffer to userspace */
	map.handle = create.handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if (ret) {
		printf("cannot map dump (%s): %m\n",
				strerror(errno));
		ret = -errno;
		return -errno;
	}

	bo->vaddr = mmap64(0, create.size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, map.offset);
	if (bo->vaddr == NULL){
		printf("cannot map fail (%s) for offset=%llu\n",
				strerror(errno), map.offset);
		ret = -errno;
		return -errno;
	}

	for (i = 0; i < (int)(bo->size / 4); i++){
		*(uint32_t *)(bo->vaddr + i*4) = color;
	}
	return 0;
}

static void modeset_destroy_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_destroy_dumb destroy = {};

	drmModeRmFB(fd, bo->fb_id);

	munmap(bo->vaddr, bo->size);

	destroy.handle = bo->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

/*
 * Find the property IDs and value that match its name.
 */
static void getPropertyValue(uint32_t objectID, uint32_t objectType,
                          const char *propName, uint32_t* propId,
                          uint64_t* value, int drmfd)
{
    uint64_t ivalue = 0;
    uint32_t id = 0;
    drmModeObjectPropertiesPtr pModeObjectProperties =
        drmModeObjectGetProperties(drmfd, objectID, objectType);

    if (pModeObjectProperties == NULL) {
        printf("drmModeObjectGetProperties failed.");
        return;
    }

    for (uint32_t i = 0; i < pModeObjectProperties->count_props; i++) {
        drmModePropertyPtr pProperty =
            drmModeGetProperty(drmfd, pModeObjectProperties->props[i]);
        if (pProperty == NULL) {
            printf("drmModeGetProperty failed.");
            continue;
        }

        if (strcmp(propName, pProperty->name) == 0) {
            ivalue = pModeObjectProperties->prop_values[i];
            id = pProperty->prop_id;
            drmModeFreeProperty(pProperty);
            break;
        }

        drmModeFreeProperty(pProperty);
    }

    drmModeFreeObjectProperties(pModeObjectProperties);

    if (propId != NULL) {
        *propId = id;
    }

    if (value != NULL) {
        *value = ivalue;
    }
}

void show_plane(int p_id, int alpha, int plane_id, int plane_zpos)
{
	//set PLANE by property
	uint32_t property_fb_id;
	uint32_t property_crtc_x;
	uint32_t property_crtc_y;
	uint32_t property_crtc_w;
	uint32_t property_crtc_h;
	uint32_t property_src_x;
	uint32_t property_src_y;
	uint32_t property_src_w;
	uint32_t property_src_h;
	uint32_t property_z;
	uint32_t property_alpha;
	uint64_t zpos;
	//uint32_t plane_id;
	//plane_id = plane_test_id1;//valid_plane_id[p_id];
	/* get plane properties */
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID",
			&property_crtc_id, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID",
			&property_fb_id, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X",
			&property_crtc_x, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y",
			&property_crtc_y, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W",
			&property_crtc_w, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H",
			&property_crtc_h, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X",
			&property_src_x, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y",
			&property_src_y, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W",
			&property_src_w, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H",
			&property_src_h, NULL, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "zpos",
			&property_z, &zpos, fd);
	getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "alpha",
			&property_alpha, NULL, fd);
	/* atomic plane update */
	req = drmModeAtomicAlloc();
	drmModeAtomicAddProperty(req, plane_id, property_crtc_id, crtc_id);
	//if((p_id != 0)){
	if(1){
		drmModeAtomicAddProperty(req, plane_id, property_fb_id, buf[p_id].fb_id);
		drmModeAtomicAddProperty(req, plane_id, property_crtc_x, 0);
		drmModeAtomicAddProperty(req, plane_id, property_crtc_y, 0);
		drmModeAtomicAddProperty(req, plane_id, property_crtc_w, 500 - 40 * plane_zpos);
		drmModeAtomicAddProperty(req, plane_id, property_crtc_h, 300 - 40 * plane_zpos);
		drmModeAtomicAddProperty(req, plane_id, property_src_x, 0);
		drmModeAtomicAddProperty(req, plane_id, property_src_y, 0);
		drmModeAtomicAddProperty(req, plane_id, property_src_w, (1920)<< 16);
		drmModeAtomicAddProperty(req, plane_id, property_src_h, (1080)<< 16);
		drmModeAtomicAddProperty(req, plane_id, property_src_w, (500-40*plane_zpos)<< 16);
		drmModeAtomicAddProperty(req, plane_id, property_src_h, (300-40*plane_zpos)<< 16);
	}
	drmModeAtomicAddProperty(req, plane_id, property_z, plane_zpos);
	drmModeAtomicAddProperty(req, plane_id, property_alpha, alpha);
	drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	printf("plane_id=%d, z=%d, alpha=%d, w=%d, h=%d %s\n", plane_id, plane_zpos, alpha, 500-40*plane_zpos, 300-40*plane_zpos, strerror(errno));
	drmModeAtomicFree(req);
}

void clean_plane(int plane_id)
{
    uint32_t property_fb_id;
    uint32_t property_alpha;
    printf("clean plane: %d\n", plane_id);

    getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &property_crtc_id, NULL, fd);
    getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", &property_fb_id, NULL, fd);
    getPropertyValue(plane_id, DRM_MODE_OBJECT_PLANE, "alpha", &property_alpha, NULL, fd);

    req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, plane_id, property_crtc_id, 0);
    drmModeAtomicAddProperty(req, plane_id, property_fb_id, 0);
    drmModeAtomicAddProperty(req, plane_id, property_alpha, 0);
    drmModeAtomicCommit(fd, req, NULL, NULL);
    drmModeAtomicFree(req);
    printf("plane cleaned: %d, (%s)\n", plane_id, strerror(errno));
}


int main(int argc, char **argv)
{
	int ret=0;
	drmModeConnector *conn;
	drmModeRes *res;
	drmModePlaneRes *plane_res;
	drmModePlanePtr plane[10];
	uint32_t plane_id;
	uint32_t conn_id;
        int plane_test_id1;
        int plane_test_zpos1;
        int plane_test_id2;
        int plane_test_zpos2;

	//char *drmCardPath = "/dev/dri/controlD64";
        char *drmCardPath = "/dev/dri/card0";
	int i,j;

	if (argc != 5) {
		printf("modeset plane_id1 zpos1 plane_id2 zpos2\n");
		return -1;
	}

	signal(SIGINT, sigint_handler);

	plane_test_id1 = atoi(argv[1]);
	plane_test_zpos1 = atoi(argv[2]);
	plane_test_id2 = atoi(argv[3]);
	plane_test_zpos2 = atoi(argv[4]);

	printf("plane_id=%d, zpos=%d \n", plane_test_id1, plane_test_zpos1);
        printf("plane_id2=%d, zpos2=%d \n", plane_test_id2, plane_test_zpos2);

	fd = open(drmCardPath, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		printf("cannot open %s\n", drmCardPath);
		return ret;
	}

	res = drmModeGetResources(fd);
	if (!res) {
		printf("cannot retrieve DRM resources (%d): %m\n",
				errno);
		return -errno;
	}

	for (i = 0; i < res->count_connectors; ++i) {
		conn_id = res->connectors[i];
		conn = drmModeGetConnector(fd, conn_id);
		if (!conn) {
			printf("cannot retrieve DRM connector %u:%u (%d): %m\n",
					i, conn_id, errno);
			continue;
		}

		if (conn->connection == DRM_MODE_CONNECTED) {
			printf("find connector %d in %d\n", i, res->count_connectors);
			break;
		}
	}

	drmModeEncoderPtr enc;
	uint32_t enc_id, crtc_idx;
	uint32_t crtcs_for_connector = 0;
	uint32_t active_crtcs = 0;
	printf("enc nums = %d \n", res->count_encoders);
	for (i = 0; i < res->count_encoders; ++i) {
		enc_id = res->encoders[i];
		enc = drmModeGetEncoder(fd, enc_id);
		crtcs_for_connector |= enc->possible_crtcs; 

		for (j = 0; j < res->count_crtcs; ++j) {
			if (enc->crtc_id == res->crtcs[j])
				active_crtcs |= 1 < j; 
		}
	}
	if (crtcs_for_connector & active_crtcs)
		crtc_idx = ffs(crtcs_for_connector & active_crtcs);
	else
		crtc_idx = ffs(crtcs_for_connector);

	printf("crtc idx = %d \n", crtc_idx);
	crtc_id = res->crtcs[crtc_idx - 1];

	//get plane ID
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	plane_res = drmModeGetPlaneResources(fd);
	uint32_t valid_num=0;
	for (i = 0; i < (int)plane_res->count_planes; i++) {
		plane_id = plane_res->planes[i];
		plane[i] = drmModeGetPlane(fd, plane_id);
		if ((plane[i]->possible_crtcs)&(0x01<<(crtc_idx - 1))){
			printf("valid plane[%d].id=%d \n",
					i, plane_id);
			valid_plane_id[valid_num++] = plane_id;
		}
	}
	printf("valid plane num=%d\n", valid_num);

	//get drm modes
	buf[0].width = conn->modes[0].hdisplay;
	buf[0].height = conn->modes[0].vdisplay;
	buf[1].width = conn->modes[0].hdisplay;
	buf[1].height = conn->modes[0].vdisplay;
	buf[2].width = conn->modes[0].hdisplay;
	buf[2].height = conn->modes[0].vdisplay;
	buf[3].width = conn->modes[0].hdisplay;
	buf[3].height = conn->modes[0].vdisplay;

        printf("Mode: %d x %d\n", conn->modes[0].hdisplay, conn->modes[0].vdisplay);

	modeset_create_fb(fd, &buf[0], 0xffff0000);
	modeset_create_fb(fd, &buf[1], 0xff00ff00);
	modeset_create_fb(fd, &buf[2], 0xff0000ff);
	modeset_create_fb(fd, &buf[3], 0xff0000ff);

#if 0
	struct custom_buf *rgbbuf;
	uint32_t hsae_handle;
	rgbbuf = custom_buf_alloc(1280*720*4, BUF_TYPE_CUSTOM);
	drmPrimeFDToHandle(fd, rgbbuf->fd, &hsae_handle);
	printf("hsae_handle=%d, %s\n", hsae_handle, strerror(errno));
	drmModeAddFB(fd, 1280, 720, 32, 32, 5120,
			hsae_handle, &(buf[0].fb_id));
	printf("fb_id=%d, %s\n", buf[0].fb_id, strerror(errno));
	for (i = 0; i < (int)((1280*720*4) / 4); i++){
		*(uint32_t *)(rgbbuf->buf_vaddr + i*4) = 0x100000ff;
	}
#endif
	//set CRTC by property
	uint32_t property_mode_id;
	uint32_t property_active;
	uint32_t mode_id;
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	/* get connector properties */
	getPropertyValue(conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID",
			&property_crtc_id, NULL, fd);
	/* get crtc properties */
	getPropertyValue(crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE",
			&property_active, NULL, fd);
	getPropertyValue(crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID",
			&property_mode_id , NULL, fd);
	/* create blob to store current mode, and retun the mode id */
	drmModeCreatePropertyBlob(fd, &conn->modes[0],
			sizeof(conn->modes[0]), &mode_id);
	/* start modeseting */
	req = drmModeAtomicAlloc();
	drmModeAtomicAddProperty(req, crtc_id, property_active, 1);
	drmModeAtomicAddProperty(req, crtc_id, property_mode_id, mode_id);
	drmModeAtomicAddProperty(req, conn_id, property_crtc_id, crtc_id);
	ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	drmModeAtomicFree(req);
	printf("drmModeAtomicCommit SetCrtc (%d: %s)\n", ret, strerror(errno));

        printf("Show plane %d\n", plane_test_id1);
        show_plane(0, 65535, plane_test_id1, plane_test_zpos1);
        usleep(2000*1000);
        printf("Show plane %d\n", plane_test_id2);
	show_plane(1, 65535, plane_test_id2, plane_test_zpos2);

	usleep(2000*1000);

        // Disable second plane
        printf("Close/clean the second plane: zpos=%d\n", plane_test_zpos2);
        clean_plane(plane_test_id2);
	usleep(2000*1000);
       // Disable first plane
        printf("Close/clean the first plane: zpos=%d\n", plane_test_zpos1);
        clean_plane(plane_test_id1);

	modeset_destroy_fb(fd, &buf[0]);
	modeset_destroy_fb(fd, &buf[1]);
	modeset_destroy_fb(fd, &buf[2]);
	modeset_destroy_fb(fd, &buf[3]);
        printf("Buffer freed \n");

	drmModeFreeConnector(conn);
	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);

	close(fd);
	printf("close fd \n");

	return 0;
}


