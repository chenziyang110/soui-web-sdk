#include <soui/json_bridge.h>
int main(){soui::sdk::json_bridge bridge("app://external-consumer");return bridge.handle_json("null").empty()?1:0;}
