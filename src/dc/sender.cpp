#include "dc/sender.h"

#include <deltachat.h>

#include <string>

namespace deltafeedback::dc {

namespace {
dc_context_t* as_ctx(DcContextHandle* h) { return reinterpret_cast<dc_context_t*>(h); }
}  // namespace

DcSender::DcSender(DcContextHandle* ctx, OwnerHandle& owner) : ctx_(ctx), owner_(owner) {}

std::optional<std::uint32_t> DcSender::send_to_owner(std::string_view text,
                                                     std::uint32_t quote_msg_id) {
    std::uint32_t owner = owner_.contact_id.load();
    if (owner == 0) return std::nullopt;

    auto* ctx = as_ctx(ctx_);
    std::uint32_t chat = dc_create_chat_by_contact_id(ctx, owner);
    if (chat == 0) return std::nullopt;

    std::string body(text);

    // Plain path when no quote requested — same one-call DC API as before.
    if (quote_msg_id == 0) {
        std::uint32_t id = dc_send_text_msg(ctx, chat, body.c_str());
        if (id == 0) return std::nullopt;
        return id;
    }

    // Quote path: build a dc_msg_t and attach the prior message as quote so
    // the admin's client threads the notifications under one ticket.
    dc_msg_t* msg = dc_msg_new(ctx, DC_MSG_TEXT);
    if (!msg) return std::nullopt;
    dc_msg_set_text(msg, body.c_str());

    if (dc_msg_t* quoted = dc_get_msg(ctx, quote_msg_id)) {
        dc_msg_set_quote(msg, quoted);
        dc_msg_unref(quoted);
    }
    // If the quoted msg is already gone from the DC db (e.g. very old, never
    // happens in practice for our short-lived tickets), we silently fall back
    // to sending without a quote rather than failing the whole send.

    std::uint32_t id = dc_send_msg(ctx, chat, msg);
    dc_msg_unref(msg);
    if (id == 0) return std::nullopt;
    return id;
}

std::optional<std::uint32_t> DcSender::owner_contact_id() const {
    std::uint32_t v = owner_.contact_id.load();
    if (v == 0) return std::nullopt;
    return v;
}

}  // namespace deltafeedback::dc
