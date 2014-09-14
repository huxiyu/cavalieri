#ifndef CAVALIERI_SCHEDULER_REAL_SCHEDULER_H
#define CAVALIERI_SCHEDULER_REAL_SCHEDULER_H

#include <vector>
#include <tbb/concurrent_queue.h>
#include <future>
#include <scheduler/scheduler.h>
#include <pool/async_thread_pool.h>



class real_scheduler : public scheduler_interface {
public:
  real_scheduler();
  remove_task_future_t add_periodic_task(task_fn_t task,
                                          float interval) override;
  remove_task_future_t add_once_task(task_fn_t task, float dt) override;
  time_t unix_time() override;
  void set_time(const time_t t) override;
  void clear() override;
  void stop();

private:
  void async_callback(async_loop & loop);
  std::future<remove_task_fn_t> add_task(task_fn_t task,
                                         float interval, bool once);

private:
  using promise_shared_t = std::shared_ptr<std::promise<remove_task_fn_t>>;

  using task_promise_t  = struct {
                                  promise_shared_t promise;
                                  task_cb_fn_t task;
                                  float interval;
                                  bool once;
                                 };
  using promise_queue_t =
   tbb::concurrent_bounded_queue<task_promise_t>;

  using remove_task_queue_t = tbb::concurrent_bounded_queue<timer_id_t>;

private:
  async_thread_pool threads_;
  std::vector<promise_queue_t> task_promises_;
  std::vector<remove_task_queue_t> remove_tasks_;

};

#endif
