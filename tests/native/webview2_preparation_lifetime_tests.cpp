#include "preparation_lifetime.h"

#include <iostream>
#include <stdexcept>
#include <string>

using soui::sdk::detail::preparation_lifetime;
using operation = preparation_lifetime::operation;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

int main() {
  try {
    {
      preparation_lifetime value;
      require(value.settled() && value.begin(operation::environment), "Environment submission begins pending ownership");
      require(!value.settled(), "A submitted environment cannot close before its callback");
      {
        auto environment = value.receive(operation::environment);
        require(bool(environment) && !value.settled(), "A running callback still owns initialization");
        require(value.begin(operation::controller), "A live environment may create its controller");
      }
      value.cancel();
      require(!value.settled(), "Cancellation does not pretend the controller callback arrived");
      {
        auto controller = value.receive(operation::controller);
        require(bool(controller) && !value.settled(), "Late controller cleanup remains owned until callback returns");
        require(!value.begin(operation::script), "Cancellation cannot install a preload");
      }
      require(value.settled(), "A returned late callback completes native initialization ownership");
    }
    {
      preparation_lifetime value;
      require(value.begin(operation::environment), "Begin before immediate cancellation");
      value.cancel();
      {
        auto environment = value.receive(operation::environment);
        require(bool(environment), "Cancellation still receives the real environment completion");
        require(!value.begin(operation::controller), "A cancelled environment must not create a controller");
      }
      require(value.settled(), "Cancelled environment completion settles after cleanup");
      auto duplicate = value.receive(operation::environment);
      require(!duplicate && value.settled(), "Duplicate arrival cannot revive or underflow ownership");
    }
    {
      preparation_lifetime value;
      require(value.begin(operation::environment), "Begin before synchronous submission failure");
      require(value.settle(operation::environment) && value.settled(), "Synchronous API failure has no future callback ownership");
      require(!value.settle(operation::environment), "Submission failure settles exactly once");
    }
    {
      preparation_lifetime value;
      require(value.begin(operation::script), "Begin script registration");
      bool observed_exception = false;
      try {
        auto script = value.receive(operation::script);
        require(bool(script) && !value.settled(), "Reentrant close during completion is busy");
        value.cancel();
        throw std::runtime_error("controlled callback exception");
      } catch (const std::runtime_error& error) {
        observed_exception = std::string(error.what()) == "controlled callback exception";
      }
      require(observed_exception, "Only the intended callback exception was observed");
      require(value.cancelled() && value.settled(), "An exception releases callback ownership without clearing cancellation");
      require(!value.begin(operation::controller), "A failed cancelled callback cannot resume initialization");
    }
    std::cout << "Preparation ownership, cancellation, duplicate and exception boundaries passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
