#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#if defined(_WIN32)
#include <BaseTsd.h>
#include <sys/stat.h>
using mode_t = unsigned int;
using uid_t = unsigned int;
using gid_t = unsigned int;
using ssize_t = SSIZE_T;
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif
