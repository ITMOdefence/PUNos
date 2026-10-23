#define CHECK_TEST_NAME "uilib"
#include "../../../SystemLibs/testing/check.h"
#include "fake_runtime.h"
#include <gui/protocol.h>
#include <uilib/ui.h>

typedef struct Events { u32 ready, configure, buffer, frame; } Events;
static void on_event(UiViewHandle view, const UiEvent *event, void *opaque)
{
    Events *events=opaque; (void)view;
    if (event->kind==UI_EVENT_READY) events->ready++;
    if (event->kind==UI_EVENT_CONFIGURE) events->configure++;
    if (event->kind==UI_EVENT_BUFFER_AVAILABLE) events->buffer++;
    if (event->kind==UI_EVENT_FRAME_DONE) events->frame++;
}

int main(void)
{
    UiContextHandle ui=NULL; UiViewHandle view=NULL; Events events={0};
    UiConfig config={2}; UiViewConfig view_config={"runtime",UI_VIEW_ROLE_APP,NULL,0,on_event,&events};
    u8 bytes[2048]; usize size; IpcMessage message;
    EXPECT(1,ui_initialize((AppHandle)1,&config,&ui)==CORE_OK);
    EXPECT(2,ui_view_create(ui,&view_config,&view)==CORE_OK);
    fake_connect_clients();
    EXPECT(3,fake_sent_count(true,GUI_SURFACE_CREATE)==1);
    u64 surface_request=fake_last_request(true,GUI_SURFACE_CREATE);
    GuiSurfaceCreateResult surface={{CORE_OK,surface_request},41,{0}};
    for (usize i=0;i<GUI_SURFACE_BIND_TOKEN_SIZE;++i) surface.surface_bind_token[i]=(u8)(i+1);
    EXPECT(4,gui_encode_surface_create_result(&surface,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_SURFACE_CREATE_RESULT,{bytes,size}}; fake_deliver(true,&message);
    EXPECT(5,fake_sent_count(false,GUI_WINDOW_CREATE)==1);
    u64 window_request=fake_last_request(false,GUI_WINDOW_CREATE);
    GuiWindowCreateResult window={{CORE_OK,window_request},73};
    EXPECT(6,gui_encode_window_create_result(&window,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_WINDOW_CREATE_RESULT,{bytes,size}}; fake_deliver(false,&message);
    EXPECT(7,events.ready==1);
    GuiWindowConfigureEvent configure={73,5,64,96,GUI_SCALE_1X,GUI_ORIENTATION_NORMAL,{0,0,0,0}};
    EXPECT(8,gui_encode_window_configure(&configure,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_WINDOW_CONFIGURE,{bytes,size}}; fake_deliver(false,&message);
    EXPECT(9,events.configure==1 && fake_sent_count(true,GUI_SURFACE_BUFFER_IMPORT)==2);
    EXPECT(10,fake_last_had_descriptor(true,GUI_SURFACE_BUFFER_IMPORT));
    for (u64 id=1;id<=2;++id) {
        u64 request=0;
        /* Imports are the last two compositor messages; request ids are monotonic. */
        request=fake_last_request(true,GUI_SURFACE_BUFFER_IMPORT)-(2-id);
        GuiBufferImportResult imported={{CORE_OK,request},100+id};
        EXPECT(11,gui_encode_buffer_import_result(&imported,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
        message=(IpcMessage){GUI_SURFACE_BUFFER_IMPORT_RESULT,{bytes,size}}; fake_deliver(true,&message);
    }
    EXPECT(12,events.buffer==2);
    UiMappedBuffer mapping;
    EXPECT(13,ui_view_acquire_buffer(view,&mapping)==CORE_OK && mapping.width==64 && mapping.height==96);
    GuiRect damage={0,0,64,96};
    EXPECT(14,ui_view_damage(view,&damage,1)==CORE_OK);
    EXPECT(15,ui_view_present(view,mapping.buffer)==CORE_OK);
    EXPECT(16,fake_sent_count(true,GUI_SURFACE_ATTACH)==1 &&
              fake_sent_count(true,GUI_SURFACE_DAMAGE)==1 &&
              fake_sent_count(true,GUI_SURFACE_REQUEST_FRAME)==1 &&
              fake_sent_count(true,GUI_SURFACE_COMMIT)==1);
    u64 commit_request=fake_last_request(true,GUI_SURFACE_COMMIT);
    GuiBasicResult committed={CORE_OK,commit_request};
    EXPECT(17,gui_encode_basic_result(&committed,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_SURFACE_COMMIT_RESULT,{bytes,size}}; fake_deliver(true,&message);
    EXPECT(18,fake_sent_count(false,GUI_WINDOW_ACK_CONFIGURE)==1);
    GuiSurfaceFrameDoneEvent frame={fake_last_request(true,GUI_SURFACE_REQUEST_FRAME),41,1,123};
    EXPECT(19,gui_encode_surface_frame_done(&frame,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_SURFACE_FRAME_DONE,{bytes,size}}; fake_deliver(true,&message);
    EXPECT(20,events.frame==1);
    GuiBufferReleasedEvent released={1,101};
    EXPECT(21,gui_encode_buffer_released(&released,(Bytes){bytes,sizeof(bytes)},&size)==CORE_OK);
    message=(IpcMessage){GUI_SURFACE_BUFFER_RELEASED,{bytes,size}}; fake_deliver(true,&message);
    EXPECT(22,events.buffer==3);
    EXPECT(23,ui_view_destroy(&view)==CORE_OK && ui_shutdown(&ui)==CORE_OK);
    check_passed(); return 0;
}
