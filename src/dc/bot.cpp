#include "dc/bot.h"

#include "db/database.h"
#include "db/messages.h"
#include "db/queries.h"
#include "db/state.h"
#include "db/statement.h"
#include "db/tickets.h"
#include "dc/admin_route.h"
#include "dc/sender.h"

#include <deltachat.h>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace deltafeedback::dc {

namespace {

dc_context_t* as_ctx(DcContextHandle* h) { return reinterpret_cast<dc_context_t*>(h); }

std::string get_str_cfg(dc_context_t* ctx, const char* key) {
    char* v = dc_get_config(ctx, key);
    std::string s = v ? v : "";
    if (v) dc_str_unref(v);
    return s;
}

void send_to(dc_context_t* ctx, std::uint32_t contact_id, const std::string& text,
             std::uint32_t quote_msg_id = 0) {
    if (contact_id == 0) return;
    std::uint32_t chat = dc_create_chat_by_contact_id(ctx, contact_id);
    if (chat == 0) return;

    if (quote_msg_id == 0) {
        dc_send_text_msg(ctx, chat, text.c_str());
        return;
    }
    dc_msg_t* msg = dc_msg_new(ctx, DC_MSG_TEXT);
    if (!msg) return;
    dc_msg_set_text(msg, text.c_str());
    if (dc_msg_t* q = dc_get_msg(ctx, quote_msg_id)) {
        dc_msg_set_quote(msg, q);
        dc_msg_unref(q);
    }
    dc_send_msg(ctx, chat, msg);
    dc_msg_unref(msg);
}

void route_admin_msg(dc_context_t* ctx,
                     std::uint32_t owner_contact_id,
                     std::uint32_t admin_msg_id,
                     const std::string& text,
                     std::uint32_t quoted_msg_id,
                     const std::string& quoted_text,
                     db::TicketsRepo& tickets,
                     db::MessagesRepo& messages) {
    auto reply_to_owner = [&](const std::string& s, std::uint32_t quote = 0) {
        send_to(ctx, owner_contact_id, s, quote);
    };

    auto lookup = [&](std::uint32_t id) -> std::optional<std::string> {
        return messages.find_ticket_by_dc_msg_id(id);
    };
    auto route = route_admin_message(text, quoted_msg_id, quoted_text, lookup);

    if (route.kind == AdminRoute::NoTicketId) {
        reply_to_owner("Ответьте на сообщение бота через «reply» — или укажите [ID] тикета "
                       "в первой строке.");
        return;
    }

    auto t = tickets.get(route.ticket_id);
    if (!t.has_value()) {
        reply_to_owner("Тикет [" + route.ticket_id + "] не найден (возможно, удалён по сроку).");
        return;
    }
    if (t->status == db::TicketStatus::Closed) {
        reply_to_owner("Тикет [" + route.ticket_id + "] уже закрыт.");
        return;
    }

    auto now = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (route.kind == AdminRoute::Close) {
        tickets.close(route.ticket_id, now);
        // Reply on the admin's "/close" with the confirmation, so it threads.
        reply_to_owner("Тикет [" + route.ticket_id + "] закрыт.", admin_msg_id);
        return;
    }

    // Reply path.
    if (route.body.empty()) {
        reply_to_owner("Пустой ответ — ничего не отправлено.");
        return;
    }
    messages.append(route.ticket_id, db::Sender::Admin, route.body, now, "");
    tickets.set_status(route.ticket_id, db::TicketStatus::AwaitingVisitor);
    // silent success on Reply: visitor sees the reply on their ticket page next poll.
}

}  // namespace

struct Bot::Impl {
    DcContextHandle*       ctx_handle;
    OwnerHandle&           owner;
    db::StateRepo&         state;
    db::TicketsRepo&       tickets;
    db::MessagesRepo&      messages;
    dc_event_emitter_t*    emitter = nullptr;
    std::atomic<bool>      running{true};

    Impl(DcContextHandle* c, OwnerHandle& o, db::StateRepo& s,
         db::TicketsRepo& t, db::MessagesRepo& m)
        : ctx_handle(c), owner(o), state(s), tickets(t), messages(m) {}
};

Bot::Bot(DcContextHandle* ctx, OwnerHandle& owner,
         db::StateRepo& state, db::TicketsRepo& tickets, db::MessagesRepo& messages)
    : impl_(std::make_unique<Impl>(ctx, owner, state, tickets, messages)) {

    // Restore owner from state on construction.
    auto stored = state.get("owner_contact_id");
    if (stored.has_value() && !stored->empty()) {
        try {
            owner.contact_id.store(static_cast<std::uint32_t>(std::stoul(*stored)));
        } catch (...) { /* corrupt value — leave as 0 */ }
    }
}

Bot::~Bot() {
    if (impl_->emitter) dc_event_emitter_unref(impl_->emitter);
}

bool Bot::configure_if_needed(const std::string& addr, const std::string& mail_pw) {
    auto* ctx = as_ctx(impl_->ctx_handle);
    if (dc_is_configured(ctx)) return true;

    dc_set_config(ctx, "addr", addr.c_str());
    dc_set_config(ctx, "mail_pw", mail_pw.c_str());
    dc_set_config(ctx, "bot", "1");
    dc_set_config(ctx, "e2ee_enabled", "1");
    // Read receipts: explicit so we don't rely on upstream defaults.
    // Lets the admin's client display "read" on messages we've processed.
    dc_set_config(ctx, "mdns_enabled", "1");
    dc_configure(ctx);

    // Wait for completion via a temporary emitter so the main loop's emitter
    // doesn't also see configure events.
    auto* em = dc_get_event_emitter(ctx);
    if (!em) return false;
    bool ok = false;
    while (impl_->running) {
        dc_event_t* ev = dc_get_next_event(em);
        if (!ev) break;
        int id = dc_event_get_id(ev);
        if (id == DC_EVENT_CONFIGURE_PROGRESS) {
            int p = dc_event_get_data1_int(ev);
            std::printf("[CONFIG] %d/1000\n", p);
            if (p == 1000) { ok = true; dc_event_unref(ev); break; }
            if (p == 0)    { ok = false; dc_event_unref(ev); break; }
        }
        dc_event_unref(ev);
    }
    dc_event_emitter_unref(em);
    return ok;
}

void Bot::set_displayname(const std::string& name) {
    if (name.empty()) return;
    dc_set_config(as_ctx(impl_->ctx_handle), "displayname", name.c_str());
}

void Bot::print_invite_qr() {
    auto* ctx = as_ctx(impl_->ctx_handle);
    char* qr = dc_get_securejoin_qr(ctx, 0);
    if (qr) {
        std::printf("\n========== Invite the admin via this URL ==========\n%s\n"
                    "===================================================\n\n", qr);
        dc_str_unref(qr);
    }
}

int Bot::run_event_loop() {
    auto* ctx = as_ctx(impl_->ctx_handle);
    impl_->emitter = dc_get_event_emitter(ctx);
    if (!impl_->emitter) return 1;

    dc_start_io(ctx);
    std::printf("Bot event loop started. Owner: %u\n", impl_->owner.contact_id.load());

    while (impl_->running) {
        dc_event_t* ev = dc_get_next_event(impl_->emitter);
        if (!ev) break;
        int id = dc_event_get_id(ev);

        switch (id) {
            case DC_EVENT_INFO: {
                char* s = dc_event_get_data2_str(ev);
                if (s) { std::printf("[DC INFO] %s\n", s); dc_str_unref(s); }
                break;
            }
            case DC_EVENT_WARNING: {
                char* s = dc_event_get_data2_str(ev);
                if (s) { std::fprintf(stderr, "[DC WARN] %s\n", s); dc_str_unref(s); }
                break;
            }
            case DC_EVENT_ERROR: {
                char* s = dc_event_get_data2_str(ev);
                if (s) { std::fprintf(stderr, "[DC ERROR] %s\n", s); dc_str_unref(s); }
                break;
            }
            case DC_EVENT_INCOMING_MSG: {
                std::uint32_t chat_id = static_cast<std::uint32_t>(dc_event_get_data1_int(ev));
                std::uint32_t msg_id  = static_cast<std::uint32_t>(dc_event_get_data2_int(ev));

                dc_msg_t* msg = dc_get_msg(ctx, msg_id);
                if (!msg) break;
                std::uint32_t from = dc_msg_get_from_id(msg);
                char* text_c = dc_msg_get_text(msg);
                std::string text = text_c ? text_c : "";
                if (text_c) dc_str_unref(text_c);

                std::uint32_t quoted_msg_id = 0;
                if (dc_msg_t* qm = dc_msg_get_quoted_msg(msg)) {
                    quoted_msg_id = dc_msg_get_id(qm);
                    dc_msg_unref(qm);
                }
                char* qt = dc_msg_get_quoted_text(msg);
                std::string quoted_text = qt ? qt : "";
                if (qt) dc_str_unref(qt);

                dc_msg_unref(msg);

                std::uint32_t current_owner = impl_->owner.contact_id.load();

                if (current_owner == 0) {
                    // First contact wins → becomes admin.
                    impl_->owner.contact_id.store(from);
                    impl_->state.set("owner_contact_id", std::to_string(from));
                    send_to(ctx, from,
                        "Вы зарегистрированы как администратор обратной связи. "
                        "Все новые обращения с формы будут приходить сюда. "
                        "Чтобы ответить — нажмите «ответить» или начните сообщение с [ID]. "
                        "Команда закрытия: [ID] /close");
                    std::printf("[BOT] Owner set: %u\n", from);
                    // Confirm processing: marks seen + emits MDN so admin's
                    // DC client shows "read" on their original message.
                    dc_markseen_msgs(ctx, &msg_id, 1);
                } else if (from == current_owner) {
                    route_admin_msg(ctx, current_owner, msg_id, text, quoted_msg_id, quoted_text,
                                    impl_->tickets, impl_->messages);
                    dc_markseen_msgs(ctx, &msg_id, 1);
                } else {
                    // Stranger: block + delete chat. We never reply (don't
                    // confirm bot existence to randoms) — and intentionally
                    // do NOT send a read receipt either.
                    dc_block_contact(ctx, from, 1);
                    dc_delete_chat(ctx, chat_id);
                    std::printf("[BOT] Blocked stranger contact %u\n", from);
                }
                break;
            }
            default: break;
        }
        dc_event_unref(ev);
    }
    return 0;
}

void Bot::stop() {
    impl_->running.store(false);
    auto* ctx = as_ctx(impl_->ctx_handle);
    dc_stop_io(ctx);
}

void Bot::reset_owner_in_db(db::StateRepo& state, db::Database& db) {
    state.erase("owner_contact_id");
    // Drop all tickets (cascades to messages) — per spec, no carry-over.
    char* err = nullptr;
    sqlite3_exec(db.handle(), "DELETE FROM tickets;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    sqlite3_exec(db.handle(), "DELETE FROM pow_replay;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

}  // namespace deltafeedback::dc
