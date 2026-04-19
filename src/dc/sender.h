#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deltafeedback::dc {

// Shared between the bot's DC event-loop thread (writer) and Crow worker
// threads (readers via DcSender). Atomic load/store is enough; we never need
// CAS because only the bot thread writes.
struct OwnerHandle {
    std::atomic<std::uint32_t> contact_id{0};   // 0 == no owner configured
};

// Abstraction so feedback / bot logic can be unit-tested with a fake.
//
// `send_to_owner` returns the DC message id of the outgoing notification, or
// std::nullopt if no owner is configured / DC failed to send. If
// `quote_msg_id` is non-zero AND that message exists in the DC db, the
// outgoing message will be sent as a DC reply to it (quote chain). This is
// how follow-up visitor messages get threaded under the original ticket
// notification in the admin's client.
class IMessageSender {
public:
    virtual ~IMessageSender() = default;
    virtual std::optional<std::uint32_t> send_to_owner(std::string_view text,
                                                       std::uint32_t quote_msg_id = 0) = 0;
    virtual std::optional<std::uint32_t> owner_contact_id() const = 0;
};

// Forward-declared opaque handle keeps this header free of <deltachat.h>.
// Implementation reinterpret_cast's to dc_context_t* in sender.cpp.
struct DcContextHandle;

class DcSender : public IMessageSender {
public:
    DcSender(DcContextHandle* ctx, OwnerHandle& owner);

    std::optional<std::uint32_t> send_to_owner(std::string_view text,
                                               std::uint32_t quote_msg_id = 0) override;
    std::optional<std::uint32_t> owner_contact_id() const override;

private:
    DcContextHandle* ctx_;
    OwnerHandle&     owner_;
};

}  // namespace deltafeedback::dc
