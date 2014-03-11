#include <xtreams.h>

namespace {

forward_fn_t null_fn = [](const Event &)->void{};

xtream_node_t child_join(xtream_node_t left, xtream_node_t right) {
  auto node = std::make_shared<xtream_t>();

  node->input_fn = [=](const Event & e) {
    left->input_fn(e);
    right->input_fn(e);
  };

  return node;
}

xtreams_t join(xtream_node_t left, xtream_node_t right) {
  left->output_fn = right->input_fn;

  xtreams_t list;
  list.push_back(left);
  list.push_back(right);

  return list;
}

xtreams_t join(xtreams_t left, xtreams_t right) {

  left.back()->output_fn = right.front()->input_fn;
  left.insert(left.end(), begin(right), end(right));

  return left;
}

xtreams_t join(xtreams_t left, xtream_node_t right) {

  left.back()->output_fn = right->input_fn;
  left.push_back(right);

  return left;
}

}

xtreams_t join(xtream_node_t left, xtreams_t right) {

  left->output_fn = right.front()->input_fn;
  right.push_front(left);

  return right;
}

xtream_node_t create_xtream_node(node_fn_t fn) {
  xtream_node_t node = std::make_shared<xtream_t>();

  node->input_fn = [=](const Event & e) {
    fn(node->output_fn, e);
  };

  return node;
}

xtream_t::xtream_t() : output_fn(null_fn), input_fn(null_fn) {}

xtream_node_t operator+ (xtream_node_t left, xtream_node_t right) {
  return child_join(left, right);
}

xtreams_t operator>>(xtream_node_t left, xtream_node_t right) {
  return join(left, right);
}

xtreams_t operator>>(xtream_node_t left, xtreams_t right) {
  return join(left, right);
}

xtreams_t operator>>(xtreams_t left, xtream_node_t right) {
  return join(left, right);
}

xtreams_t operator>>(xtreams_t left, xtreams_t right) {
  return join(left, right);
}

void push_event(xtream_node_t node, const Event & e) {
  node->input_fn(e);
}

void push_event(xtreams_t l, const Event & e) {
  l.front()->input_fn(e);
}