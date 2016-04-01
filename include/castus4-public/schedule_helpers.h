
#ifndef Castus4publicScheduleHelpers_h
#define Castus4publicScheduleHelpers_h

#include <castus4-public/schedule_object.h>

namespace Castus4publicScheduleHelpers {
    bool load(Castus4publicSchedule &schedule, std::string file);
    bool load_from_string(class Castus4publicSchedule &schedule, std::string data);
}

#endif
