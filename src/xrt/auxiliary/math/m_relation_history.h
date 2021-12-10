// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Small utility for keeping track of the history of an xrt_space_relation, ie. for knowing where a HMD or
 * controller was in the past
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */
#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque type for storing the history of a space relation in a ring buffer
 *
 * @ingroup aux_util
 */
struct m_relation_history;

/**
 * @brief Describes how the resulting space relation for the desired time stamp was generated.
 *
 * @relates m_relation_history
 */
enum m_relation_history_result
{
	M_RELATION_HISTORY_RESULT_INVALID = 0,       //!< The supplied timestamp was invalid (0) or buffer was empty
	M_RELATION_HISTORY_RESULT_EXACT,             //!< The exact desired timestamp was found
	M_RELATION_HISTORY_RESULT_INTERPOLATED,      //!< The desired timestamp was between two entries
	M_RELATION_HISTORY_RESULT_PREDICTED,         //!< The desired timestamp was newer than the most recent entry
	M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED, //!< The desired timestamp was older than the oldest entry
};

/*!
 * @brief Creates an opaque relation_history object.
 *
 * @public @memberof m_relation_history
 */
void
m_relation_history_create(struct m_relation_history **rh);

/*!
 * Pushes a new pose to the history.
 *
 * If the history is full, it will also pop a pose out of the other side of the buffer.
 *
 * @return false if the timestamp is earlier than the most recent timestamp already recorded
 *
 * @public @memberof m_relation_history
 */
bool
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation const *in_relation, uint64_t ts);

/*!
 * @brief Interpolates or extrapolates to the desired timestamp.
 *
 * Read-only operation - doesn't remove anything from the buffer or anything like that - you can call this as often as
 * you want.
 *
 * @public @memberof m_relation_history
 */
enum m_relation_history_result
m_relation_history_get(struct m_relation_history *rh, uint64_t at_time_ns, struct xrt_space_relation *out_relation);

/*!
 * @brief Returns the number of items in the history.
 *
 * @public @memberof m_relation_history
 */
uint32_t
m_relation_history_get_size(const struct m_relation_history *rh);

/*!
 * Destroys an opaque relation_history object.
 *
 * @public @memberof m_relation_history
 */
void
m_relation_history_destroy(struct m_relation_history **rh);

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
namespace xrt::auxiliary::math {

/*!
 * C++ interface for @ref m_relation_history, non-copyable/deletable.
 *
 * @ingroup aux_math
 */
class RelationHistory
{
public:
	/*!
	 * @copydoc m_relation_history_result
	 */
	typedef m_relation_history_result Result;


private:
	m_relation_history *mPtr{nullptr};


public:
	// clang-format off
	RelationHistory() noexcept { m_relation_history_create(&mPtr); }
	~RelationHistory() { m_relation_history_destroy(&mPtr); }
	// clang-format on

	// Special non-copyable reference.
	RelationHistory(RelationHistory const &) = delete;
	RelationHistory(RelationHistory &&) = delete;
	RelationHistory &
	operator=(RelationHistory const &) = delete;
	RelationHistory &
	operator=(RelationHistory &&) = delete;


	/*!
	 * @copydoc m_relation_history_push
	 */
	bool
	push(xrt_space_relation const &relation, uint64_t ts) noexcept
	{
		return m_relation_history_push(mPtr, &relation, ts);
	}

	/*!
	 * @copydoc m_relation_history_get
	 */
	Result
	get(uint64_t at_time_ns, xrt_space_relation *out_relation) noexcept
	{
		return m_relation_history_get(mPtr, at_time_ns, out_relation);
	}
};

} // namespace xrt::auxiliary::math
#endif
