#include <appframework/app.h>
#include <uilib/ui.h>

typedef struct Demo {
    AppHandle app;
    UiViewHandle view;
    bool configured;
    bool frame_submitted;
} Demo;

static void draw_if_possible(Demo *demo)
{
    UiMappedBuffer mapping;
    GuiRect full;

    if (!demo->configured || demo->frame_submitted ||
        ui_view_acquire_buffer(demo->view, &mapping) != CORE_OK) return;
    for (usize offset = 0; offset + 3 < mapping.size; offset += 4) {
        mapping.data[offset] = 0x40;
        mapping.data[offset + 1] = 0x90;
        mapping.data[offset + 2] = 0xe0;
        mapping.data[offset + 3] = 0xff;
    }
    full = (GuiRect){ 0, 0, mapping.width, mapping.height };
    if (ui_view_damage(demo->view, &full, 1) == CORE_OK &&
        ui_view_present(demo->view, mapping.buffer) == CORE_OK)
        demo->frame_submitted = true;
}

static void on_ui_event(UiViewHandle view, const UiEvent *event, void *opaque)
{
    Demo *demo = opaque;
    (void)view;
    if (event->kind == UI_EVENT_READY) {
        (void)ui_view_publish(demo->view);
    } else if (event->kind == UI_EVENT_CONFIGURE) {
        demo->configured = event->error == CORE_OK;
        draw_if_possible(demo);
    } else if (event->kind == UI_EVENT_BUFFER_AVAILABLE) {
        draw_if_possible(demo);
    } else if (event->kind == UI_EVENT_REDRAW) {
        demo->frame_submitted = false;
        draw_if_possible(demo);
    } else if (event->kind == UI_EVENT_FRAME_DONE) {
        (void)app_request_quit(demo->app);
    } else if (event->kind == UI_EVENT_CLOSE_REQUESTED) {
        (void)ui_view_respond_close(demo->view, event->close.close_serial,
                                    GUI_CLOSE_ALLOW);
        (void)app_request_quit(demo->app);
    }
}

int main(void)
{
    AppConfig app_config = { "hello-ui", "hello-ui" };
    UiConfig ui_config = { UI_BUFFER_COUNT_DEFAULT };
    UiViewConfig view_config = {
        .title = "PUNos UI demo",
        .role = UI_VIEW_ROLE_APP,
        .handler = on_ui_event
    };
    UiContextHandle ui = NULL;
    Demo demo = { 0 };
    ErrorCode result = app_create(&app_config, &demo.app);

    if (result == CORE_OK) result = ui_initialize(demo.app, &ui_config, &ui);
    view_config.context = &demo;
    if (result == CORE_OK) result = ui_view_create(ui, &view_config, &demo.view);
    if (result == CORE_OK) result = app_run(demo.app);
    if (demo.view != NULL) (void)ui_view_destroy(&demo.view);
    if (ui != NULL) (void)ui_shutdown(&ui);
    if (demo.app != NULL) (void)app_destroy(&demo.app);
    return result == CORE_OK ? 0 : 1;
}
