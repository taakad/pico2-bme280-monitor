#include "command.hpp"

#include <cctype>
#include <cstring>

namespace monitor {
namespace {

// Lowercases in place so the protocol is case-insensitive, and trims the
// trailing CR/LF that every terminal adds.
void normalize(char *line) {
    char *end = line + std::strlen(line);
    while (end != line) {
        const char c = end[-1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            *--end = '\0';
        } else {
            break;
        }
    }
    for (char *p = line; *p; ++p) {
        *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
}

}  // namespace

bool feedChar(char c, char *line, unsigned int *length, unsigned int capacity) {
    if (c == '\n') {
        if (*length == 0) {
            return false;  // bare newline: nothing to parse
        }
        line[*length] = '\0';
        *length = 0;
        return true;
    }
    if (c == '\r') {
        return false;
    }
    // +1 reserves the NUL terminator.
    if (*length + 1 >= capacity) {
        // Over-long line: discard it entirely instead of acting on a prefix.
        *length = 0;
        line[0] = '\0';
        return false;
    }
    line[(*length)++] = c;
    return false;
}

Command parseLine(char *line, const char **key, const char **value) {
    *key = nullptr;
    *value = nullptr;
    normalize(line);
    if (line[0] == '\0') {
        return Command::None;
    }

    if (std::strcmp(line, "get") == 0) {
        return Command::Get;
    }
    if (std::strcmp(line, "help") == 0) {
        return Command::Help;
    }
    if (std::strcmp(line, "ping") == 0) {
        return Command::Ping;
    }

    if (std::strncmp(line, "set ", 4) != 0) {
        return Command::Bad;
    }
    char *assignment = std::strchr(line + 4, '=');
    if (assignment == nullptr) {
        return Command::Bad;
    }
    *assignment = '\0';
    *key = line + 4;
    *value = assignment + 1;
    if (**key == '\0' || **value == '\0') {
        return Command::Bad;
    }
    return Command::Set;
}

}  // namespace monitor
