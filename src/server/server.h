#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace deltafeedback::pow      { class ChallengeIssuer; }
namespace deltafeedback::feedback { class Validator; }
namespace deltafeedback::dc       { class IMessageSender; }
namespace deltafeedback::db       { class Database; class TicketsRepo; class MessagesRepo; class ReplayStore; }

namespace deltafeedback::server {

struct Settings {
    std::string  bind = "0.0.0.0";
    std::uint16_t port = 8080;
    std::string  web_root = "./web";
};

// Wires Crow up. Routes (planned):
//   GET  /                  → web_root/index.html
//   GET  /t/<id>            → web_root/index.html (SPA, fetches via API)
//   GET  /static/*          → web_root assets
//   GET  /api/captcha       → fresh POW challenge {token, difficulty}
//   POST /api/feedback      → submit new ticket   {pow, fields...}
//   GET  /api/tickets/<id>  → status + messages   (Authorization: Bearer <read_token>)
//   POST /api/tickets/<id>  → visitor reply       (Authorization + pow + body)
class Server {
public:
    Server(Settings s,
           pow::ChallengeIssuer&    pow,
           feedback::Validator&     validator,
           dc::IMessageSender&      sender,
           db::TicketsRepo&         tickets,
           db::MessagesRepo&        messages,
           db::ReplayStore&         replay);
    ~Server();

    int run();   // blocks; respects external signal handler that calls _Exit
    void stop(); // best-effort

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deltafeedback::server
