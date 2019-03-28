#include <algorithm>
#include <ctime>

#include <castus4-public/schedule.h>
#include <castus4-public/schedule_object.h>

#include "utils.h"
#include "utils_schedule.h"

using namespace std;

int main() {
	Castus4publicSchedule schedule;
    load(schedule);

    // Step 1: Get the current time
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);

    // How far into the schedule we are (predeclared due to initialization within a conditional)
    Castus4publicSchedule::ideal_time_t sched_offset;

    switch (schedule.schedule_type) {
        case C4_SCHED_TYPE_DAILY:
            struct tm cal;
            localtime_r(&current_time.tv_sec, &cal);
            // Clear the date components because we only care about the time side
            cal.tm_year = cal.tm_mon = cal.tm_mday = 0;
            // Convert back to seconds, and make it compatible with CASTUS' time format
            sched_offset = (Castus4publicSchedule::ideal_time_t)mktime(&cal) * Castus4publicSchedule::ideal_microsec_per_sec;
            break;
        default:
            // Only daily schedules are supported currently
            return 1;
    }

    // Step 2: Find the first block whose start..end range contains that time
    auto result = find_if(schedule.schedule_blocks.begin(), schedule.schedule_blocks.end(), [sched_offset](Castus4publicSchedule::ScheduleBlock& block) {
        return (block.getStartTime() <= sched_offset && block.getEndTime() >= sched_offset);
    });

    // If no matching blocks exist, our work is done; however, since the user
    // triggered us, this is likely an error. Note that errors are not reported:
    // returning an error code simply causes the schedule to not reload.
    if (result == schedule.schedule_blocks.end()) {
        return 2;
    }

    // Bind the block to a reference to tidy up the remaining code
    auto& block = *result;

    // Step 3: Calculate how far into the block we are
    auto time_offset = sched_offset - block.getStartTime();

    // Step 4.1: Move the targeted items down by the difference between the block's start and the current time
    for (auto& item : schedule.schedule_items) {
        if (is_valid(item) && in_block(item, block) &&
            // All well-formed triggered items will start at the top of the block
            item.getStartTime() == block.getStartTime() &&
            // All well-formed triggered items will have zero duration
            item.getStartTime() == item.getEndTime()) {

            item.setStartTime(item.getStartTime() + time_offset);
            item.setEndTime(  item.getEndTime()   + time_offset);
        }
    }

    // Step 4.2: Correct the targeted items' durations
    for (auto& item : schedule.schedule_items) {
        if (is_valid(item) && in_block(item, block)) {
            // Update the duration information
            update_timing(item);
        }
    }

    // Step 4.3: Ripple the items
    // Skip an item at the start (it will be manually visited)
    for (auto schedule_item = ++schedule.schedule_items.begin();
              schedule_item !=  schedule.schedule_items.end();
              ++schedule_item) {
        // Stash the current iterator
        auto tmp = schedule_item;
        // Bind the "next item", walking the stashed iterator back a step
        auto& next = *(tmp--);
        // Bind the current item
        auto& current = *tmp;

        if (is_valid(current) && in_block(current, block) &&
            is_valid(next)    && in_block(next,    block)) {

            // Shift the `next` item down to no longer overlap with `current`
            ripple_one(current, next);

            // Step 4.4: Truncate anything that has been pushed past the end of the block
            // If the item is now entirely outside the block
            if (next.getStartTime() > block.getEndTime()) {
                // Remove the item from the schedule entirely
                // NOTE: This avoids iterator invalidation due to this being a linked list,
                //       and so is safe to perform.
                schedule.schedule_items.erase(++tmp);
            } else if (next.getEndTime() > block.getEndTime()) {
                // Cut the item's end point short
                next.setEndTime(block.getEndTime());
            }
        }
    }

    write(schedule);

	return 0;
}
