/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
  ... (License omitted for brevity, see header) ...
*/

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#include "cJSON.h"

/* define our own boolean type */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const unsigned char *json;
    size_t position;
} error;
static error global_error = { NULL, 0 };

CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void)
{
    return (const char*) (global_error.json + global_error.position);
}

CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item) 
{
    if (!cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item) 
{
    if (!cJSON_IsNumber(item)) return (double)NAN;
    return item->valuedouble;
}

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (hooks == NULL) {
        /* Reset hooks */
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }

    cJSON_malloc = (hooks->malloc_fn != NULL) ? hooks->malloc_fn : malloc;
    cJSON_free = (hooks->free_fn != NULL) ? hooks->free_fn : free;
}

static cJSON *cJSON_New_Item(const cJSON_Hooks * const hooks)
{
    cJSON* node = (cJSON*)hooks->malloc_fn(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

CJSON_PUBLIC(void) cJSON_Delete(cJSON *item)
{
    cJSON *next = NULL;
    while (item != NULL)
    {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child) cJSON_Delete(item->child);
        if (!(item->type & cJSON_IsReference) && item->valuestring) cJSON_free(item->valuestring);
        if (!(item->type & cJSON_StringIsConst) && item->string) cJSON_free(item->string);
        cJSON_free(item);
        item = next;
    }
}

static int parse_string(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr);
static int parse_number(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr);
static int parse_array(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr);
static int parse_object(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr);

static const unsigned char *parse_value(cJSON * const item, const unsigned char * const input_buffer)
{
    if ((input_buffer == NULL) || (item == NULL)) return NULL;

    const unsigned char *parse_end = NULL;
    size_t skipped = 0;
    while(input_buffer[skipped] != '\0' && input_buffer[skipped] <= 32) skipped++;
    const unsigned char *current = input_buffer + skipped;

    if (*current == '\"') {
        if (parse_string(item, current, &parse_end)) return parse_end;
    }
    else if (*current == '[' && parse_array(item, current, &parse_end)) return parse_end;
    else if (*current == '{' && parse_object(item, current, &parse_end)) return parse_end;
    else if (*current == '-' || (*current >= '0' && *current <= '9')) {
        if (parse_number(item, current, &parse_end)) return parse_end;
    }
    else if (strncmp((const char*)current, "true", 4) == 0) {
        item->type = cJSON_True;
        return current + 4;
    }
    else if (strncmp((const char*)current, "false", 5) == 0) {
        item->type = cJSON_False;
        return current + 5;
    }
    else if (strncmp((const char*)current, "null", 4) == 0) {
        item->type = cJSON_NULL;
        return current + 4;
    }

    global_error.json = input_buffer;
    global_error.position = 0;
    return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value)
{
    return cJSON_ParseWithLength(value, value ? strlen(value) : 0);
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    if (value == NULL) return NULL;
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item == NULL) return NULL;

    const unsigned char *end = parse_value(item, (const unsigned char*)value);
    if ((end == NULL) || (end == (const unsigned char*)value)) {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

/* Simplified parsers */
static int parse_number(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr)
{
    char *endptr = NULL;
    double number = strtod((const char*)input_ptr, &endptr);
    if (input_ptr == (unsigned char*)endptr) return 0;
    item->valuedouble = number;
    item->valueint = (int)number;
    item->type = cJSON_Number;
    *output_ptr = (unsigned char*)endptr;
    return 1;
}

static int parse_string(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr)
{
    const unsigned char *ptr = input_ptr + 1;
    const unsigned char *end_quote = (unsigned char*)strchr((const char*)ptr, '\"');
    if (!end_quote) return 0;

    // Simple copy, not handling escapes for brevity in this restricted environment
    size_t len = end_quote - ptr;
    item->valuestring = (char*)cJSON_malloc(len + 1);
    if (!item->valuestring) return 0;
    memcpy(item->valuestring, ptr, len);
    item->valuestring[len] = '\0';
    item->type = cJSON_String;
    *output_ptr = end_quote + 1;
    return 1;
}

static int parse_array(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr)
{
    cJSON *head = NULL;
    cJSON *current_item = NULL;
    const unsigned char *ptr = input_ptr + 1;
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };

    item->type = cJSON_Array;
    while (*ptr != '\0' && *ptr <= 32) ptr++;
    if (*ptr == ']') { *output_ptr = ptr + 1; return 1; }

    while (1) {
        cJSON *new_item = cJSON_New_Item(&hooks);
        if (!new_item) return 0; // Fail

        if (head == NULL) head = current_item = new_item;
        else {
            current_item->next = new_item;
            new_item->prev = current_item;
            current_item = new_item;
        }

        const unsigned char *end = parse_value(new_item, ptr);
        if (!end) return 0; // Fail
        ptr = end;
        while (*ptr != '\0' && *ptr <= 32) ptr++;

        if (*ptr == ']') {
            item->child = head;
            *output_ptr = ptr + 1;
            return 1;
        }
        if (*ptr == ',') ptr++;
        while (*ptr != '\0' && *ptr <= 32) ptr++;
    }
}

static int parse_object(cJSON * const item, const unsigned char * const input_ptr, const unsigned char ** const output_ptr)
{
    cJSON *head = NULL;
    cJSON *current_item = NULL;
    const unsigned char *ptr = input_ptr + 1;
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };

    item->type = cJSON_Object;
    while (*ptr != '\0' && *ptr <= 32) ptr++;
    if (*ptr == '}') { *output_ptr = ptr + 1; return 1; }

    while (1) {
        cJSON *new_item = cJSON_New_Item(&hooks);
        if (!new_item) return 0;

        if (head == NULL) head = current_item = new_item;
        else {
            current_item->next = new_item;
            new_item->prev = current_item;
            current_item = new_item;
        }

        // Parse key
        while (*ptr != '\0' && *ptr <= 32) ptr++;
        if (*ptr != '\"') return 0;
        
        const unsigned char *end_quote = (unsigned char*)strchr((const char*)ptr + 1, '\"');
        if (!end_quote) return 0;
        size_t len = end_quote - (ptr + 1);
        new_item->string = (char*)cJSON_malloc(len + 1);
        memcpy(new_item->string, ptr + 1, len);
        new_item->string[len] = '\0';
        new_item->type = cJSON_StringIsConst; // Temp flag, cleared later

        ptr = end_quote + 1;
        while (*ptr != '\0' && *ptr <= 32) ptr++;
        if (*ptr != ':') return 0;
        ptr++;

        const unsigned char *val_end = parse_value(new_item, ptr);
        if (!val_end) return 0;
        ptr = val_end;

        while (*ptr != '\0' && *ptr <= 32) ptr++;
        if (*ptr == '}') {
            item->child = head;
            *output_ptr = ptr + 1;
            return 1;
        }
        if (*ptr == ',') ptr++;
        while (*ptr != '\0' && *ptr <= 32) ptr++;
    }
}

// Printers and Creators (Simplified)

static char *cJSON_strdup_string(const char *str) {
    size_t len = strlen(str) + 1;
    char *copy = (char*)cJSON_malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item) item->type = cJSON_Object;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item) item->type = cJSON_Array;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item) {
        item->type = cJSON_String;
        item->valuestring = cJSON_strdup_string(string);
    }
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *item = cJSON_New_Item(&hooks);
    if (item) {
        item->type = boolean ? cJSON_True : cJSON_False;
    }
    return item;
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!item) return 0;
    if (item->string) cJSON_free(item->string);
    item->string = cJSON_strdup_string(string);
    item->type &= ~cJSON_StringIsConst;
    
    if (!object->child) object->child = item;
    else {
        cJSON *c = object->child;
        while (c->next) c = c->next;
        c->next = item;
        item->prev = c;
    }
    return 1;
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    if (!item) return 0;
    if (!array->child) array->child = item;
    else {
        cJSON *c = array->child;
        while (c->next) c = c->next;
        c->next = item;
        item->prev = c;
    }
    return 1;
}

/* Helper functions for adding items to object */

CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *null = cJSON_New_Item(&hooks);
    if(null) {
        null->type = cJSON_NULL;
        cJSON_AddItemToObject(object, name, null);
    }
    return null;
}

CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *true_item = cJSON_New_Item(&hooks);
    if(true_item) {
        true_item->type = cJSON_True;
        cJSON_AddItemToObject(object, name, true_item);
    }
    return true_item;
}

CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *false_item = cJSON_New_Item(&hooks);
    if(false_item) {
        false_item->type = cJSON_False;
        cJSON_AddItemToObject(object, name, false_item);
    }
    return false_item;
}

CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean) {
    cJSON_Hooks hooks = { cJSON_malloc, cJSON_free };
    cJSON *bool_item = cJSON_New_Item(&hooks);
    if(bool_item) {
        bool_item->type = boolean ? cJSON_True : cJSON_False;
        cJSON_AddItemToObject(object, name, bool_item);
    }
    return bool_item;
}

CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number) {
    cJSON *number_item = cJSON_CreateNumber(number);
    if(number_item) {
        cJSON_AddItemToObject(object, name, number_item);
    }
    return number_item;
}

CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string) {
    cJSON *string_item = cJSON_CreateString(string);
    if(string_item) {
        cJSON_AddItemToObject(object, name, string_item);
    }
    return string_item;
}


CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string) {
    cJSON *c = object ? object->child : NULL;
    while (c && cJSON_IsObject(object)) {
        if (c->string && strcmp(c->string, string) == 0) return c;
        c = c->next;
    }
    return NULL;
}

static size_t measure_escaped_string(const char *s) {
    /* Returns length of JSON string including surrounding quotes. */
    size_t n = 2; /* quotes */
    if (s == NULL) return n;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '\"':
            case '\\':
                n += 2; /* escaped */
                break;
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                n += 2;
                break;
            default:
                if (c < 0x20) {
                    n += 6; /* \u00XX */
                } else {
                    n += 1;
                }
                break;
        }
    }
    return n;
}

static int append_char(char *out, size_t out_len, size_t *idx, char c) {
    if ((*idx + 1) >= out_len) return 0;
    out[(*idx)++] = c;
    return 1;
}

static int append_str(char *out, size_t out_len, size_t *idx, const char *s) {
    if (s == NULL) return 1;
    while (*s) {
        if (!append_char(out, out_len, idx, *s++)) return 0;
    }
    return 1;
}

static int append_escaped_string(char *out, size_t out_len, size_t *idx, const char *s) {
    if (!append_char(out, out_len, idx, '\"')) return 0;
    if (s != NULL) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            unsigned char c = *p;
            switch (c) {
                case '\"': if (!append_str(out, out_len, idx, "\\\"")) return 0; break;
                case '\\': if (!append_str(out, out_len, idx, "\\\\")) return 0; break;
                case '\b': if (!append_str(out, out_len, idx, "\\b")) return 0; break;
                case '\f': if (!append_str(out, out_len, idx, "\\f")) return 0; break;
                case '\n': if (!append_str(out, out_len, idx, "\\n")) return 0; break;
                case '\r': if (!append_str(out, out_len, idx, "\\r")) return 0; break;
                case '\t': if (!append_str(out, out_len, idx, "\\t")) return 0; break;
                default:
                    if (c < 0x20) {
                        char tmp[7];
                        /* tmp: \u00XX */
                        snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)c);
                        if (!append_str(out, out_len, idx, tmp)) return 0;
                    } else {
                        if (!append_char(out, out_len, idx, (char)c)) return 0;
                    }
                    break;
            }
        }
    }
    return append_char(out, out_len, idx, '\"');
}

/**
 * @brief Convert double to string without using %f/%g (STM32 newlib-nano compatible)
 * @param value The double value to convert
 * @param buffer Output buffer (must be at least 32 bytes)
 * @param decimals Number of decimal places (max 4)
 * @return Length of the resulting string
 * 
 * This function avoids using floating-point printf which may not work
 * on STM32 with newlib-nano without -u _printf_float linker flag.
 * Also avoids %lld which is not supported by newlib-nano.
 */
static int double_to_str(double value, char *buffer, int decimals) {
    char *ptr = buffer;
    
    /* Handle negative numbers */
    if (value < 0) {
        *ptr++ = '-';
        value = -value;
    }
    
    /* Limit decimals to reasonable range (max 4 to fit in long) */
    if (decimals < 0) decimals = 0;
    if (decimals > 4) decimals = 4;
    
    /* Calculate multiplier for decimal places */
    long multiplier = 1;
    for (int i = 0; i < decimals; i++) {
        multiplier *= 10;
    }
    
    /* Round the value */
    double rounded = value + 0.5 / (double)multiplier;
    
    /* Extract integer part */
    long int_part = (long)rounded;
    
    /* Extract fractional part */
    double frac = rounded - (double)int_part;
    long frac_part = (long)(frac * (double)multiplier);
    
    /* Print integer part using %ld (supported by newlib-nano) */
    int int_len = snprintf(ptr, 16, "%ld", int_part);
    ptr += int_len;
    
    /* Print decimal part if needed */
    if (decimals > 0 && frac_part > 0) {
        *ptr++ = '.';
        
        /* Format with leading zeros if needed */
        char frac_buf[8];
        int frac_len = snprintf(frac_buf, sizeof(frac_buf), "%ld", frac_part);
        
        /* Add leading zeros */
        for (int i = frac_len; i < decimals; i++) {
            *ptr++ = '0';
        }
        
        /* Copy fractional digits, removing trailing zeros */
        int actual_len = frac_len;
        while (actual_len > 0 && frac_buf[actual_len - 1] == '0') {
            actual_len--;
        }
        for (int i = 0; i < actual_len; i++) {
            *ptr++ = frac_buf[i];
        }
        
        /* Remove decimal point if no fractional digits remain */
        if (ptr[-1] == '.') {
            ptr--;
        }
    }
    
    *ptr = '\0';
    return (int)(ptr - buffer);
}

static size_t measure_item(const cJSON *item) {
    if (item == NULL) return 4; /* null */

    if (item->type & cJSON_Object) {
        size_t n = 2; /* {} */
        const cJSON *child = item->child;
        while (child) {
            n += measure_escaped_string(child->string ? child->string : "");
            n += 1; /* : */
            n += measure_item(child);
            if (child->next) n += 1; /* , */
            child = child->next;
        }
        return n;
    }

    if (item->type & cJSON_Array) {
        size_t n = 2; /* [] */
        const cJSON *child = item->child;
        while (child) {
            n += measure_item(child);
            if (child->next) n += 1; /* , */
            child = child->next;
        }
        return n;
    }

    if (item->type & cJSON_String) {
        return measure_escaped_string(item->valuestring ? item->valuestring : "");
    }

    if (item->type & cJSON_Number) {
        char tmp[32];
        int len = double_to_str(item->valuedouble, tmp, 4);
        if (len < 0) return 1;
        return (size_t)len;
    }

    if (item->type & cJSON_True) return 4;
    if (item->type & cJSON_False) return 5;
    /* cJSON_NULL and unknown -> null */
    return 4;
}

static int write_item(const cJSON *item, char *out, size_t out_len, size_t *idx) {
    if (item == NULL) return append_str(out, out_len, idx, "null");

    if (item->type & cJSON_Object) {
        if (!append_char(out, out_len, idx, '{')) return 0;
        const cJSON *child = item->child;
        while (child) {
            if (!append_escaped_string(out, out_len, idx, child->string ? child->string : "")) return 0;
            if (!append_char(out, out_len, idx, ':')) return 0;
            if (!write_item(child, out, out_len, idx)) return 0;
            if (child->next) {
                if (!append_char(out, out_len, idx, ',')) return 0;
            }
            child = child->next;
        }
        return append_char(out, out_len, idx, '}');
    }

    if (item->type & cJSON_Array) {
        if (!append_char(out, out_len, idx, '[')) return 0;
        const cJSON *child = item->child;
        while (child) {
            if (!write_item(child, out, out_len, idx)) return 0;
            if (child->next) {
                if (!append_char(out, out_len, idx, ',')) return 0;
            }
            child = child->next;
        }
        return append_char(out, out_len, idx, ']');
    }

    if (item->type & cJSON_String) {
        return append_escaped_string(out, out_len, idx, item->valuestring ? item->valuestring : "");
    }

    if (item->type & cJSON_Number) {
        char tmp[32];
        double_to_str(item->valuedouble, tmp, 4);
        return append_str(out, out_len, idx, tmp);
    }

    if (item->type & cJSON_True) return append_str(out, out_len, idx, "true");
    if (item->type & cJSON_False) return append_str(out, out_len, idx, "false");
    return append_str(out, out_len, idx, "null");
}

CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item) {
    if (item == NULL) return NULL;

    size_t needed = measure_item(item);
    char *out = (char *)cJSON_malloc(needed + 1);
    if (out == NULL) return NULL;

    size_t idx = 0;
    if (!write_item(item, out, needed + 1, &idx)) {
        cJSON_free(out);
        return NULL;
    }
    out[idx] = '\0';
    return out;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item) { return item && (item->type & cJSON_Number); }
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item) { return item && (item->type & cJSON_String); }
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item) { return item && (item->type & cJSON_Object); }

#ifdef __cplusplus
}
#endif
