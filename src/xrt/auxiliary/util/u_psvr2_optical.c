// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Shared PSVR2 camera-based Sense controller position tracking.
 */

#include "u_psvr2_optical.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"
#include "util/u_debug.h"
#include "util/u_time.h"

#include <pthread.h>

#define OPTICAL_MAX_CANDIDATES 8
#define OPTICAL_MAX_TRACKS 2
#define OPTICAL_MAX_AGE_NS (250 * U_TIME_1MS_IN_NS)

DEBUG_GET_ONCE_BOOL_OPTION(psvr2_optical_tracking, "PSSENSE_OPTICAL_TRACKING", true)
DEBUG_GET_ONCE_FLOAT_OPTION(psvr2_optical_baseline, "PSSENSE_OPTICAL_BASELINE_M", 0.063f)
DEBUG_GET_ONCE_FLOAT_OPTION(psvr2_optical_focal, "PSSENSE_OPTICAL_FOCAL_PX", 320.0f)
DEBUG_GET_ONCE_FLOAT_OPTION(psvr2_optical_threshold, "PSSENSE_OPTICAL_THRESHOLD", 235.0f)

struct optical_candidate
{
	float x;
	float y;
	float score;
};

struct optical_track
{
	bool valid;
	int64_t timestamp_ns;
	struct xrt_vec3 position;
};

struct optical_state
{
	pthread_mutex_t mutex;
	struct xrt_pose hmd_pose;
	int64_t hmd_timestamp_ns;
	struct optical_track tracks[OPTICAL_MAX_TRACKS];
};

static struct optical_state state = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.hmd_pose = XRT_POSE_IDENTITY,
};

static int
find_candidates(const uint8_t *image,
                uint32_t width,
                uint32_t height,
                uint32_t stride,
                float threshold,
                struct optical_candidate candidates[OPTICAL_MAX_CANDIDATES])
{
	int count = 0;
	for (uint32_t y = 2; y + 2 < height; y += 2) {
		for (uint32_t x = 2; x + 2 < width; x += 2) {
			float value = image[y * stride + x];
			if (value < threshold || value < image[y * stride + x - 2] ||
			    value < image[y * stride + x + 2] || value < image[(y - 2) * stride + x] ||
			    value < image[(y + 2) * stride + x]) {
				continue;
			}

			float weighted_x = 0.0f;
			float weighted_y = 0.0f;
			float weight_sum = 0.0f;
			for (int dy = -2; dy <= 2; dy++) {
				for (int dx = -2; dx <= 2; dx++) {
					float weight = image[(y + dy) * stride + x + dx];
					weighted_x += (float)(x + dx) * weight;
					weighted_y += (float)(y + dy) * weight;
					weight_sum += weight;
				}
			}

			struct optical_candidate candidate = {
			    .x = weighted_x / weight_sum,
			    .y = weighted_y / weight_sum,
			    .score = value,
			};

			int insert = count;
			while (insert > 0 && candidates[insert - 1].score < candidate.score) {
				if (insert < OPTICAL_MAX_CANDIDATES) {
					candidates[insert] = candidates[insert - 1];
				}
				insert--;
			}
			if (insert < OPTICAL_MAX_CANDIDATES) {
				candidates[insert] = candidate;
				if (count < OPTICAL_MAX_CANDIDATES) {
					count++;
				}
			}
		}
	}

	return count;
}

static bool
is_far_enough(const struct optical_candidate *candidate,
              const struct optical_candidate candidates[OPTICAL_MAX_CANDIDATES],
              int count)
{
	for (int i = 0; i < count; i++) {
		float dx = candidate->x - candidates[i].x;
		float dy = candidate->y - candidates[i].y;
		if (dx * dx + dy * dy < 144.0f) {
			return false;
		}
	}
	return true;
}

static void
sort_tracks(struct optical_track tracks[OPTICAL_MAX_TRACKS])
{
	if (tracks[0].valid && tracks[1].valid && tracks[0].position.x > tracks[1].position.x) {
		struct optical_track temp = tracks[0];
		tracks[0] = tracks[1];
		tracks[1] = temp;
	}
}

bool
u_psvr2_optical_is_enabled(void)
{
	return debug_get_bool_option_psvr2_optical_tracking();
}

void
u_psvr2_optical_publish_hmd_pose(const struct xrt_pose *pose, int64_t timestamp_ns)
{
	if (!u_psvr2_optical_is_enabled()) {
		return;
	}

	pthread_mutex_lock(&state.mutex);
	state.hmd_pose = *pose;
	state.hmd_timestamp_ns = timestamp_ns;
	pthread_mutex_unlock(&state.mutex);
}

void
u_psvr2_optical_process_frame(const uint8_t *data,
                              uint32_t width,
                              uint32_t height,
                              uint32_t stride,
                              int64_t timestamp_ns)
{
	if (!u_psvr2_optical_is_enabled() || data == NULL || width < 4 || height < 4 || stride < width ||
	    width % 2 != 0) {
		return;
	}

	const uint32_t view_width = width / 2;
	const float focal = debug_get_float_option_psvr2_optical_focal();
	const float baseline = debug_get_float_option_psvr2_optical_baseline();
	const float threshold = debug_get_float_option_psvr2_optical_threshold();
	if (focal <= 0.0f || baseline <= 0.0f) {
		return;
	}

	struct optical_candidate left[OPTICAL_MAX_CANDIDATES] = {0};
	struct optical_candidate right[OPTICAL_MAX_CANDIDATES] = {0};
	int left_count = find_candidates(data, view_width, height, stride, threshold, left);
	int right_count = find_candidates(data + view_width, view_width, height, stride, threshold, right);
	if (left_count == 0 || right_count == 0) {
		return;
	}

	struct optical_track tracks[OPTICAL_MAX_TRACKS] = {0};
	for (int li = 0; li < left_count && tracks[1].valid == false; li++) {
		if (!is_far_enough(&left[li], left, li)) {
			continue;
		}

		int best_ri = -1;
		float best_error = 30.0f;
		for (int ri = 0; ri < right_count; ri++) {
			float y_error = fabsf(left[li].y - right[ri].y);
			float disparity = left[li].x - right[ri].x;
			if (y_error >= 30.0f || disparity <= 3.0f || !is_far_enough(&right[ri], right, ri)) {
				continue;
			}
			if (y_error < best_error) {
				best_error = y_error;
				best_ri = ri;
			}
		}
		if (best_ri < 0) {
			continue;
		}

		float disparity = left[li].x - right[best_ri].x;
		float z = focal * baseline / disparity;
		if (z < 0.15f || z > 5.0f) {
			continue;
		}

		float cx = view_width * 0.5f;
		float cy = height * 0.5f;
		struct xrt_vec3 camera_position = {
		    .x = (left[li].x - cx) * z / focal,
		    .y = -(left[li].y - cy) * z / focal,
		    .z = -z,
		};

		struct xrt_pose hmd_pose;
		int64_t hmd_timestamp_ns;
		pthread_mutex_lock(&state.mutex);
		hmd_pose = state.hmd_pose;
		hmd_timestamp_ns = state.hmd_timestamp_ns;
		pthread_mutex_unlock(&state.mutex);
		if (hmd_timestamp_ns == 0 || timestamp_ns - hmd_timestamp_ns > OPTICAL_MAX_AGE_NS) {
			continue;
		}

		struct xrt_vec3 world_position;
		math_quat_rotate_vec3(&hmd_pose.orientation, &camera_position, &world_position);
		math_vec3_accum(&hmd_pose.position, &world_position);

		tracks[0 + (tracks[0].valid ? 1 : 0)] = (struct optical_track){
		    .valid = true,
		    .timestamp_ns = timestamp_ns,
		    .position = world_position,
		};
	}

	sort_tracks(tracks);

	pthread_mutex_lock(&state.mutex);
	for (int i = 0; i < OPTICAL_MAX_TRACKS; i++) {
		state.tracks[i] = tracks[i];
	}
	pthread_mutex_unlock(&state.mutex);
}

bool
u_psvr2_optical_get_position(bool right_hand, int64_t timestamp_ns, struct xrt_vec3 *out_position)
{
	if (!u_psvr2_optical_is_enabled() || out_position == NULL) {
		return false;
	}

	pthread_mutex_lock(&state.mutex);
	struct optical_track track = state.tracks[right_hand ? 1 : 0];
	pthread_mutex_unlock(&state.mutex);

	if (!track.valid || timestamp_ns - track.timestamp_ns > OPTICAL_MAX_AGE_NS) {
		return false;
	}

	*out_position = track.position;
	return true;
}
