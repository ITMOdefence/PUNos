#include <appserv/appserv.h>

static ErrorCode validate_string(const String *text, usize limit, bool nonempty)
{
    String checked = { NULL, 0 };
    ErrorCode error;
    if (text->data == NULL || text->length > limit ||
        (nonempty && text->length == 0) || text->data[text->length] != '\0')
        return CORE_INVALID_ARGUMENT;
    error = string_from_utf8((Bytes){ (u8 *)text->data, text->length }, &checked);
    if (error == CORE_OK) error = string_destroy(&checked);
    return error;
}

static bool same(const String *a, const String *b)
{
    bool equal = false;
    return string_equals(a, b, &equal) == CORE_OK && equal;
}

ErrorCode app_catalog_validate(const AppCatalog *catalog)
{
    ErrorCode error;
    usize size;
    if (catalog == NULL || catalog->count > APP_CATALOG_MAX_COUNT ||
        (catalog->count != 0 &&
         (catalog->applications == NULL || catalog->launch_specs == NULL)))
        return CORE_INVALID_ARGUMENT;
    error = validate_string(&catalog->registry_id, 64, true);
    if (error != CORE_OK) return error;
    error = validate_string(&catalog->expected_registry_id, 64, true);
    if (error != CORE_OK) return error;
    if (catalog->registry_id.length != 64 ||
        !same(&catalog->registry_id, &catalog->expected_registry_id))
        return CORE_INVALID_ARGUMENT;
    for (usize i = 0; i < 64; ++i) {
        char c = catalog->registry_id.data[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return CORE_INVALID_ARGUMENT;
    }
    for (usize i = 0; i < catalog->count; ++i) {
        const NativeAppSpec *app = &catalog->applications[i];
        const NativeAppLaunchSpec *launch = &catalog->launch_specs[i];
        error = validate_string(&app->application_id, APP_TEXT_MAX_LENGTH, true);
        if (error != CORE_OK) return error;
        error = validate_string(&app->display_name, APP_TEXT_MAX_LENGTH, false);
        if (error != CORE_OK) return error;
        error = validate_string(&app->executable, STRING_MAX_LENGTH, true);
        if (error != CORE_OK) return error;
        error = validate_string(&launch->application_id, APP_TEXT_MAX_LENGTH, true);
        if (error != CORE_OK) return error;
        error = validate_string(&launch->executable, STRING_MAX_LENGTH, true);
        if (error != CORE_OK) return error;
        if (!same(&launch->application_id, &app->application_id) ||
            !same(&launch->executable, &app->executable) ||
            !app->terminates_on_sigterm ||
            (app->argument_count != 0 && app->default_arguments == NULL) ||
            (app->environment_count != 0 && app->environment == NULL) ||
            core_size_multiply(app->argument_count, sizeof(String), &size) != CORE_OK ||
            core_size_multiply(app->environment_count, sizeof(String), &size) != CORE_OK)
            return CORE_INVALID_ARGUMENT;
        for (usize j = 0; j < i; ++j)
            if (same(&app->application_id, &catalog->applications[j].application_id))
                return CORE_INVALID_ARGUMENT;
        for (usize j = 0; j < app->argument_count; ++j) {
            error = validate_string(&app->default_arguments[j], STRING_MAX_LENGTH, false);
            if (error != CORE_OK) return error;
        }
        for (usize j = 0; j < app->environment_count; ++j) {
            const String *entry = &app->environment[j];
            usize split = 0;
            error = validate_string(entry, STRING_MAX_LENGTH, true);
            if (error != CORE_OK) return error;
            while (split < entry->length && entry->data[split] != '=') ++split;
            if (split == 0 || split == entry->length) return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}
