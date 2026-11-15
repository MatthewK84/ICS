// link.h: the TCP link to a 577 (ICS-040). The 577's Ethernet port is a raw
// TCP socket, 2101 unless it is set otherwise. Each command is one line out
// and one line back, waited for no longer than the timeout.
#ifndef ICS_TRIGGER_LINK_H
#define ICS_TRIGGER_LINK_H

// JSF deviation D-6: ics::Result and ics::Duration come from .hpp headers.
#include <memory>
#include <string>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/scpi.h>

namespace ics::trigger
{

struct Link_settings
{
  // The 577's IPv4 address, such as "192.168.1.57".
  std::string address;
  uint16 port;
  // The longest wait for a reply.
  Duration timeout;
};

class Tcp_link final : public Scpi_link
{
public:
  // A link to a 577: kInvalidArgument for an address that is not IPv4, and
  // kUnavailable when nothing answers there. Connecting waits as long as the
  // host's network stack does.
  [[nodiscard]] static Result<std::unique_ptr<Tcp_link>> open(const Link_settings& settings);

  Tcp_link(const Tcp_link&) = delete;
  Tcp_link& operator=(const Tcp_link&) = delete;
  Tcp_link(Tcp_link&&) = delete;
  Tcp_link& operator=(Tcp_link&&) = delete;
  ~Tcp_link() override;

  // kUnavailable when the command cannot be sent, the reply does not come in
  // time or is too long, or the 577 closes the connection.
  [[nodiscard]] Result<std::string> ask(const std::string& command) override;

private:
  Tcp_link(const int32 socket, const Duration timeout);

  // The reply: bytes up to a line feed.
  [[nodiscard]] Result<std::string> read_line() const;

  int32 socket_;
  Duration timeout_;
};

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_LINK_H
