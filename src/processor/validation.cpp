#include "validation.h"
#include <ctime>

bool validate_event(const event::Event& evt) {
    if (evt.user_id().empty() || evt.user_id().size() > 256) return false;
    if (evt.event_type().empty() || evt.event_type().size() > 256) return false;
    if (evt.platform().size() > 64) return false;
    if (evt.payload().size() > 65536) return false;
    if (evt.ts() <= 0) return false;
    auto now = std::time(nullptr) * 1000;
    if (evt.ts() < now - 86400000LL * 30) return false;
    if (evt.ts() > now + 86400000LL * 1) return false;
    return true;
}
