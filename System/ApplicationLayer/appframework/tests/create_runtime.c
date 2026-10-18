/*
 * Runtime test of app_create(), app_destroy() and the rules around them
 * (DevDoc/ApplicationLayer/appframework.md, "Контракты" 2, 3 and 10).
 *
 * Check groups:
 *   1-12   app_create() argument checks; a refused create leaves no handle
 *   13-20  string limits, in bytes, including multi-byte UTF-8
 *   21-30  one handle at a time; a new one after app_destroy()
 *   31-35  app_destroy() arguments
 *   36-38  the configuration is copied
 *   40-47  calls from a thread other than the owner
 *   50-55  operations after app_run() has returned
 *   60-69  UTF-8 rules compared with libstr
 *   70-78  text length checks
 */

#define CHECK_TEST_NAME "appframework-create"
#include <check.h>

#include <sys/string.h>

#include "support.h"

/* ---- argument checks ---------------------------------------------------- */

static ErrorCode create_with(const char *id, const char *component, AppHandle *out)
{
    AppConfig config = { .application_id = id, .log_component = component };

    return app_create(&config, out);
}

/* After a refused create a valid one must still work: no slot was leaked. */
static bool valid_create_works(void)
{
    AppHandle app = test_app();

    if (app == NULL) {
        return false;
    }
    return app_destroy(&app) == CORE_OK && app == NULL;
}

static void argument_checks(void)
{
    AppHandle app = NULL;
    AppConfig config = test_config();
    char long_id[66];
    char long_component[258];

    for (usize i = 0; i < sizeof(long_id) - 1; ++i) {
        long_id[i] = 'a';
    }
    long_id[sizeof(long_id) - 1] = '\0';
    for (usize i = 0; i < sizeof(long_component) - 1; ++i) {
        long_component[i] = 'c';
    }
    long_component[sizeof(long_component) - 1] = '\0';

    EXPECT(1, app_create(NULL, &app) == CORE_INVALID_ARGUMENT);
    EXPECT(2, app_create(&config, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(3, create_with(NULL, "c", &app) == CORE_INVALID_ARGUMENT);
    EXPECT(4, create_with("a", NULL, &app) == CORE_INVALID_ARGUMENT);
    EXPECT(5, create_with("", "c", &app) == CORE_INVALID_ARGUMENT);
    EXPECT(6, create_with("a", "", &app) == CORE_INVALID_ARGUMENT);
    EXPECT(7, create_with(long_id, "c", &app) == CORE_INVALID_ARGUMENT);    /* 65 bytes */
    EXPECT(8, create_with("a", long_component, &app) == CORE_INVALID_ARGUMENT); /* 257 */
    EXPECT(9, create_with("\xC0\x80", "c", &app) == CORE_INVALID_ARGUMENT);     /* overlong */
    EXPECT(10, create_with("a", "\xED\xA0\x80", &app) == CORE_INVALID_ARGUMENT); /* surrogate */
    EXPECT(11, app == NULL); /* out_app is untouched on failure */
    EXPECT(12, valid_create_works());
}

/* ---- limits --------------------------------------------------------------- */

static void limit_checks(void)
{
    AppHandle app = NULL;
    char id64[65];
    char component256[257];
    char cyrillic32[65];  /* 32 two-byte characters: exactly 64 bytes */
    char cyrillic33[67];  /* 33 two-byte characters: 66 bytes */

    for (usize i = 0; i < 64; ++i) {
        id64[i] = 'a';
    }
    id64[64] = '\0';
    for (usize i = 0; i < 256; ++i) {
        component256[i] = 'c';
    }
    component256[256] = '\0';
    for (usize i = 0; i < 32; ++i) {
        cyrillic32[2 * i] = (char)0xD0;
        cyrillic32[2 * i + 1] = (char)0xB6; /* U+0436 */
    }
    cyrillic32[64] = '\0';
    for (usize i = 0; i < 33; ++i) {
        cyrillic33[2 * i] = (char)0xD0;
        cyrillic33[2 * i + 1] = (char)0xB6;
    }
    cyrillic33[66] = '\0';

    EXPECT(13, create_with(id64, "c", &app) == CORE_OK && app != NULL);
    EXPECT(14, app_destroy(&app) == CORE_OK);
    EXPECT(15, create_with("a", component256, &app) == CORE_OK && app != NULL);
    EXPECT(16, app_destroy(&app) == CORE_OK);
    EXPECT(17, create_with(cyrillic32, "c", &app) == CORE_OK && app != NULL);
    EXPECT(18, app_destroy(&app) == CORE_OK);
    EXPECT(19, create_with(cyrillic33, "c", &app) == CORE_INVALID_ARGUMENT);
    EXPECT(20, create_with("\xF0\x9F\x98\x80", "\xE2\x82\xAC", &app) == CORE_OK &&
                   app_destroy(&app) == CORE_OK); /* 4- and 3-byte characters */
}

/* ---- one handle at a time ---------------------------------------------------- */

static void single_instance(void)
{
    AppConfig config = test_config();
    AppHandle first = NULL;
    AppHandle second = NULL;

    EXPECT(21, app_create(&config, &first) == CORE_OK && first != NULL);
    EXPECT(22, app_create(&config, &second) == CORE_INVALID_STATE);
    EXPECT(23, second == NULL);
    EXPECT(24, app_destroy(&first) == CORE_OK);
    EXPECT(25, first == NULL);
    EXPECT(26, app_create(&config, &second) == CORE_OK && second != NULL);
    EXPECT(27, app_destroy(&second) == CORE_OK);
    /* Create and destroy many times: neither the slot nor memory runs out. */
    for (u32 i = 0; i < 200; ++i) {
        if (!valid_create_works()) {
            check_fail(28);
        }
    }
    EXPECT(29, valid_create_works());
    /* A second create does not disturb the first handle. */
    first = test_app();
    EXPECT(30, first != NULL && app_create(&config, &second) == CORE_INVALID_STATE &&
                   app_request_quit(first) == CORE_OK && app_destroy(&first) == CORE_OK);
}

static void destroy_arguments(void)
{
    AppHandle app = NULL;

    EXPECT(31, app_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(32, app_destroy(&app) == CORE_INVALID_ARGUMENT); /* *app == NULL */
    app = test_app();
    EXPECT(33, app != NULL && app_destroy(&app) == CORE_OK);
    EXPECT(34, app_destroy(&app) == CORE_INVALID_ARGUMENT); /* already destroyed */
    EXPECT(35, app_request_quit(NULL) == CORE_INVALID_ARGUMENT);
}

static void configuration_is_copied(void)
{
    char id[] = "copied-app";
    char component[] = "copied-component";
    AppConfig config = { .application_id = id, .log_component = component };
    AppHandle app = NULL;

    EXPECT(36, app_create(&config, &app) == CORE_OK);
    id[0] = 'X';
    component[0] = 'Y'; /* the caller's strings may change after the call */
    EXPECT(37, app->application_id_length == 10 && app->application_id[0] == 'c' &&
                   app->application_id[10] == '\0');
    EXPECT(38, app->log_component_length == 16 && app->log_component[0] == 'c' &&
                   app->log_component[16] == '\0');
    EXPECT(39, app_destroy(&app) == CORE_OK);
}

/* ---- another thread ------------------------------------------------------------ */

typedef struct ForeignCalls {
    AppHandle app;
    ErrorCode run;
    ErrorCode add_fd;
    ErrorCode add_timer;
    ErrorCode remove;
    ErrorCode destroy;
    ErrorCode quit;
} ForeignCalls;

static void ignore_event(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    (void)app;
    (void)source;
    (void)mask;
    (void)context;
}

static i32 foreign_thread(void *argument)
{
    ForeignCalls *calls = argument;
    AppHandle copy = calls->app;
    AppEventSourceId source = 0;

    calls->run = app_run(calls->app);
    calls->add_fd = app_event_add_fd(calls->app, 0, EVENT_READABLE, ignore_event, NULL, &source);
    calls->add_timer = app_event_create_timer(calls->app, 1, 0, ignore_event, NULL, &source);
    calls->remove = app_event_remove(calls->app, 1);
    calls->destroy = app_destroy(&copy);
    calls->quit = app_request_quit(calls->app); /* the one call that is allowed */
    return 0;
}

static void foreign_thread_checks(void)
{
    ForeignCalls calls = { 0 };
    AppHandle app = test_app();
    ErrorCode run;

    EXPECT(40, app != NULL);
    calls.app = app;
    EXPECT(41, test_run_on_thread(foreign_thread, &calls));
    EXPECT(42, calls.run == CORE_INVALID_STATE);
    EXPECT(43, calls.add_fd == CORE_INVALID_STATE && calls.add_timer == CORE_INVALID_STATE);
    EXPECT(44, calls.remove == CORE_INVALID_STATE);
    EXPECT(45, calls.destroy == CORE_INVALID_STATE);
    EXPECT(46, calls.quit == CORE_OK);
    /* The refused calls changed nothing: the owner can still run and the quit
     * from the other thread is honoured at once. */
    run = app_run(app);
    EXPECT(47, run == CORE_OK && app_destroy(&app) == CORE_OK);
}

/* ---- after app_run() ------------------------------------------------------------- */

static void after_run_checks(void)
{
    AppHandle app = test_app();
    AppEventSourceId source = 0;

    EXPECT(50, app != NULL && app_request_quit(app) == CORE_OK && app_run(app) == CORE_OK);
    EXPECT(51, app_run(app) == CORE_INVALID_STATE); /* no second run */
    EXPECT(52, app_event_add_fd(app, 0, EVENT_READABLE, ignore_event, NULL, &source) ==
                   CORE_INVALID_STATE);
    EXPECT(53, app_event_create_timer(app, 1, 0, ignore_event, NULL, &source) ==
                   CORE_INVALID_STATE);
    EXPECT(54, app_event_remove(app, 12345) == CORE_NOT_FOUND); /* allowed, nothing there */
    EXPECT(55, app_request_quit(app) == CORE_OK && app_destroy(&app) == CORE_OK);
}

/* ---- UTF-8 ----------------------------------------------------------------------- */

typedef struct Utf8Case {
    const u8 *bytes;
    usize size;
} Utf8Case;

#define CASE(literal) { (const u8 *)(literal), sizeof(literal) - 1 }

/* The index of the first case on which the library and libstr disagree with
 * the expected answer, or -1. */
static i32 utf8_first_mismatch(const Utf8Case *cases, usize count, bool expected)
{
    for (usize i = 0; i < count; ++i) {
        Bytes view = { (u8 *)cases[i].bytes, cases[i].size };
        String copy = { 0 };
        bool ours = libappframework_internal_utf8_valid(cases[i].bytes, cases[i].size);
        bool libstr = string_from_utf8(view, &copy) == CORE_OK;

        if (libstr) {
            (void)string_destroy(&copy);
        }
        if (ours != expected || libstr != expected) {
            return (i32)i;
        }
    }
    return -1;
}

static void utf8_checks(void)
{
    static const Utf8Case valid[] = {
        CASE("a"), CASE("hello"), CASE("\xC2\x80"), CASE("\xC3\xA9"), CASE("\xDF\xBF"),
        CASE("\xE0\xA0\x80"), CASE("\xE2\x82\xAC"), CASE("\xED\x9F\xBF"),
        CASE("\xEE\x80\x80"), CASE("\xEF\xBF\xBD"), CASE("\xEF\xBF\xBF"),
        CASE("\xF0\x90\x80\x80"), CASE("\xF0\x9F\x98\x80"), CASE("\xF4\x8F\xBF\xBF"),
        CASE("a\xC3\xA9z"),
    };
    static const Utf8Case overlong_and_range[] = {
        CASE("\xC0\x80"), CASE("\xC1\xBF"), CASE("\xE0\x80\x80"), CASE("\xE0\x9F\xBF"),
        CASE("\xED\xA0\x80"), CASE("\xED\xBF\xBF"), CASE("\xF0\x80\x80\x80"),
        CASE("\xF0\x8F\xBF\xBF"), CASE("\xF4\x90\x80\x80"), CASE("\xF5\x80\x80\x80"),
        CASE("\xF8\x88\x80\x80\x80"), CASE("\xFF"), CASE("\xFE"),
    };
    static const Utf8Case malformed[] = {
        CASE("\x80"), CASE("\xBF"), CASE("\xC2"), CASE("\xE2\x82"), CASE("\xF0\x90\x80"),
        CASE("\xE2\x28\xA1"), CASE("\xE2\x82\x28"), CASE("\xF0\x28\x8C\xBC"),
        CASE("\xC2\x41"), CASE("a\xC3"), CASE("\xC3\xA9\x80"),
    };
    static const u8 embedded_nul[] = { 'a', 0x00, 'b' };

    EXPECT(60, utf8_first_mismatch(valid, sizeof(valid) / sizeof(valid[0]), true) == -1);
    EXPECT(61, utf8_first_mismatch(overlong_and_range,
                                   sizeof(overlong_and_range) / sizeof(overlong_and_range[0]),
                                   false) == -1);
    EXPECT(62, utf8_first_mismatch(malformed, sizeof(malformed) / sizeof(malformed[0]),
                                   false) == -1);
    EXPECT(63, !libappframework_internal_utf8_valid(embedded_nul, sizeof(embedded_nul)));
    EXPECT(64, libappframework_internal_utf8_valid(NULL, 0));
    EXPECT(65, !libappframework_internal_utf8_valid(NULL, 1));
    EXPECT(66, libappframework_internal_utf8_valid((const u8 *)"", 0));
}

/* ---- text length --------------------------------------------------------------------- */

static void text_checks(void)
{
    usize length = 99;
    MemoryBlock block;
    MemoryBlock guard;
    char *edge;

    EXPECT(70, libappframework_internal_text_check("abc", 1, 3, &length) == CORE_OK &&
                   length == 3);
    EXPECT(71, libappframework_internal_text_check("abcd", 1, 3, &length) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(72, libappframework_internal_text_check("", 1, 3, &length) == CORE_INVALID_ARGUMENT);
    EXPECT(73, libappframework_internal_text_check("", 0, 3, &length) == CORE_OK && length == 0);
    EXPECT(74, libappframework_internal_text_check(NULL, 0, 3, &length) == CORE_INVALID_ARGUMENT);
    EXPECT(75, libappframework_internal_text_check("a", 0, 3, NULL) == CORE_INVALID_ARGUMENT);

    /* Text without a terminator right before an unmapped page: the check must
     * stop after max_length + 1 bytes, or the read faults. */
    EXPECT(76, memory_allocate(2 * 4096, &block) == CORE_OK);
    edge = (char *)block.data + 4096 - 65;
    for (usize i = 0; i < 65; ++i) {
        edge[i] = 'a';
    }
    guard.data = block.data + 4096;
    guard.size = 4096;
    /* Only the second page becomes inaccessible. */
    EXPECT(77, memory_protect(&guard, MEM_NONE) == CORE_OK);
    EXPECT(78, libappframework_internal_text_check(edge, 1, 64, &length) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(79, memory_protect(&guard, MEM_READ | MEM_WRITE) == CORE_OK);
    (void)memory_free(&block);
}

int main(void)
{
    argument_checks();
    limit_checks();
    single_instance();
    destroy_arguments();
    configuration_is_copied();
    foreign_thread_checks();
    after_run_checks();
    utf8_checks();
    text_checks();
    check_passed();
    return 0;
}
