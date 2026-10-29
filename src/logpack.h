// Rec Cue! — ログを取得: the recent logs, the record of each recording and the settings in one ZIP, to hand over as it
// is (attached to a mail or a chat). The files are stored, not compressed: Windows has no ZIP writer to call, and the
// logs of a week are a few MB.
#pragma once

#include "common.h"

namespace rc {

constexpr int kLogDays = 7;   // the logs and records of the last week

struct LogPack {
    int files = 0;          // put in the ZIP (about.json included)
    int skipped = 0;        // left out: unreadable, or past the size limits
    uint64_t bytes = 0;     // the ZIP's size
    double ms = 0;
    std::string error;      // empty when the ZIP was written
};

// Writes `zip` (through a .tmp beside it): logs\rec-cue-*.log and sessions\*.json changed in the last `days` days,
// config.ini, and `about` as about.json. The log of today is read as far as it is written when it is opened.
bool packLogs(const std::wstring& zip, int days, const std::string& about, LogPack* out);
std::wstring downloadsDir();   // the user's Downloads (Rec Cue!'s own folder when there is none)

}  // namespace rc
