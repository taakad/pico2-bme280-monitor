#ifndef MONITOR_COMMAND_HPP
#define MONITOR_COMMAND_HPP

#include "config.hpp"

// Line-oriented serial protocol. Kept free of pico-sdk headers so it can be
// unit-tested on a host compiler.
//
//   set <key>=<value>   change one setting, replies "ok <key>=<value>"
//   get                 replies the whole config as "config ..."
//   help                lists the accepted keys
//   ping                replies "pong"
namespace monitor {

enum class Command {
    None,
    Set,
    Get,
    Help,
    Ping,
    Bad,
};

// Parses one NUL-terminated line (trailing CR/LF is ignored). On Command::Set,
// *key and *value point into line. Returns Command::None for an empty line.
Command parseLine(char *line, const char **key, const char **value);

// Appends one received character to line. Returns true once a full line is
// available (line is NUL-terminated and *length is reset). Lines longer than
// the buffer are dropped rather than truncated, so a noisy byte stream cannot
// produce a bogus command.
bool feedChar(char c, char *line, unsigned int *length, unsigned int capacity);

}  // namespace monitor

#endif  // MONITOR_COMMAND_HPP
