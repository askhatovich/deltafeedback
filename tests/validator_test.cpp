#include "feedback/validator.h"

#include <gtest/gtest.h>

using namespace deltafeedback::feedback;

namespace {
Validator make_v() { return Validator(Limits{}); }

SubmissionInput good() {
    return SubmissionInput{
        .name = "Иван",
        .message = "Здравствуйте, у меня вопрос.",
        .locale = "ru",
        .honeypot = "",
        .fill_time_ms = 5'000,
    };
}
}  // namespace

TEST(Utf8, CodepointCount) {
    EXPECT_EQ(utf8_codepoint_count(""), 0u);
    EXPECT_EQ(utf8_codepoint_count("abc"), 3u);
    EXPECT_EQ(utf8_codepoint_count("Привет"), 6u);  // 6 codepoints, 12 bytes
}

TEST(ControlChars, RejectsNul) {
    EXPECT_TRUE(has_disallowed_control_chars(std::string("a\0b", 3)));
    EXPECT_TRUE(has_disallowed_control_chars("\x1b[31m"));
    EXPECT_FALSE(has_disallowed_control_chars("plain text"));
    EXPECT_FALSE(has_disallowed_control_chars("with\nnewline\tand tab"));
}

TEST(Validator, Happy) {
    auto v = make_v();
    auto r = v.validate(good());
    EXPECT_TRUE(r.ok());
}

TEST(Validator, HoneypotTripped) {
    auto v = make_v();
    auto in = good();
    in.honeypot = "I am a bot";
    EXPECT_EQ(v.validate(in).honeypot, FieldError::HoneypotTripped);
}

TEST(Validator, FilledTooFast) {
    auto v = make_v();
    auto in = good();
    in.fill_time_ms = 200;
    EXPECT_EQ(v.validate(in).timing, FieldError::FilledTooFast);
}

TEST(Validator, MessageTooLong) {
    auto v = make_v();
    auto in = good();
    std::string long_msg(501, 'a');
    in.message = long_msg;
    EXPECT_EQ(v.validate(in).message, FieldError::TooLong);
}

TEST(Validator, MessageEmpty) {
    auto v = make_v();
    auto in = good();
    in.message = "";
    EXPECT_EQ(v.validate(in).message, FieldError::Empty);
}

TEST(Validator, BadLocale) {
    auto v = make_v();
    auto in = good();
    in.locale = "fr";
    EXPECT_EQ(v.validate(in).locale, FieldError::BadLocale);
}

TEST(Validator, NameOptional) {
    auto v = make_v();
    auto in = good();
    in.name = "";
    EXPECT_TRUE(v.validate(in).ok());
}

TEST(Validator, ReplyChecked) {
    auto v = make_v();
    EXPECT_EQ(v.validate_reply("ok"), FieldError::Ok);
    EXPECT_EQ(v.validate_reply(""),   FieldError::Empty);
    EXPECT_EQ(v.validate_reply(std::string(501, 'x')), FieldError::TooLong);
}
