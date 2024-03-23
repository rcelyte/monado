#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_builders.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "arkit/arkit.h"

#include <assert.h>
#import <ARKit/ARConfiguration.h>

#ifndef XRT_BUILD_DRIVER_ARKIT
#error "Must only be built with XRT_BUILD_DRIVER_ARKIT set"
#endif

static const char *driver_list[] = {
    "arkit",
    NULL,
};

static xrt_result_t iphone_estimate_system(struct xrt_builder *const this, cJSON *const config, struct xrt_prober *const prober,
		struct xrt_builder_estimate *const estimate) {
	estimate->certain.dof6 = estimate->certain.head = estimate->maybe.dof6 = estimate->maybe.head = ARWorldTrackingConfiguration.isSupported;
	estimate->priority = -10;
	return XRT_SUCCESS;
}

static xrt_result_t iphone_open_system(struct xrt_builder *const this, cJSON *const config, struct xrt_prober *const prober,
		struct xrt_system_devices **const out_devices, struct xrt_space_overseer **const out_overseer) {
	assert(out_devices != NULL && *out_devices == NULL);
	struct u_system_devices *sysDevs = u_system_devices_allocate();
	sysDevs->base.roles.head = sysDevs->base.xdevs[sysDevs->base.xdev_count++] = arkit_device_create();
	*out_devices = &sysDevs->base;
	u_builder_create_space_overseer(&sysDevs->base, out_overseer);
	return XRT_SUCCESS;
}

static void iphone_destroy(struct xrt_builder *const this) {
	free(this);
}

struct xrt_builder *t_builder_iphone_create(void) {
	struct xrt_builder *const this = U_TYPED_CALLOC(struct xrt_builder);
	this->estimate_system = iphone_estimate_system;
	this->open_system = iphone_open_system;
	this->destroy = iphone_destroy;
	this->identifier = "iPhone";
	this->name = "Cardboard with ARKit tracking";
	this->driver_identifiers = driver_list;
	this->driver_identifier_count = ARRAY_SIZE(driver_list) - 1;
	return this;
}
