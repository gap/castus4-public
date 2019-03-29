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

using ideal_time_t = Castus4publicSchedule::ideal_time_t;

fs::path create_daily_schedule(fs::path day_dir) {
    const vector<string> hours{
        "12am", "1am", "2am", "3am",  "4am",  "5am",
         "6am", "7am", "8am", "9am", "10am", "11am",
        "12pm", "1pm", "2pm", "3pm",  "4pm",  "5pm",
         "6pm", "7pm", "8pm", "9pm", "10pm", "11pm"
    };
    Sched schedule;
    schedule.schedule_type = C4_SCHED_TYPE_DAILY;

    // Step 1: Create the day's directory
    fs::create_directories(day_dir);

    // Step 2: Create schedule blocks for each hour
    for (auto& block_name : hours) {
        Sched::ScheduleBlock block{C4_SCHED_TYPE_DAILY};

        block.setBlockName(block_name);

        ideal_time_t last_block = schedule.schedule_blocks.empty() ? 0 : schedule.schedule_blocks.back().getEndTime();
        block.setStartTime(last_block);
        block.setEndTime(last_block + Sched::ideal_hour);

        block.setValue("trigger", "charter_event");
        schedule.schedule_blocks.push_back(block);

        // Step 2.1: Create directory for each block
        fs::create_directories(day_dir / block_name);
    }

    // Step 3: Add the event trigger information
    schedule.schedule_triggers.insert(pair<string, string>(
        "charter_event",
        "/usr/libexec/castus/schedule-filters/filter-update-event"
    ));

    // Step 4: Save the schedule
    ofstream schedule_file{(day_dir / "Schedule").string()};

    schedule.sort_schedule_items();
    schedule.sort_schedule_blocks();
    if (!schedule.write_out(schedule_file)) {
        cerr << "Error while writing schedule " << (day_dir / "Schedule") << endl;
    }

    return day_dir / "Schedule";
}

int main(int argc, char** argv) {
	Sched schedule;

    load(schedule);

    // The base directory for the daily schedules (and files) of this yearly schedule
    fs::path base;
    bool base_initialized = false;
    for (int arg_idx = 1; arg_idx < argc; ++arg_idx) {
        // The path to the schedule is given as `--file <path>`
        if (string(argv[arg_idx]) == "--file" && arg_idx + 1 < argc) {
            // Capture the path
            base = fs::path(argv[arg_idx + 1]);
            // Take the filename
            string schedule_name = base.leaf().string();
            // And replace it with the added suffix " Days"
            base.remove_leaf() /= schedule_name + " Days";
            base_initialized = true;
        }
    }
    if (!base_initialized) {
        return 1;
    }

    // Step 1: Get the current time
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);

    // How far into the schedule we are (predeclared due to initialization within a conditional)
    ideal_time_t sched_offset;

    // Split-out calendar time
    struct tm cal;
    localtime_r(&current_time.tv_sec, &cal);

    switch (schedule.schedule_type) {
        case C4_SCHED_TYPE_YEARLY:
            // Convert back to seconds, and make it compatible with CASTUS' time format
            sched_offset =
                    (ideal_time_t)cal.tm_mon  * Sched::ideal_month +
                    (ideal_time_t)cal.tm_mday * Sched::ideal_day +
                    (ideal_time_t)cal.tm_hour * Sched::ideal_hour +
                    (ideal_time_t)cal.tm_min  * Sched::ideal_minute;
            break;
        default:
            // Only daily schedules are supported currently
            return 1;
    }

    // Set up our own interval timer on first run
    if (schedule.schedule_intervals.empty()) {
        schedule.schedule_intervals.insert(pair<string, string>(
            "P1W",
            "/usr/libexec/castus/schedule-filters/filter-update-yearly-triggers"
        ));
    }

    // Populate the schedule on first run
    if (schedule.schedule_items.empty()) {
        for (int month = 0; month < 12; ++month) {
            for (int day = 0; day < 31; ++day) {
                // Calculate the CASTUS start time of the day
                ideal_time_t start_time =
                    (ideal_time_t)month * Sched::ideal_month +
                    (ideal_time_t)day   * Sched::ideal_day;

                // If the day is more than a week ago, generate for next year
                int year = cal.tm_year + 1900;
                if (start_time + Sched::ideal_week <= sched_offset) {
                    year += 1;
                }

                // Create a daily schedule for the appropriate day
                auto day_dir = base / to_string(year) / to_string(month+1) / to_string(day+1);
                auto daily_schedule = create_daily_schedule(day_dir);

                // Slot it into the schedule at the appropriate point
                Sched::ScheduleItem day_item{C4_SCHED_TYPE_YEARLY};
                day_item.setItem(daily_schedule.string());
                day_item.setValue("Infinite Year", to_string(year));
                day_item.setStartTime(start_time);
                day_item.setEndTime(start_time + Sched::ideal_day);
                schedule.schedule_items.push_back(day_item);
            }
        }
    }

    for (auto& day_item : schedule.schedule_items) {
        // If the item is from more than a week ago
        if (day_item.getStartTime() + Sched::ideal_week <= sched_offset) {
            // And hasn't yet been re-upped to point to next year
            if (cal.tm_year + 1900 >= stoi(day_item.getValue("Infinite Year"))) {
                ideal_time_t start = day_item.getStartTime();
                start /= Sched::ideal_day;

                // Figure out its long-form date (in the next year)
                int day = (int)(start % (Sched::ideal_month / Sched::ideal_day));
                start /= Sched::ideal_day_per_month;
                int month = (int)(start % (Sched::ideal_year / Sched::ideal_month));
                start /= Sched::ideal_month_per_year;
                int year = (int)(cal.tm_year + 1900 + 1);

                // Create a daily schedule file for it
                auto day_dir = base / to_string(year) / to_string(month+1) / to_string(day+1);
                auto daily_schedule = create_daily_schedule(day_dir);

                // And update the path and year marker
                day_item.setItem(daily_schedule.string());
                day_item.setValue("Infinite Year", to_string(year));
            }
        }
    }

    write(schedule);
}
