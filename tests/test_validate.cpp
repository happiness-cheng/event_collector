#include <gtest/gtest.h>
#include "validation.h"
#include <ctime>

event::Event make_valid_event() {
    event::Event evt;
    evt.set_event_id("evt_001");
    evt.set_user_id("user_42");
    evt.set_platform("web");
    evt.set_event_type("click");
    evt.set_ts(static_cast<int64_t>(std::time(nullptr)) * 1000);
    evt.set_payload(R"({"page": "/home"})");
    return evt;
}

auto now_ms = [] { return static_cast<int64_t>(std::time(nullptr)) * 1000; };

TEST(ValidateEvent, ValidEvent_ReturnsTrue) {
    auto evt = make_valid_event();
    EXPECT_TRUE(validate_event(evt));
}

TEST(ValidateEvent, EmptyUserId_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_user_id("");
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, EmptyEventType_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_event_type("");
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, OversizedPayload_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_payload(std::string(65537, 'x'));
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, PayloadExactly65536_ReturnsTrue) {
    auto evt = make_valid_event();
    evt.set_payload(std::string(65536, 'x'));
    EXPECT_TRUE(validate_event(evt));
}

TEST(ValidateEvent, TsZero_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(0);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, FutureTsBeyondOneDay_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(now_ms() + 2 * 86400000LL);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, PastTsBeyond30Days_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(now_ms() - 60 * 86400000LL);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, OversizedPlatform_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_platform(std::string(65, 'a'));
    EXPECT_FALSE(validate_event(evt));
}
