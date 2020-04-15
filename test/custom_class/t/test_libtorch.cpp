
#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>
#include <iterator>
#include <list>
#include <torch/script.h>
#include <pybind11/pybind11.h>

using namespace std;

torch::Tensor warp_perspective(torch::Tensor image) { return image; }
struct Foo {
  int x, y;
  Foo(): x(2), y(5){}
  Foo(int x_, int y_) : x(x_), y(y_) {}
  void display() {
    cout<<"x: "<<x<<' '<<"y: "<<y<<endl;
  }
};

static auto registry = torch::jit::RegisterOperators("my_ops::warp_perspective",
                                                     &warp_perspective);
static auto test = torch::jit::class_<Foo>("Foo");
