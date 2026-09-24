#include <soui/json_bridge.h>
int main(){
  soui::sdk::json_bridge bridge("app://external-consumer");
  if(!bridge.enable_directory_service())return 1;
  if(!bridge.enable_clipboard_service())return 2;
  if(!bridge.enable_window_service())return 3;
  return bridge.handle_json("null").empty()?4:0;
}
