#include <algorithm>
#include <ctime>

#include <boost/filesystem.hpp>

#include <castus4-public/schedule.h>
#include <castus4-public/schedule_object.h>

#include "utils.h"
#include "utils_schedule.h"

using namespace std;
namespace fs = boost::filesystem;

using Sched = Castus4publicSchedule;
using ideal_time_t = Sched::ideal_time_t;

int main(int argc, char** argv) {
    Sched schedule;
    load(schedule);

    // Step 1: Get the current time
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);

    // How far into the schedule we are (predeclared due to initialization within a conditional)
    ideal_time_t sched_offset;

    switch (schedule.schedule_type) {
        case C4_SCHED_TYPE_DAILY:
            struct tm cal;
            localtime_r(&current_time.tv_sec, &cal);
            // Clear the date components because we only care about the time side
            cal.tm_year = cal.tm_mon = cal.tm_mday = 0;
            // Convert back to seconds, and make it compatible with CASTUS' time format
            sched_offset =
                cal.tm_hour * Sched::ideal_hour +
                cal.tm_min  * Sched::ideal_minute;
            break;
        default:
            // Only daily schedules are supported currently
            return 1;
    }

    // Step 2: Find the first block whose start..end range contains that time
    auto result = find_if(schedule.schedule_blocks.begin(), schedule.schedule_blocks.end(), [sched_offset](Sched::ScheduleBlock& block) {
        return (
            block.getStartTime() <= sched_offset &&
            block.getEndTime()   >  sched_offset &&
            block.getValue("trigger") && string(block.getValue("trigger")) == "charter_event"
        );
    });

    // If no matching blocks exist, our work is done; however, since the user
    // triggered us, this is likely an error. Note that errors are not reported:
    // returning an error code simply causes the schedule to not reload.
    if (result == schedule.schedule_blocks.end()) {
        return 2;
    }

    // Bind the block to a reference to tidy up the remaining code
    auto& block = *result;

    // Step 3: Find the items that should play as part of this schedule
    // The base directory for the daily schedules (and files) of this yearly schedule
    fs::path base;
    bool base_initialized = false;
    for (int arg_idx = 1; arg_idx < argc; ++arg_idx) {
        // The path to the schedule is given as `--file <path>`
        if (string(argv[arg_idx]) == "--file" && arg_idx + 1 < argc) {
            // Capture the path
            base = fs::path(argv[arg_idx + 1]);
            // And replace the schedule with the directory the videos are in
            base.remove_leaf() /= block.getBlockName();
            base_initialized = true;
        }
    }
    if (!base_initialized) {
        return 3;
    }

    // Collect the files in the directory
    vector<fs::path> files;
    transform(
        fs::directory_iterator(base),
        fs::directory_iterator(),
        back_inserter(files),
        [](fs::directory_entry& entry){ return entry.path(); }
    );
    // Remove anything that isn't a non-hidden m2ts
    auto end = remove_if(files.begin(), files.end(), [](fs::path& path){
        if (path.extension() != ".m2ts") {
            return true;
        } else if (path.leaf().string().substr(0, 1) == ".") {
            return true;
        } else {
            return false;
        }
    });
    files.erase(end, files.end());
    // Sort them
    sort(files.begin(), files.end());

    // Give each one a trivial duration, so that they have a clear
    // ordering when Jonathan's code sorts them
    ideal_time_t bias = 0;
    // Step 4.1: Create items (at the appropriate time) for the event
    for (auto& path : files) {
        Sched::ScheduleItem item{C4_SCHED_TYPE_DAILY};
        item.setItem(path.string());
        item.setStartTime(sched_offset + bias);
        bias += Sched::ideal_microsecond;
        item.setEndTime(  sched_offset + bias);
        item.setValue("trigger", "charter_event");
        schedule.schedule_items.push_back(item);
    }

    // Step 4.2: Correct the targeted items' durations
    for (auto& item : schedule.schedule_items) {
        if (is_valid(item) && item.getValue("trigger") && string(item.getValue("trigger")) == "charter_event") {
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

        if (is_valid(current) && current.getValue("trigger") && string(current.getValue("trigger")) == "charter_event" &&
            is_valid(next)    &&    next.getValue("trigger") && string(   next.getValue("trigger")) == "charter_event") {

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

    // Clear out the trigger markers on old items
    for (auto& item : schedule.schedule_items) {
        item.deleteValue("trigger");
    }

    write(schedule);

    return 0;
}
