#include <glog/logging.h>
#include <util.h>
#include <stdio.h>
#include <sstream>
#include <external/graphite_pool.h>

using namespace std::placeholders;

namespace {

const size_t k_buffer_size = 2048;
const size_t k_batch_size = 100;

}

graphite_pool::graphite_pool(size_t thread_num, const std::string host,
                             const int port)
  :
  tcp_client_pool_(
    thread_num,
    host,
    port,
    k_batch_size,
    std::bind(&graphite_pool::output_events, this, _1)
  )
{
}

void graphite_pool::push_event(const Event & event) {
  VLOG(1) << "push_event()";

  tcp_client_pool_.push_event(event);
}

std::vector<char> graphite_pool::output_events(const std::vector<Event> events)
{

  std::stringstream sstream;

  for (const auto & event : events) {

    sstream << event.host() << "." << event.service() << " "
            << metric_to_double(event) << event.time() << "\n";
  }

  std::string ev_str = sstream.str();

  return std::vector<char>(ev_str.begin(), ev_str.end());

}