#include "main/comp_window.h"
#import <UIKit/UIWindow.h>
#import <UIKit/UIScreen.h>
#import <UIKit/UIViewController.h>
#import <Metal/Metal.h>

@interface MetalWindow : UIWindow @end
@implementation MetalWindow {}
	+ (Class)layerClass {
		return [CAMetalLayer class];
	}
@end

struct comp_window_uikit {
	struct comp_target_swapchain base;
	MetalWindow *window;
};

static void comp_window_uikit_destroy(struct comp_target *const target) {
	struct comp_window_uikit *const this = (struct comp_window_uikit*)target;
	comp_target_swapchain_cleanup(&this->base);
	MetalWindow *const window = this->window;
	this->window = nil;
	if(window != nil) {
		dispatch_sync(dispatch_get_main_queue(), ^{
			window.hidden = YES;
			[window release];
		});
	}
	free(this);
}

static bool comp_window_uikit_init(struct comp_target *const target) {
	dispatch_sync(dispatch_get_main_queue(), ^{
		struct comp_window_uikit *const this = (struct comp_window_uikit*)target;
		this->window = [MetalWindow.alloc initWithFrame:UIScreen.mainScreen.bounds];
		this->window.contentScaleFactor = UIScreen.mainScreen.nativeScale;
		this->window.opaque = YES;
		this->window.userInteractionEnabled = NO;
		this->window.layer.name = @"monado-render-surface";

		CAMetalLayer *const metalLayer = (CAMetalLayer*)this->window.layer;
		metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		metalLayer.framebufferOnly = YES;

		target->c->settings.preferred.width = metalLayer.drawableSize.width;
		target->c->settings.preferred.height = metalLayer.drawableSize.height;

		this->window.rootViewController = [UIViewController new];
		[this->window makeKeyAndVisible];
	});
	return true;
}

static bool comp_window_uikit_init_swapchain(struct comp_target *const target, const uint32_t width, const uint32_t height) {
	struct comp_window_uikit *const this = (struct comp_window_uikit*)target;
	const PFN_vkCreateMetalSurfaceEXT pfn_vkCreateMetalSurfaceEXT = target->c->base.vk.vkCreateMetalSurfaceEXT;
	const VkInstance instance = target->c->base.vk.instance;
	const UIWindow *const window = this->window;
	__block VkResult result = VK_ERROR_UNKNOWN;
	__block VkSurfaceKHR surface = VK_NULL_HANDLE;
	dispatch_sync(dispatch_get_main_queue(), ^{
		const VkMetalSurfaceCreateInfoEXT surface_info = {
			.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
			.pLayer = (CAMetalLayer*)window.layer,
		};
		result = pfn_vkCreateMetalSurfaceEXT(instance, &surface_info, NULL, &surface);
	});
	if(result != VK_SUCCESS) {
		COMP_ERROR(target->c, "vkCreateMetalSurfaceEXT: %s", vk_result_string(result));
	} else {
		this->base.surface.handle = surface;
	}
	return result == VK_SUCCESS;
}

static void comp_window_uikit_flush(struct comp_target *const target) {}

XRT_MAYBE_UNUSED static VkResult (*comp_target_swapchain_present)(struct comp_target *ct, VkQueue queue, uint32_t index, uint64_t timeline_semaphore_value, uint64_t desired_present_time_ns, uint64_t present_slop_ns) = NULL;
XRT_MAYBE_UNUSED static VkResult comp_window_uikit_present(struct comp_target *const target, const VkQueue queue, const uint32_t index, const uint64_t timeline_semaphore_value, uint64_t desired_present_time_ns, uint64_t present_slop_ns) {
	desired_present_time_ns = 0; // MoltenVK deadlocks without this; needs further investigation
	present_slop_ns = 0;
	return comp_target_swapchain_present(target, queue, index, timeline_semaphore_value, desired_present_time_ns, present_slop_ns);
}

static void comp_window_uikit_set_title(struct comp_target *const target, const char title[]) {}
struct comp_target *comp_window_uikit_create(struct comp_compositor *const comp) {
	struct comp_window_uikit *const this = U_TYPED_CALLOC(struct comp_window_uikit);

	comp_target_swapchain_init_and_set_fnptrs(&this->base, COMP_TARGET_FORCE_FAKE_DISPLAY_TIMING);
	comp_target_swapchain_present = this->base.base.present;
	this->base.base.present = comp_window_uikit_present;

	this->base.base.name = comp_target_factory_uikit.identifier;
	this->base.base.destroy = comp_window_uikit_destroy;
	this->base.base.flush = comp_window_uikit_flush;
	this->base.base.init_pre_vulkan = comp_window_uikit_init;
	this->base.base.init_post_vulkan = comp_window_uikit_init_swapchain;
	this->base.base.set_title = comp_window_uikit_set_title;
	this->base.base.c = comp;
	return &this->base.base;
}

static bool create_target(const struct comp_target_factory *const factory, struct comp_compositor *const comp, struct comp_target **const out_target) {
	struct comp_target *const target = comp_window_uikit_create(comp);
	if(target == NULL)
		return false;
	*out_target = target;
	return true;
}

static bool detect(const struct comp_target_factory *factory, struct comp_compositor *comp) {
	return true;
}

static const char *instance_extensions[] = {
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
};

const struct comp_target_factory comp_target_factory_uikit = {
    .name = "UIKit",
    .identifier = "uikit",
    .requires_vulkan_for_create = true,
    .is_deferred = false,
    .required_instance_extensions = instance_extensions,
    .required_instance_extension_count = ARRAY_SIZE(instance_extensions),
    .detect = detect,
    .create_target = create_target,
};
