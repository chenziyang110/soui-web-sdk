#include <soui/json_bridge.h>
int main(){
  soui::sdk::json_bridge bridge("app://external-consumer");
  if(!bridge.enable_clipboard_service())return 1;
  return bridge.handle_json("null").empty()?2:0;
}
