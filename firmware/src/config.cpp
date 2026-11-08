#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace monitor {
namespace {

bool parseU32(const char *text, uint32_t *out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char *end = nullptr;
    const unsigned long v = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

bool keyEquals(const char *a, const char *b) {
    return std::strcmp(a, b) == 0;
}

}  // namespace

uint8_t osrField(uint8_t osr) {
    switch (osr) {
        case 1:  return 1;
        case 2:  return 2;
        case 4:  return 3;
        case 8:  return 4;
        case 16: return 5;
        default: return 0;
    }
}

ParseResult applySetting(Config *cfg, const char *key, const char *value,
                         const char **message) {
    // Work on a copy so a rejected setting never leaves a half-applied state.
    Config next = *cfg;

    // mode and json are the only string-valued keys, so handle them before the
    // numeric parse.
    if (keyEquals(key, "mode")) {
        if (keyEquals(value, "normal")) {
            next.forced = false;
        } else if (keyEquals(value, "forced")) {
            next.forced = true;
        } else {
            *message = "mode must be normal or forced";
            return ParseResult::BadValue;
        }
        *cfg = next;
        *message = nullptr;
        return ParseResult::Ok;
    }
    if (keyEquals(key, "json")) {
        if (keyEquals(value, "true") || keyEquals(value, "1")) {
            next.json = true;
        } else if (keyEquals(value, "false") || keyEquals(value, "0")) {
            next.json = false;
        } else {
            *message = "json must be true or false";
            return ParseResult::BadValue;
        }
        *cfg = next;
        *message = nullptr;
        return ParseResult::Ok;
    }

    uint32_t number = 0;
    if (!parseU32(value, &number)) {
        *message = "value must be a non-negative integer";
        return ParseResult::BadValue;
    }

    if (keyEquals(key, "interval") || keyEquals(key, "interval_ms")) {
        // 100 ms is the practical floor: the sensor conversion plus I2C
        // transfers need room, and the watchdog budget assumes samples land
        // well inside it.
        if (number < 100 || number > 3600000) {
            *message = "interval must be 100..3600000 ms";
            return ParseResult::BadValue;
        }
        next.interval_ms = number;
    } else if (keyEquals(key, "decimals")) {
        if (number > 6) {
            *message = "decimals must be 0..6";
            return ParseResult::BadValue;
        }
        next.decimals = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "average")) {
        if (number < 1 || number > 64) {
            *message = "average must be 1..64";
            return ParseResult::BadValue;
        }
        next.average = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "osr.t") || keyEquals(key, "osr_temperature")) {
        if (number > 16 || osrField(static_cast<uint8_t>(number)) == 0) {
            *message = "osr must be 1, 2, 4, 8 or 16";
            return ParseResult::BadValue;
        }
        next.osr_t = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "osr.p") || keyEquals(key, "osr_pressure")) {
        if (number > 16 || osrField(static_cast<uint8_t>(number)) == 0) {
            *message = "osr must be 1, 2, 4, 8 or 16";
            return ParseResult::BadValue;
        }
        next.osr_p = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "osr.h") || keyEquals(key, "osr_humidity")) {
        if (number > 16 || osrField(static_cast<uint8_t>(number)) == 0) {
            *message = "osr must be 1, 2, 4, 8 or 16";
            return ParseResult::BadValue;
        }
        next.osr_h = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "filter")) {
        if (number > 4) {
            *message = "filter must be 0..4 (0=off,1=x2,2=x4,3=x8,4=x16)";
            return ParseResult::BadValue;
        }
        next.filter = static_cast<uint8_t>(number);
    } else if (keyEquals(key, "mode")) {
        if (keyEquals(value, "normal")) {
            next.forced = false;
        } else if (keyEquals(value, "forced")) {
            next.forced = true;
        } else {
            *message = "mode must be normal or forced";
            return ParseResult::BadValue;
        }
    } else {
        *message = "unknown setting";
        return ParseResult::UnknownKey;
    }

    *cfg = next;
    *message = nullptr;
    return ParseResult::Ok;
}

const char *describe(const Config &cfg, char *buf, size_t size) {
    std::snprintf(buf, size,
                  "interval=%u decimals=%u average=%u osr.t=%u osr.p=%u "
                  "osr.h=%u filter=%u mode=%s json=%s",
                  cfg.interval_ms, cfg.decimals, cfg.average, cfg.osr_t,
                  cfg.osr_p, cfg.osr_h, cfg.filter,
                  cfg.forced ? "forced" : "normal", cfg.json ? "true" : "false");
    return buf;
}

}  // namespace monitor
