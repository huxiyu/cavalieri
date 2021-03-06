#include <glog/logging.h>
#include <functional>
#include <utility>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <util/util.h>
#include <streams/lib.h>
#include <transport/tcp_connection.h>
#include <transport/tcp_client_pool.h>

namespace {

const size_t k_queue_capacity = 10000;
const size_t k_reconnect_interval_secs = 2;

int connect_client(const std::string host, const int port) {

  auto sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (sock_fd < 0) {
    LOG(ERROR) << "failed to create socket to " << host << " and " << port;
    return -1;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));

  hints.ai_family = AF_INET;

  struct addrinfo* server;

  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                  &server) != 0)
  {
    LOG(ERROR) << "failed to resolve: " << host;
    return -1;
  }

  if (connect(sock_fd, server->ai_addr, server->ai_addrlen) < 0) {

    LOG(ERROR) << "failed to connect() to " << host << " and " << port;

    freeaddrinfo(server);
    close(sock_fd);

    return -1;
  }

  freeaddrinfo(server);

  fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK);

  VLOG(1) << "tcp client connected to  create socket to "
          << host << " and " << port;

  return sock_fd;

}

bool add_client(const int loop_id, tcp_pool & tcp_pool,
                const std::string host, const int port) {

  auto socket_fd = connect_client(host, port);
  bool ok = socket_fd != -1;

  if (ok) {

    tcp_pool.add_client(loop_id, socket_fd);

  }

  return ok;
}


}

using namespace std::placeholders;

tcp_client_pool::tcp_client_pool(size_t thread_num, const std::string host,
                                 const int port,
                                 output_event_fn_t output_event_fn)
:
  tcp_pool_(
    thread_num,
    {},
    std::bind(&tcp_client_pool::create_conn, this, _1, _2, _3),
    std::bind(&tcp_client_pool::data_ready, this, _1, _2),
    std::bind(&tcp_client_pool::async, this, _1)
  ),
  host_(host),
  port_(port),
  thread_event_queues_(0),
  fd_event_queues_(thread_num),
  output_event_fn_(output_event_fn),
  output_events_fn_(),
  batched_(false),
  batch_size_(0),
  flush_batch_(thread_num, 0),
  next_thread_(0)
{

  LOG(FATAL) << "Not supported yet";
  // TODO

}

tcp_client_pool::tcp_client_pool(size_t thread_num, const std::string host,
                                 const int port, size_t batch_size,
                                 output_events_fn_t output_events_fn)
:
  tcp_pool_(
    thread_num,
    {},
    std::bind(&tcp_client_pool::create_conn, this, _1, _2, _3),
    std::bind(&tcp_client_pool::data_ready, this, _1, _2),
    std::bind(&tcp_client_pool::async, this, _1)
  ),
  host_(host),
  port_(port),
  thread_event_queues_(0),
  fd_event_queues_(thread_num),
  output_event_fn_(),
  output_events_fn_(output_events_fn),
  batched_(true),
  batch_size_(batch_size),
  flush_batch_(thread_num, 0),
  next_thread_(0)
{

  for (size_t i = 0; i < thread_num; i++) {

    auto queue = std::make_shared<event_queue_t>();
    queue->set_capacity(k_queue_capacity);

    thread_event_queues_.push_back(queue);

    tcp_pool_.loop(i).add_periodic_task(
        k_global_ns,
        std::bind(&tcp_client_pool::connect_clients, this, _1),
        k_reconnect_interval_secs);

    tcp_pool_.loop(i).add_periodic_task(
        k_global_ns,
        std::bind(&tcp_client_pool::signal_batch_flush, this, _1),
        k_reconnect_interval_secs);

  }

  tcp_pool_.start_threads();

}


void tcp_client_pool::push_event(const Event & event) {

  VLOG(3) << "push_event() " << event.json_str();

  auto & queue = thread_event_queues_[next_thread_];

  if (!queue->try_push(event)) {
    LOG(ERROR) << "queue of thread " << next_thread_ << " is full";
    return;
  }

  if (batched_ && queue->size() < static_cast<int>(batch_size_)) {
    return;
  }

  tcp_pool_.signal_thread(next_thread_);

  next_thread_ = (next_thread_ + 1) % thread_event_queues_.size();

}

void tcp_client_pool::create_conn(int fd, async_loop & loop,
                                 tcp_connection & conn)
{
  fd_conn_data_t conn_data(conn, fd_event_queue_t{});

  fd_event_queues_[loop.id()].insert({fd, std::move(conn_data)});
}

void tcp_client_pool::data_ready(async_fd & async, tcp_connection & tcp_conn) {

  auto & fd_conn = fd_event_queues_[async.loop().id()];
  auto it = fd_conn.find(async.fd());

  CHECK(it != fd_conn.end()) << "couldn't find fd";

  /* First things first, check whether the connection is closed */
  if (async.ready_read()) {

    tcp_conn.read();

    if (tcp_conn.close_connection) {

      fd_conn.erase(it);

      return;
    }

  }

  /* Connection is open */

  auto & out_queue = it->second.second;

  if (!tcp_conn.pending_write() && out_queue.empty()) {

    /* There isn't anything to send, we only want read notifications. */
    async.loop().set_fd_mode(async.fd(), async_fd::read);

    return;
  }

  if (!async.ready_write()) {

    /* fd is not ready to write */
    return;
  }

  /* Fill the buffer with more data to send */

  while (!out_queue.empty()) {

    auto & event = out_queue.front();

    auto out_ptr = reinterpret_cast<char *>(&event[0]);
    auto out_len = event.size();

    if (!tcp_conn.queue_write(out_ptr, out_len)) {
      break;
    }

    async.loop().set_fd_mode(async.fd(), async_fd::readwrite);
    out_queue.pop();

  }

  tcp_conn.write();

}

void tcp_client_pool::async(async_loop & loop) {

  size_t loop_id =  loop.id();

  if (fd_event_queues_[loop_id].empty()) {
    return;
  }

  auto event_queue = thread_event_queues_[loop_id];

  if (batched_) {

    bool flush = flush_batch_[loop_id];
    bool enough = event_queue->size() > static_cast<int>(batch_size_);

    if (flush || enough) {

      std::vector<Event> events;

      while (!event_queue->empty()) {

        Event event;
        if (!event_queue->try_pop(event)) {
          continue;
        }

        events.emplace_back(std::move(event));

      }

      if (events.empty()) {
        return;
      }

      auto output = output_events_fn_(std::move(events));

      if (flush) {
        flush_batch_[loop_id] = 0;
      }

      // Add result to output queue
      for (auto & fd_conn : fd_event_queues_[loop_id]) {

        auto & out_queue = fd_conn.second.second;

        if (out_queue.size() > k_queue_capacity) {
          LOG(ERROR) << "Per connection event queue is full";
        } else {
          out_queue.push(std::move(output));
          loop.set_fd_mode(fd_conn.first, async_fd::readwrite);
        }

      }
    }

  } else {

    while (!event_queue->empty()) {

      Event event;
      if (!event_queue->try_pop(event)) {
        continue;
      }

      auto output = output_event_fn_(std::move(event));

    }

  }

}

void tcp_client_pool::signal_batch_flush(const size_t loop_id) {

  VLOG(3) << "signal_batch_flush";

  flush_batch_[loop_id] = 1;
  tcp_pool_.signal_thread(loop_id);

}

void tcp_client_pool::connect_clients(const size_t loop_id) {

  VLOG(3) << "connect_clients";

  if (!fd_event_queues_[loop_id].empty()) {
    return;
  }

  add_client(loop_id, tcp_pool_, host_, port_);

}
