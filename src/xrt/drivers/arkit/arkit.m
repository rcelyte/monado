#include "arkit.h"
#import <ARKit/ARKit.h>
#include "math/m_api.h"
#include "os/os_threading.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_logging.h"
#include "xrt/xrt_device.h"

@interface arkit_device : NSObject<ARSessionDelegate> @end
@implementation arkit_device {
		@public
		struct xrt_device base;
		struct u_cardboard_distortion cardboard;
		ARSession *session;
		struct os_mutex poseLock;
		uint64_t timestamp;
		struct xrt_pose pose;
		struct xrt_input inputs[1];
		struct xrt_hmd_parts hmd;
		struct xrt_tracking_origin tracking_origin;
	}
	- (void)session:(ARSession*)session didUpdateFrame:(ARFrame*)frame {
		const uint64_t timestamp_ns = frame.timestamp * 1000000000. + clock_gettime_nsec_np(CLOCK_MONOTONIC) - clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
		os_mutex_lock(&self->poseLock);
		self->timestamp = timestamp_ns;
		const simd_float4x4 mat = frame.camera.transform;
		const struct xrt_vec3 cameraPos = {mat.columns[3].x, mat.columns[3].y, mat.columns[3].z};
		const struct xrt_matrix_3x3 cameraRot = {{
			mat.columns[0].x, mat.columns[1].x, mat.columns[2].x,
			mat.columns[0].y, mat.columns[1].y, mat.columns[2].y,
			mat.columns[0].z, mat.columns[1].z, mat.columns[2].z,
		}};
		self->pose.position = (struct xrt_vec3){0.f, 0.f, 0.f}; // TODO: camera -> head/screen offset
		math_quat_from_matrix_3x3(&cameraRot, &self->pose.orientation);
		math_quat_rotate_vec3(&self->pose.orientation, &self->pose.position, &self->pose.position);
		math_vec3_accum(&cameraPos, &self->pose.position);
		os_mutex_unlock(&self->poseLock);
	}
@end

static arkit_device *as_arkit_device(struct xrt_device *const base) {
	return (arkit_device*)((uint8_t*)base - (uintptr_t)&((arkit_device*)nil)->base);
}

static void arkit_device_update_inputs(struct xrt_device *const base) {}

static void arkit_device_get_tracked_pose(struct xrt_device *const base, const enum xrt_input_name name, const uint64_t at_timestamp_ns,
		struct xrt_space_relation *const out_relation) {
	arkit_device *const this = as_arkit_device(base);
	os_mutex_lock(&this->poseLock);
	out_relation->pose = this->pose;
	// TODO: adjust pose for latency using CMMotionManager + `this->timestamp` (accounting for camera -> accelerometer positional offset)
	os_mutex_unlock(&this->poseLock);
	/*out_relation->linear_velocity = ...;
	out_relation->angular_velocity = ...;*/
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
}

static bool arkit_device_compute_distortion(struct xrt_device *const base, const uint32_t view, const float u, const float v,
		struct xrt_uv_triplet *const result) {
	arkit_device *const this = as_arkit_device(base);
	return u_compute_distortion_cardboard(&this->cardboard.values[view], u, v, result);
}

static ARWorldMap *TryLoadCalibration(void) {
	NSError *error = nil;
	do {
		NSURL *const url = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
		if(url == nil)
			break;
		NSData *const data = [NSData dataWithContentsOfURL:[url URLByAppendingPathComponent:@"map.arexperience"] options:0 error:&error];
		if(data == nil)
			break;
		ARWorldMap *const worldMap = [NSKeyedUnarchiver unarchivedObjectOfClass:[ARWorldMap class] fromData:data error:&error];
		if(worldMap == nil)
			break;
		return worldMap;
	} while(false);
	U_LOG_W("Failed to load ARKit world map%s%s", (error != nil) ? ": " : "", (error != nil) ? error.localizedDescription.UTF8String : "");
	return nil;
}

static void arkit_device_destroy(struct xrt_device *const base) {@autoreleasepool {
	arkit_device *const this = as_arkit_device(base);
	dispatch_sync(dispatch_get_main_queue(), ^{@autoreleasepool { // TODO: necessary?
		[this->session pause];
		this->session.delegate = nil;
		[this->session release];
		this->session = nil;
	}});
	free(this->hmd.distortion.mesh.vertices);
	this->hmd.distortion.mesh.vertices = NULL;
	free(this->hmd.distortion.mesh.indices);
	this->hmd.distortion.mesh.indices = NULL;
	os_mutex_destroy(&this->poseLock);
	[this release];
}}

struct xrt_device *arkit_device_create(void) {@autoreleasepool {
	arkit_device *const this = [arkit_device new];
	if(os_mutex_init(&this->poseLock) != 0) {
		U_LOG_E("Failed to init mutex!");
		return NULL;
	}
	this->base.input_count = 1;
	this->base.inputs = this->inputs;
	this->inputs[0].active = true;
	this->base.hmd = &this->hmd;
	this->base.tracking_origin = &this->tracking_origin;
	this->tracking_origin.type = XRT_TRACKING_TYPE_NONE;
	this->tracking_origin.offset.orientation.w = 1.0f;
	snprintf(this->tracking_origin.name, XRT_TRACKING_NAME_LEN, "%s", "No tracking");

	this->base.name = XRT_DEVICE_GENERIC_HMD;
	this->base.destroy = arkit_device_destroy;
	this->base.update_inputs = arkit_device_update_inputs;
	this->base.get_tracked_pose = arkit_device_get_tracked_pose;
	this->base.get_view_poses = u_device_get_view_poses;
	this->base.compute_distortion = arkit_device_compute_distortion;
	this->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	this->base.device_type = XRT_DEVICE_TYPE_HMD;
	snprintf(this->base.str, XRT_DEVICE_NAME_LEN, "ARKit Tracking");
	snprintf(this->base.serial, XRT_DEVICE_NAME_LEN, "ARKit Tracking");

	const struct u_cardboard_distortion_arguments args = {
		.distortion_k = {.441f, .156f, 0.f, 0.f, 0.f},
		.screen = {
			.w_pixels = 1334,
			.h_pixels = 750,
			.w_meters = 1334.f / 326.f * .0254f,
			.h_meters = 750.f / 326.f * .0254f,
		},
		.inter_lens_distance_meters = .06f,
		.lens_y_center_on_screen_meters = 750.f / 326.f * .0254f / 2.f,
		.screen_to_lens_distance_meters = .042f,
		.fov = {
			.angle_left = -45 * M_PI / 180.,
			.angle_right = 45 * M_PI / 180.,
			.angle_up = 45 * M_PI / 180.,
			.angle_down = -45 * M_PI / 180.,
		},
	};
	// this->hmd.view_count = 2;
	u_distortion_cardboard_calculate(&args, &this->hmd, &this->cardboard);
	u_distortion_mesh_fill_in_compute(&this->base);

	this->timestamp = clock_gettime_nsec_np(CLOCK_MONOTONIC);
	this->pose = (struct xrt_pose)XRT_POSE_IDENTITY;

	dispatch_sync(dispatch_get_main_queue(), ^{@autoreleasepool { // TODO: necessary?
		this->session = [ARSession new];
		this->session.delegate = this;
		ARWorldTrackingConfiguration *const config = [ARWorldTrackingConfiguration new];
		// config.planeDetection = YES; // TODO: detect floor height
		config.worldAlignment = ARWorldAlignmentGravity;
		config.lightEstimationEnabled = NO;
		bool found = false;
		for(ARVideoFormat *const format in ARWorldTrackingConfiguration.supportedVideoFormats) {
			if(format.captureDevicePosition == AVCaptureDevicePositionFront)
				continue;
			if(found && (format.framesPerSecond < config.videoFormat.framesPerSecond || (double)format.imageResolution.width * (double)format.imageResolution.height >=
					(double)config.videoFormat.imageResolution.width * (double)config.videoFormat.imageResolution.height))
				continue;
			config.videoFormat = format;
			found = true;
		}
		config.initialWorldMap = TryLoadCalibration();
		[this->session runWithConfiguration:config];
	}});

	return &[this retain]->base;
}}
