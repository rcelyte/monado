// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  List of all @ref xrt_builder creation functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_drivers.h"


/*
 *
 * Config checking, sorted alphabetically.
 *
 */

// Always enabled.
#define T_BUILDER_LEGACY

#if defined(XRT_BUILD_DRIVER_ARKIT) || defined(XRT_DOXYGEN)
#define T_BUILDER_IPHONE
#endif

#if defined(XRT_BUILD_DRIVER_SURVIVE) || defined(XRT_BUILD_DRIVER_VIVE) || defined(XRT_DOXYGEN)
#define T_BUILDER_LIGHTHOUSE
#endif

#if defined(XRT_BUILD_DRIVER_NS) || defined(XRT_DOXYGEN)
#define T_BUILDER_NS
#endif

#if defined(XRT_BUILD_DRIVER_REMOTE) || defined(XRT_DOXYGEN)
#define T_BUILDER_REMOTE
#endif

#if defined(XRT_BUILD_DRIVER_QWERTY) || defined(XRT_DOXYGEN)
#define T_BUILDER_QWERTY
#endif

#if defined(XRT_BUILD_DRIVER_PSMV) || defined(XRT_BUILD_DRIVER_PSVR) || defined(XRT_DOXYGEN)
#define T_BUILDER_RGB_TRACKING
#endif

#if defined(XRT_BUILD_DRIVER_SIMULATED) || defined(XRT_DOXYGEN)
#define T_BUILDER_SIMULATED
#endif

#if defined(XRT_BUILD_DRIVER_SIMULAVR) || defined(XRT_DOXYGEN)
#define T_BUILDER_SIMULAVR
#endif

#if defined(XRT_BUILD_DRIVER_WMR) || defined(XRT_DOXYGEN)
#define T_BUILDER_WMR
#endif


/*
 *
 * Setter upper creation functions, sorted alphabetically.
 *
 */

#ifdef T_BUILDER_LEGACY
/*!
 * Builder used as a fallback for drivers not converted to builders yet.
 */
struct xrt_builder *
t_builder_legacy_create(void);
#endif

#ifdef T_BUILDER_IPHONE
/*!
 * Builder for Cardboard on iPhone
 */
struct xrt_builder *
t_builder_iphone_create(void);
#endif

#ifdef T_BUILDER_LIGHTHOUSE
/*!
 * Builder for Lighthouse-tracked devices (vive, index, tundra trackers, etc.)
 */
struct xrt_builder *
t_builder_lighthouse_create(void);
#endif

#ifdef T_BUILDER_NS
/*!
 * Builder for NorthStar headsets
 */
struct xrt_builder *
t_builder_north_star_create(void);
#endif

#ifdef T_BUILDER_QWERTY
/*!
 * The qwerty driver builder.
 */
struct xrt_builder *
t_builder_qwerty_create(void);
#endif

#ifdef T_BUILDER_REMOTE
/*!
 * The remote driver builder.
 */
struct xrt_builder *
t_builder_remote_create(void);
#endif

#ifdef T_BUILDER_RGB_TRACKING
/*!
 * RGB tracking based drivers, like @ref drv_psmv and @ref drv_psvr.
 */
struct xrt_builder *
t_builder_rgb_tracking_create(void);
#endif

#ifdef T_BUILDER_SIMULATED
/*!
 * Builder for @drv_simulated devices.
 */
struct xrt_builder *
t_builder_simulated_create(void);
#endif


#ifdef T_BUILDER_SIMULAVR
/*!
 * Builder for SimulaVR headsets
 */
struct xrt_builder *
t_builder_simula_create(void);
#endif

#ifdef T_BUILDER_WMR
/*!
 * Builder for Windows Mixed Reality headsets
 */
struct xrt_builder *
t_builder_wmr_create(void);
#endif
