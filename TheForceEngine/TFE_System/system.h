#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine System Library
// System functionality, such as timers and logging.
//////////////////////////////////////////////////////////////////////

#include "types.h"

#define TFE_MAJOR_VERSION 0
#define TFE_MINOR_VERSION 2
#define TFE_BUILD_VERSION 1

enum LogWriteType
{
	LOG_MSG = 0,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL,		//Critical log messages are CRITICAL errors that also act as asserts when attached to a debugger.
	LOG_COUNT
};

namespace TFE_System
{
	void init(f32 refreshRate, bool synced, const char* versionString);
	void shutdown();
	void resetStartTime();
	void setVsync(bool sync);
	bool getVSync();

	void update();
	f64 updateThreadLocal(u64* localTime);

	// Timing
	// --- The current time and delta time are determined once per frame, during the update() function.
	//     In other words an entire frame operates on a single instance of time.
	// Return the delta time.
	f64 getDeltaTime();
	// Get the absolute time since the last start time.
	f64 getTime();

	u64 getCurrentTimeInTicks();
	f64 convertFromTicksToSeconds(u64 ticks);
	f64 microsecondsToSeconds(f64 mu);

	void getDateTimeString(char* output);

	// Log
	bool logOpen(const char* filename);
	void logClose();
	void logWrite(LogWriteType type, const char* tag, const char* str, ...);

	// Lighter weight debug output (only useful when running in a terminal or debugger).
	void debugWrite(const char* tag, const char* str, ...);

	// System
	bool osShellExecute(const char* pathToExe, const char* exeDir, const char* param, bool waitForCompletion);
	void sleep(u32 sleepDeltaMS);

	void postQuitMessage();
	bool quitMessagePosted();

	void postSystemUiRequest();
	bool systemUiRequestPosted();

	const char* getVersionString();

	extern f64 c_gameTimeScale;
}

// _strlwr() / _strupr() do not exist on Linux
#ifndef _strlwr
#include <ctype.h>
static inline void _strlwr(char *c)
{
	while (*c) {
		*c = tolower(*c);
		c++;
	}
}
#endif

#ifndef _strupr
#include <ctype.h>
static inline void _strupr(char *c)
{
	while (*c) {
		*c = toupper(*c);
		c++;
	}
}
#endif

// strcpy_s is windows-ism
#ifndef strcpy_s
#define strcpy_s(dest, len, src) strncpy(dest, src, len)
#endif

// sprintf_s is a windows-ism
#ifndef sprintf_s
#define sprintf_s snprintf
#endif
