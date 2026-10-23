// In-process Wayland server sends actual wire events to the production
// surface listeners. No phone UI, real player or audio device is contacted.
#include "../elinux/runner/sailfish_cover.cc"
#include <wayland-server-core.h>
#include <sys/socket.h>
#include <cassert>

namespace {
const wl_message events[] = {
 {"onscreen_visibility", "i", nullptr},
 {"set_generic_property", "sa", nullptr},
 {"close", "", nullptr}
};
const wl_interface fixtureInterface = {
 "qt_extended_surface", 1, 0, nullptr, 3, events
};
template<class Surface> void Check(const char* label) {
 int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
 auto* server=wl_display_create();assert(server);
 auto* connection=wl_client_create(server,sockets[0]);assert(connection);
 auto* client=wl_display_connect_to_fd(sockets[1]);assert(client);
 auto* proxy=wl_proxy_create(reinterpret_cast<wl_proxy*>(client),&fixtureInterface);
 assert(proxy);
 auto* resource=wl_resource_create(connection,&fixtureInterface,1,wl_proxy_get_id(proxy));
 assert(resource);
 windowCloseRequested=false;
 {
  Surface surface(reinterpret_cast<qt_extended_surface*>(proxy));
  for(int visibility : {1,0,2,0,1}) {
   wl_resource_post_event(resource,0,visibility);
   wl_client_flush(connection);
   assert(wl_display_dispatch(client)>=0);
   assert(!SailfishWindowCloseRequested());
  }
  wl_resource_post_event(resource,2);
  wl_client_flush(connection);
  assert(wl_display_dispatch(client)>=0);
  assert(SailfishWindowCloseRequested());
 }
 wl_display_disconnect(client);
 wl_client_destroy(connection);wl_display_destroy(server);
 fprintf(stderr,"PASS %s: visibility preserves playback, close requests host exit\n",label);
}
}
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);
 Check<PhoneProperties>("phone");
 Check<CoverProperties>("cover");
 return 0;
}
