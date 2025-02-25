// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode on PLATFORM_DISPLAY_KHR code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"
#include "util/u_pacing.h"
#include "vk/vk_helpers.h"

#include "main/comp_window_direct.h"





/*
 *
 * Types, defines and data.
 *
 */

/*!
 * These formats will be 'preferred' - we may wish to give preference
 * to higher bit depths if they are available, but most display devices we are
 * interested in should support one these.
 */
static VkFormat preferred_color_formats[] = {
    VK_FORMAT_B8G8R8A8_SRGB,         //
    VK_FORMAT_R8G8B8A8_SRGB,         //
    VK_FORMAT_B8G8R8A8_UNORM,        //
    VK_FORMAT_R8G8B8A8_UNORM,        //
    VK_FORMAT_A8B8G8R8_UNORM_PACK32, // Just in case.
};


/*
 *
 * Vulkan functions.
 *
 */

static inline struct vk_bundle *
get_vk_cts(struct comp_target_none *cts)
{
	return &cts->base.c->base.vk;
}

static void
destroy_old(struct comp_target_none *cts, VkSwapchainKHR old)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	if (old != VK_NULL_HANDLE) {
		vk->vkDestroySwapchainKHR(vk->device, old, NULL);
	}
}

static void
destroy_image_views(struct comp_target_none *cts)
{
	if (cts->base.images == NULL) {
		return;
	}

	struct vk_bundle *vk = get_vk_cts(cts);

	for (uint32_t i = 0; i < cts->base.image_count; i++) {
		if (cts->base.images[i].view == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkDestroyImageView(vk->device, cts->base.images[i].view, NULL);
		cts->base.images[i].view = VK_NULL_HANDLE;
	}

	free(cts->base.images);
	cts->base.images = NULL;
}

static void
create_image_views(struct comp_target_none *cts)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	cts->base.image_count = 3;
	VkImage *images = U_TYPED_ARRAY_CALLOC(VkImage, cts->base.image_count);
	
	for (int i = 0; i < cts->base.image_count; i++)
	{
		VkExtent2D extent = {.width = cts->base.width, .height = cts->base.height};
#if 0
		VkImageCreateInfo image = {0};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = cts->base.format;
		image.extent.width = cts->base.width;
		image.extent.height = cts->base.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	
		vk->vkCreateImage(vk->device, &image, NULL, &images[i]);
#endif
		VkDeviceMemory out_mem;
		vk_create_image_simple(vk, extent, cts->base.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &out_mem, &images[i]);
	}

	destroy_image_views(cts);

	cts->base.images = U_TYPED_ARRAY_CALLOC(struct comp_target_image, cts->base.image_count);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	for (uint32_t i = 0; i < cts->base.image_count; i++) {
		cts->base.images[i].handle = images[i];
		vk_create_view(                 //
		    vk,                         // vk_bundle
		    cts->base.images[i].handle, // image
		    VK_IMAGE_VIEW_TYPE_2D,      // type
		    cts->surface.format.format, // format
		    subresource_range,          // subresource_range
		    &cts->base.images[i].view); // out_view
	}

	free(images);
}

static void
do_update_timings_google_display_timing(struct comp_target_none *cts)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	if (!vk->has_GOOGLE_display_timing) {
		return;
	}

	if (cts->swapchain.handle == VK_NULL_HANDLE) {
		return;
	}

	uint32_t count = 0;
	vk->vkGetPastPresentationTimingGOOGLE( //
	    vk->device,                        //
	    cts->swapchain.handle,             //
	    &count,                            //
	    NULL);                             //
	if (count <= 0) {
		return;
	}

	VkPastPresentationTimingGOOGLE *timings = U_TYPED_ARRAY_CALLOC(VkPastPresentationTimingGOOGLE, count);
	vk->vkGetPastPresentationTimingGOOGLE( //
	    vk->device,                        //
	    cts->swapchain.handle,             //
	    &count,                            //
	    timings);                          //
	uint64_t now_ns = os_monotonic_get_ns();
	for (uint32_t i = 0; i < count; i++) {
		u_pc_info(cts->upc,                       //
		          timings[i].presentID,           //
		          timings[i].desiredPresentTime,  //
		          timings[i].actualPresentTime,   //
		          timings[i].earliestPresentTime, //
		          timings[i].presentMargin,       //
		          now_ns);                        //
	}

	free(timings);
}

static void
do_update_timings_vblank_thread(struct comp_target_none *cts)
{
	if (!cts->vblank.has_started) {
		return;
	}

	uint64_t last_vblank_ns;

	os_thread_helper_lock(&cts->vblank.event_thread);
	last_vblank_ns = cts->vblank.last_vblank_ns;
	cts->vblank.last_vblank_ns = 0;
	os_thread_helper_unlock(&cts->vblank.event_thread);

	if (last_vblank_ns) {
		u_pc_update_vblank_from_display_control(cts->upc, last_vblank_ns);
	}
}

static void
target_fini_semaphores(struct comp_target_none *cts)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	if (cts->base.semaphores.present_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, cts->base.semaphores.present_complete, NULL);
		cts->base.semaphores.present_complete = VK_NULL_HANDLE;
	}

	if (cts->base.semaphores.render_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, cts->base.semaphores.render_complete, NULL);
		cts->base.semaphores.render_complete = VK_NULL_HANDLE;
	}
}

static void
target_init_semaphores(struct comp_target_none *cts)
{
	struct vk_bundle *vk = get_vk_cts(cts);
	VkResult ret;

	target_fini_semaphores(cts);

	VkSemaphoreTypeCreateInfo timelineCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.pNext = NULL,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = 1000,
	};
	

	VkSemaphoreCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .pNext = &timelineCreateInfo,
	};

	//cts->base.semaphores.present_complete_is_timeline = true;
	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cts->base.semaphores.present_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}
	info.pNext = &timelineCreateInfo;

	cts->base.semaphores.render_complete_is_timeline = true;
	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cts->base.semaphores.render_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	//cts->base.semaphores.present_complete = cts->base.semaphores.render_complete;
	cts->base.semaphores.render_is_offscreen = true;
}


/*
 *
 * Member functions.
 *
 */

static void
comp_target_none_create_images(struct comp_target *ct,
                                    uint32_t preferred_width,
                                    uint32_t preferred_height,
                                    VkFormat color_format,
                                    VkColorSpaceKHR color_space,
                                    VkImageUsageFlags image_usage,
                                    VkPresentModeKHR present_mode)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	struct vk_bundle *vk = get_vk_cts(cts);
	VkBool32 supported;
	VkResult ret;

	uint64_t now_ns = os_monotonic_get_ns();
	// Some platforms really don't like the pacing_compositor code.
	bool use_display_timing_if_available = cts->timing_usage == COMP_TARGET_USE_DISPLAY_IF_AVAILABLE;
	if (cts->upc == NULL && use_display_timing_if_available && vk->has_GOOGLE_display_timing) {
		u_pc_display_timing_create(ct->c->settings.nominal_frame_interval_ns,
		                           &U_PC_DISPLAY_TIMING_CONFIG_DEFAULT, &cts->upc);
	} else if (cts->upc == NULL) {
		u_pc_fake_create(ct->c->settings.nominal_frame_interval_ns, now_ns, &cts->upc);
	}

	// Free old image views.
	destroy_image_views(cts);

	target_init_semaphores(cts);


	/*
	 * Set target info.
	 */

	cts->base.width = preferred_width;
	cts->base.height = preferred_height;
	//cts->preferred.width = preferred_width;
	//cts->preferred.height = preferred_height;
	cts->preferred.color_space = color_space;
	cts->preferred.color_format = color_format;
	cts->base.format = color_format;
	cts->base.surface_transform = 0;
	cts->base.image_count = 3;

	//cts->base.info.formats[0] = color_format;
	//cts->base.info.format_count = 1;

	create_image_views(cts);
}

static bool
comp_target_none_has_images(struct comp_target *ct)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	return 1;
}

static VkResult
comp_target_none_acquire_next_image(struct comp_target *ct, uint32_t *out_index)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	struct vk_bundle *vk = get_vk_cts(cts);

	if (!comp_target_none_has_images(ct)) {
		//! @todo what error to return here?
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	static int inc = 0;

	*out_index = inc++;
	inc = inc % 3;
	return VK_SUCCESS;
}

static VkResult
comp_target_none_present(struct comp_target *ct,
                              VkQueue queue,
                              uint32_t index,
                              uint64_t timeline_semaphore_value,
                              uint64_t desired_present_time_ns,
                              uint64_t present_slop_ns)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	struct vk_bundle *vk = get_vk_cts(cts);

	VkResult ret = VK_SUCCESS;

#ifdef VK_EXT_display_control
	if (cts->vblank.has_started) {
		os_thread_helper_lock(&cts->vblank.event_thread);
		if (!cts->vblank.should_wait) {
			cts->vblank.should_wait = true;
			os_thread_helper_signal_locked(&cts->vblank.event_thread);
		}
		os_thread_helper_unlock(&cts->vblank.event_thread);
	}
#endif
	VkSemaphoreSignalInfo info = {
		.sType = VK_SEMAPHORE_TYPE_TIMELINE,
		.pNext = NULL,
		.semaphore = cts->base.semaphores.render_complete,
		.value = 1,
	};

	vk->vkSignalSemaphore(vk->device, &info);

	return ret;
}

static bool
comp_target_none_check_ready(struct comp_target *ct)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	return 1;
}


/*
 *
 * Timing member functions.
 *
 */

static void
comp_target_none_calc_frame_pacing(struct comp_target *ct,
                                        int64_t *out_frame_id,
                                        uint64_t *out_wake_up_time_ns,
                                        uint64_t *out_desired_present_time_ns,
                                        uint64_t *out_present_slop_ns,
                                        uint64_t *out_predicted_display_time_ns)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;

	int64_t frame_id = -1;
	uint64_t wake_up_time_ns = 0;
	uint64_t desired_present_time_ns = 0;
	uint64_t present_slop_ns = 0;
	uint64_t predicted_display_time_ns = 0;
	uint64_t predicted_display_period_ns = 0;
	uint64_t min_display_period_ns = 0;
	uint64_t now_ns = os_monotonic_get_ns();

	u_pc_predict(cts->upc,                     //
	             now_ns,                       //
	             &frame_id,                    //
	             &wake_up_time_ns,             //
	             &desired_present_time_ns,     //
	             &present_slop_ns,             //
	             &predicted_display_time_ns,   //
	             &predicted_display_period_ns, //
	             &min_display_period_ns);      //

	cts->current_frame_id = frame_id;

	*out_frame_id = frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_present_slop_ns = present_slop_ns;
}

static void
comp_target_none_mark_timing_point(struct comp_target *ct,
                                        enum comp_target_timing_point point,
                                        int64_t frame_id,
                                        uint64_t when_ns)
{
	struct comp_target_none *cts = (struct comp_target_none *)ct;
	assert(frame_id == cts->current_frame_id);

	switch (point) {
	case COMP_TARGET_TIMING_POINT_WAKE_UP:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_WAKE_UP, cts->current_frame_id, when_ns);
		break;
	case COMP_TARGET_TIMING_POINT_BEGIN:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_BEGIN, cts->current_frame_id, when_ns);
		break;
	case COMP_TARGET_TIMING_POINT_SUBMIT:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_SUBMIT, cts->current_frame_id, when_ns);
		break;
	default: assert(false);
	}
}

static VkResult
comp_target_none_update_timings(struct comp_target *ct)
{
	COMP_TRACE_MARKER();

	struct comp_target_none *cts = (struct comp_target_none *)ct;

	do_update_timings_google_display_timing(cts);
	do_update_timings_vblank_thread(cts);

	return VK_SUCCESS;
}

static void
comp_target_none_info_gpu(
    struct comp_target *ct, int64_t frame_id, uint64_t gpu_start_ns, uint64_t gpu_end_ns, uint64_t when_ns)
{
	COMP_TRACE_MARKER();

	struct comp_target_none *cts = (struct comp_target_none *)ct;

	u_pc_info_gpu(cts->upc, frame_id, gpu_start_ns, gpu_end_ns, when_ns);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
comp_target_none_cleanup(struct comp_target_none *cts)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	// Thread if it has been started must be stopped first.
	if (cts->vblank.has_started) {
		// Destroy also stops the thread.
		os_thread_helper_destroy(&cts->vblank.event_thread);
		cts->vblank.has_started = false;
	}

	destroy_image_views(cts);

	if (cts->swapchain.handle != VK_NULL_HANDLE) {
		vk->vkDestroySwapchainKHR( //
		    vk->device,            // device
		    cts->swapchain.handle, // swapchain
		    NULL);                 //
		cts->swapchain.handle = VK_NULL_HANDLE;
	}

	if (cts->surface.handle != VK_NULL_HANDLE) {
		vk->vkDestroySurfaceKHR( //
		    vk->instance,        // instance
		    cts->surface.handle, // surface
		    NULL);               //
		cts->surface.handle = VK_NULL_HANDLE;
	}

	target_fini_semaphores(cts);

	u_pc_destroy(&cts->upc);
}

void
comp_target_none_init_and_set_fnptrs(struct comp_target_none *cts,
                                          enum comp_target_display_timing_usage timing_usage)
{
	cts->timing_usage = timing_usage;
	cts->base.check_ready = comp_target_none_check_ready;
	cts->base.create_images = comp_target_none_create_images;
	cts->base.has_images = comp_target_none_has_images;
	cts->base.acquire = comp_target_none_acquire_next_image;
	cts->base.present = comp_target_none_present;
	cts->base.calc_frame_pacing = comp_target_none_calc_frame_pacing;
	cts->base.mark_timing_point = comp_target_none_mark_timing_point;
	cts->base.update_timings = comp_target_none_update_timings;
	cts->base.info_gpu = comp_target_none_info_gpu;
	os_thread_helper_init(&cts->vblank.event_thread);
}

/*
 *
 * Private structs
 *
 */

/*!
 * Probed display.
 */
struct vk_display
{
	VkDisplayPropertiesKHR display_properties;
	VkDisplayKHR display;
};

/*!
 * Direct mode "window" into a device, using PLATFORM_DISPLAY_KHR.
 *
 * @implements comp_target_none
 */
struct comp_window_none
{
	struct comp_target_none base;

	struct vk_display *displays;
	uint16_t display_count;
};

/*
 *
 * Forward declare functions
 *
 */

static void
comp_window_none_destroy(struct comp_target *ct);

static bool
comp_window_none_init(struct comp_target *ct);

static struct vk_display *
comp_window_none_current_display(struct comp_window_none *w);

static bool
comp_window_none_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);


/*
 *
 * Functions.
 *
 */

static inline struct vk_bundle *
get_vk(struct comp_target *ct)
{
	return &ct->c->base.vk;
}

static void
_flush(struct comp_target *ct)
{
	(void)ct;
}

static void
_update_window_title(struct comp_target *ct, const char *title)
{
	(void)ct;
	(void)title;
}

struct comp_target *
comp_window_none_create(struct comp_compositor *c)
{
	struct comp_window_none *w = U_TYPED_CALLOC(struct comp_window_none);

	// The display timing code hasn't been tested on vk display and may be broken.
	comp_target_none_init_and_set_fnptrs(&w->base, COMP_TARGET_FORCE_FAKE_DISPLAY_TIMING);

	w->base.base.name = "None";
	w->base.display = VK_NULL_HANDLE;
	w->base.base.destroy = comp_window_none_destroy;
	w->base.base.flush = _flush;
	w->base.base.init_pre_vulkan = comp_window_none_init;
	w->base.base.init_post_vulkan = comp_window_none_init_swapchain;
	w->base.base.set_title = _update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_none_destroy(struct comp_target *ct)
{
	struct comp_window_none *w_direct = (struct comp_window_none *)ct;

	comp_target_none_cleanup(&w_direct->base);

	for (uint32_t i = 0; i < w_direct->display_count; i++) {
		struct vk_display *d = &w_direct->displays[i];
		d->display = VK_NULL_HANDLE;
	}

	if (w_direct->displays != NULL)
		free(w_direct->displays);

	free(ct);
}

static bool
append_vk_display_entry(struct comp_window_none *w, struct VkDisplayPropertiesKHR *disp)
{
	w->base.base.c->settings.preferred.width = disp->physicalResolution.width;
	w->base.base.c->settings.preferred.height = disp->physicalResolution.height;
	struct vk_display d = {.display_properties = *disp, .display = disp->display};

	w->display_count += 1;

	U_ARRAY_REALLOC_OR_FREE(w->displays, struct vk_display, w->display_count);

	if (w->displays == NULL)
		COMP_ERROR(w->base.base.c, "Unable to reallocate vk_display displays");

	w->displays[w->display_count - 1] = d;

	return true;
}

static void
print_found_displays(struct comp_compositor *c, struct VkDisplayPropertiesKHR *display_props, uint32_t display_count)
{
	COMP_ERROR(c, "== Found Displays ==");
	for (uint32_t i = 0; i < display_count; i++) {
		struct VkDisplayPropertiesKHR *p = &display_props[i];

		COMP_ERROR(c, "[%d] %s with resolution %dx%d, dims %dx%d", i, p->displayName,
		           p->physicalResolution.width, p->physicalResolution.height, p->physicalDimensions.width,
		           p->physicalDimensions.height);
	}
}

static bool
comp_window_none_init(struct comp_target *ct)
{
	struct comp_window_none *w_direct = (struct comp_window_none *)ct;

	uint32_t display_count = 1;

	struct VkDisplayPropertiesKHR *display_props = U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);
	display_props->displayName = "VkNoneDisplay";
	display_props->physicalDimensions.width = 400;
	display_props->physicalDimensions.height = 400;
	display_props->physicalResolution.width = 1024;
	display_props->physicalResolution.height = 1024;

	if (ct->c->settings.vk_display > (int)display_count) {
		COMP_ERROR(ct->c, "Requested display %d, but only %d found.", ct->c->settings.vk_display,
		           display_count);
		print_found_displays(ct->c, display_props, display_count);
		free(display_props);
		return false;
	}

	append_vk_display_entry(w_direct, &display_props[ct->c->settings.vk_display]);

	struct vk_display *d = comp_window_none_current_display(w_direct);
	if (!d) {
		COMP_ERROR(ct->c, "display not found!");
		print_found_displays(ct->c, display_props, display_count);
		free(display_props);
		return false;
	}

	free(display_props);

	return true;
}

static struct vk_display *
comp_window_none_current_display(struct comp_window_none *w)
{
	int index = w->base.base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->display_count <= (uint32_t)index)
		return NULL;

	return &w->displays[index];
}

VkResult
comp_window_none_create_surface(struct comp_target_none *cts,
                                  VkDisplayKHR display,
                                  uint32_t width,
                                  uint32_t height)
{
	struct vk_bundle *vk = get_vk_cts(cts);

	VkResult result = VK_SUCCESS;
	cts->surface.handle = malloc(sizeof(VkSurfaceKHR)); // HACK

	return result;
}

static bool
init_swapchain(struct comp_target_none *cts, VkDisplayKHR display, uint32_t width, uint32_t height)
{
	VkResult ret;

	ret = comp_window_none_create_surface(cts, display, width, height);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "Failed to create surface! '%s'", vk_result_string(ret));
		return false;
	}

	return true;
}

static bool
comp_window_none_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_none *w_direct = (struct comp_window_none *)ct;

	struct vk_display *d = comp_window_none_current_display(w_direct);
	if (!d) {
		COMP_ERROR(ct->c, "display not found.");
		return false;
	}

	// TODO HACK HACK HACK
	//if (!d->display_properties.displayName) 
	{
		d->display_properties.displayName = "";
	}
	COMP_ERROR(ct->c, "%p %p %s %u\n", d, d->display_properties.displayName, d->display_properties.displayName, w_direct->display_count);
	COMP_DEBUG(ct->c, "Will use display: %s", d->display_properties.displayName);

	struct comp_target_none *cts = (struct comp_target_none *)ct;
	cts->display = d->display;

	return init_swapchain(&w_direct->base, d->display, width, height);
}


/*
 *
 * Factory
 *
 */

static bool
detect(const struct comp_target_factory *ctf, struct comp_compositor *c)
{
	return true;
}

static bool
create_target(const struct comp_target_factory *ctf, struct comp_compositor *c, struct comp_target **out_ct)
{
	struct comp_target *ct = comp_window_none_create(c);
	if (ct == NULL) {
		return false;
	}

	*out_ct = ct;

	return true;
}

const struct comp_target_factory comp_target_factory_none = {
    .name = "No window, swapchain only",
    .identifier = "none",
    .requires_vulkan_for_create = true,
    .is_deferred = false,
    .required_instance_extensions = NULL,
    .required_instance_extension_count = 0,
    .detect = detect,
    .create_target = create_target,
};