#include "db/messages.h"
#include "db/replay.h"
#include "db/tickets.h"
#include "db_fixture.h"
#include "dc/sender.h"
#include "feedback/service.h"
#include "feedback/validator.h"
#include "pow/challenge.h"
#include "pow/sha256.h"

#include <gtest/gtest.h>

#include <chrono>

using namespace deltafeedback;

namespace {

// In-memory IMessageSender that records each outgoing notification and
// returns a synthetic dc_msg_id sequence so the service can update the row.
class FakeSender : public dc::IMessageSender {
public:
    std::optional<std::uint32_t> send_to_owner(std::string_view text,
                                               std::uint32_t quote_msg_id = 0) override {
        if (!has_owner_) return std::nullopt;
        sent.emplace_back(std::string(text));
        quoted.push_back(quote_msg_id);
        return ++next_id_;
    }
    std::optional<std::uint32_t> owner_contact_id() const override {
        return has_owner_ ? std::optional<std::uint32_t>{42u} : std::nullopt;
    }
    void set_has_owner(bool v) { has_owner_ = v; }

    std::vector<std::string>   sent;
    std::vector<std::uint32_t> quoted;
private:
    bool          has_owner_ = true;
    std::uint32_t next_id_   = 100;
};

// Brute-forces a valid POW nonce for the given token (low difficulty in tests).
std::string find_nonce(const std::string& token, unsigned bits) {
    for (std::uint64_t n = 0; n < (1ULL << 32); ++n) {
        std::string ns = std::to_string(n);
        if (pow::leading_zero_bits(pow::sha256(token + ":" + ns)) >= bits) return ns;
    }
    return {};
}

class ServiceTest : public test_support::DbTest {
protected:
    void SetUp() override {
        DbTest::SetUp();
        validator   = std::make_unique<feedback::Validator>(feedback::Limits{});
        issuer      = std::make_unique<pow::ChallengeIssuer>("test-secret", 6, std::chrono::seconds(60));
        tickets     = std::make_unique<db::TicketsRepo>(sql());
        messages    = std::make_unique<db::MessagesRepo>(sql());
        replay      = std::make_unique<db::ReplayStore>(sql());
        service     = std::make_unique<feedback::Service>(
            *issuer, *validator, sender, *tickets, *messages, *replay);
    }

    feedback::Service::SubmitInput good_input() {
        auto c = issuer->issue(std::string("0123456789abcdef", 16));
        feedback::Service::SubmitInput in;
        in.pow_token = c.token;
        in.pow_nonce = find_nonce(c.token, c.difficulty_bits);
        in.name = "Иван";
        in.message = "Здравствуйте";
        in.locale = "ru";
        in.fill_time_ms = 5000;
        in.visitor_ip = "192.168.1.10";
        return in;
    }

    FakeSender sender;
    std::unique_ptr<feedback::Validator>     validator;
    std::unique_ptr<pow::ChallengeIssuer>    issuer;
    std::unique_ptr<db::TicketsRepo>         tickets;
    std::unique_ptr<db::MessagesRepo>        messages;
    std::unique_ptr<db::ReplayStore>         replay;
    std::unique_ptr<feedback::Service>       service;
};

}  // namespace

TEST_F(ServiceTest, HappyPath) {
    auto in = good_input();
    auto r = service->submit_new(in);
    EXPECT_EQ(r.error, feedback::SubmitError::Ok);
    EXPECT_EQ(r.ticket_id.size(), 6u);
    EXPECT_EQ(r.read_token.size(), 64u);  // hex of 32 bytes
    ASSERT_EQ(sender.sent.size(), 1u);
    // Notification starts with [ID]
    EXPECT_EQ(sender.sent[0].substr(0, 1), "[");
    EXPECT_EQ(sender.sent[0].substr(1, 6), r.ticket_id);
}

TEST_F(ServiceTest, NoOwner_ReturnsServiceUnavailable) {
    sender.set_has_owner(false);
    auto r = service->submit_new(good_input());
    EXPECT_EQ(r.error, feedback::SubmitError::NoOwnerConfigured);
    EXPECT_TRUE(sender.sent.empty());
    EXPECT_TRUE(tickets->list_open().empty());
}

TEST_F(ServiceTest, HoneypotTrips) {
    auto in = good_input();
    in.honeypot = "spam";
    EXPECT_EQ(service->submit_new(in).error, feedback::SubmitError::HoneypotTripped);
}

TEST_F(ServiceTest, FilledTooFast) {
    auto in = good_input();
    in.fill_time_ms = 100;
    EXPECT_EQ(service->submit_new(in).error, feedback::SubmitError::FilledTooFast);
}

TEST_F(ServiceTest, PowReplayedRejected) {
    auto in = good_input();
    EXPECT_EQ(service->submit_new(in).error, feedback::SubmitError::Ok);
    // Same challenge token + nonce again → replay.
    EXPECT_EQ(service->submit_new(in).error, feedback::SubmitError::PowReplayed);
}

TEST_F(ServiceTest, BadPowSignature) {
    auto in = good_input();
    in.pow_token.back() = (in.pow_token.back() == 'A' ? 'B' : 'A');
    EXPECT_EQ(service->submit_new(in).error, feedback::SubmitError::PowInvalid);
}

TEST_F(ServiceTest, ReplyRequiresAwaitingVisitor) {
    auto submitted = service->submit_new(good_input());
    ASSERT_EQ(submitted.error, feedback::SubmitError::Ok);

    // Initially status = AwaitingAdmin → reply rejected.
    auto c = issuer->issue(std::string("nonce-id-bytes!!", 16));
    feedback::Service::ReplyInput rep;
    rep.ticket_id  = submitted.ticket_id;
    rep.read_token = submitted.read_token;
    rep.pow_token  = c.token;
    rep.pow_nonce  = find_nonce(c.token, c.difficulty_bits);
    rep.body       = "follow up";
    rep.visitor_ip = "192.168.1.10";

    EXPECT_EQ(service->submit_reply(rep), feedback::Service::ReplyError::NotAwaitingVisitor);

    // Flip status as the bot would after admin's reply.
    tickets->set_status(submitted.ticket_id, db::TicketStatus::AwaitingVisitor);
    EXPECT_EQ(service->submit_reply(rep), feedback::Service::ReplyError::Ok);
    EXPECT_EQ(tickets->get(submitted.ticket_id)->status, db::TicketStatus::AwaitingAdmin);
}

TEST_F(ServiceTest, ReplyQuotesPreviousNotification) {
    auto submitted = service->submit_new(good_input());
    ASSERT_EQ(submitted.error, feedback::SubmitError::Ok);

    auto initial_dc_msg_id = sender.quoted.size() == 1 ? 101u : 0u;
    EXPECT_EQ(sender.quoted[0], 0u);  // initial submission has no quote

    tickets->set_status(submitted.ticket_id, db::TicketStatus::AwaitingVisitor);

    auto c = issuer->issue(std::string("uniqueidbytes123", 16));
    feedback::Service::ReplyInput rep;
    rep.ticket_id  = submitted.ticket_id;
    rep.read_token = submitted.read_token;
    rep.pow_token  = c.token;
    rep.pow_nonce  = find_nonce(c.token, c.difficulty_bits);
    rep.body       = "follow up";
    EXPECT_EQ(service->submit_reply(rep), feedback::Service::ReplyError::Ok);

    ASSERT_EQ(sender.quoted.size(), 2u);
    // Reply notification should quote the dc_msg_id of the initial notification.
    EXPECT_EQ(sender.quoted[1], initial_dc_msg_id);
}

TEST_F(ServiceTest, ReplyRejectsBadToken) {
    auto submitted = service->submit_new(good_input());
    tickets->set_status(submitted.ticket_id, db::TicketStatus::AwaitingVisitor);

    auto c = issuer->issue(std::string("aaaabbbbccccdddd", 16));
    feedback::Service::ReplyInput rep;
    rep.ticket_id  = submitted.ticket_id;
    rep.read_token = std::string(64, '0');  // wrong token
    rep.pow_token  = c.token;
    rep.pow_nonce  = find_nonce(c.token, c.difficulty_bits);
    rep.body       = "x";
    EXPECT_EQ(service->submit_reply(rep), feedback::Service::ReplyError::BadToken);
}

TEST_F(ServiceTest, ReplyRejectsClosed) {
    auto submitted = service->submit_new(good_input());
    tickets->close(submitted.ticket_id, 100);

    auto c = issuer->issue(std::string("11112222333344445", 16));
    feedback::Service::ReplyInput rep;
    rep.ticket_id  = submitted.ticket_id;
    rep.read_token = submitted.read_token;
    rep.pow_token  = c.token;
    rep.pow_nonce  = find_nonce(c.token, c.difficulty_bits);
    rep.body       = "anything";
    EXPECT_EQ(service->submit_reply(rep), feedback::Service::ReplyError::Closed);
}
