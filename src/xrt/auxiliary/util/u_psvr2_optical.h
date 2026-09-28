// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Shared PSVR2 camera-based Sense controller position tracking.
 */
#pragma once

#include "xrt/xrt_defines.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Publish the latest headset pose used as the optical tracking reference.
 */
void
u_psvr2_optical_publish_hmd_pose(const struct xrt_pose *pose, int64_t timestamp_ns);

/*!
 * Process a side-by-side 8-bit PSVR2 camera frame.
 *
 * The tracker detects bright controller markers, triangulates up to two
 * positions, and associates them with the left and right controllers.
 */
void
u_psvr2_optical_process_frame(const uint8_t *data,
                              uint32_t width,
                              uint32_t height,
                              uint32_t stride,
                              int64_t timestamp_ns);

/*!
 * Retrieve a recent optical position for one controller.
 */
bool
u_psvr2_optical_get_position(bool right_hand, int64_t timestamp_ns, struct xrt_vec3 *out_position);

/*!
 * Return whether optical tracking is enabled.
 */
bool
u_psvr2_optical_is_enabled(void);

#ifdef __cplusplus
}
#endif
