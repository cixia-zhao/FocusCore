/*
 Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
 Minimal implementation: parse + accessors only (no serialization).
*/
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

void cJSON_InitHooks(cJSON_Hooks *hooks)
{
    if (hooks == NULL) return;
    cJSON_malloc = (hooks->malloc_fn != NULL) ? hooks->malloc_fn : malloc;
    cJSON_free  = (hooks->free_fn != NULL) ? hooks->free_fn : free;
}

static char *cJSON_strdup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *copy = (char *)cJSON_malloc(len);
    if (copy == NULL) return NULL;
    memcpy(copy, str, len);
    return copy;
}

typedef struct {
    const char *pos;
    const char *end;
} parse_buffer;

static int parse_hex4(const char *s)
{
    int h = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        h <<= 4;
        if (c >= '0' && c <= '9') h += c - '0';
        else if (c >= 'a' && c <= 'f') h += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') h += c - 'A' + 10;
        else return -1;
    }
    return h;
}

static void skip_spaces(parse_buffer *b)
{
    while (b->pos < b->end && (*b->pos == ' ' || *b->pos == '\t' || *b->pos == '\n' || *b->pos == '\r'))
        b->pos++;
}

static cJSON *create_item(int type)
{
    cJSON *item = (cJSON *)cJSON_malloc(sizeof(cJSON));
    if (item == NULL) return NULL;
    memset(item, 0, sizeof(cJSON));
    item->type = type;
    return item;
}

static void add_to_list(cJSON *list, cJSON *item)
{
    if (list->child == NULL) {
        list->child = item;
        item->prev = item;
        item->next = item;
    } else {
        item->prev = list->child->prev;
        item->next = list->child;
        list->child->prev->next = item;
        list->child->prev = item;
    }
}

static void detach_from_list(cJSON *list, cJSON *item)
{
    if (item->prev == item) {
        list->child = NULL;
    } else {
        item->prev->next = item->next;
        item->next->prev = item->prev;
        if (list->child == item) list->child = item->next;
    }
    item->prev = item->next = NULL;
}

/* ── Parsers ────────────────────────────────────────── */

static cJSON *parse_value(parse_buffer *b);

static cJSON *parse_string(parse_buffer *b, char **out)
{
    if (b->pos >= b->end || *b->pos != '"') return NULL;
    b->pos++; /* skip opening quote */

    /* Count length first */
    const char *start = b->pos;
    size_t len = 0;
    while (b->pos < b->end && *b->pos != '"') {
        if (*b->pos == '\\') {
            if (b->pos + 1 >= b->end) return NULL;
            b->pos++;
            switch (*b->pos) {
                case 'u': { b->pos += 4; break; }
                default: break;
            }
        }
        b->pos++;
        len++;
    }
    if (b->pos >= b->end) return NULL;
    const size_t str_len = len;

    char *str = (char *)cJSON_malloc(str_len + 1);
    if (str == NULL) return NULL;

    b->pos = start;
    size_t i = 0;
    while (b->pos < b->end && *b->pos != '"') {
        if (*b->pos == '\\') {
            b->pos++;
            switch (*b->pos) {
                case '"':  str[i++] = '"';  break;
                case '\\': str[i++] = '\\'; break;
                case '/':  str[i++] = '/';  break;
                case 'b':  str[i++] = '\b'; break;
                case 'f':  str[i++] = '\f'; break;
                case 'n':  str[i++] = '\n'; break;
                case 'r':  str[i++] = '\r'; break;
                case 't':  str[i++] = '\t'; break;
                case 'u': {
                    int h = parse_hex4(b->pos + 1);
                    b->pos += 4;
                    if (h < 0) { cJSON_free(str); return NULL; }
                    if (h <= 0x7F) str[i++] = (char)h;
                    else if (h <= 0x7FF) { str[i++] = (char)(0xC0 | (h >> 6)); str[i++] = (char)(0x80 | (h & 0x3F)); }
                    else { str[i++] = (char)(0xE0 | (h >> 12)); str[i++] = (char)(0x80 | ((h >> 6) & 0x3F)); str[i++] = (char)(0x80 | (h & 0x3F)); }
                    break;
                }
                default: str[i++] = *b->pos; break;
            }
        } else {
            str[i++] = *b->pos;
        }
        b->pos++;
    }
    str[i] = '\0';
    b->pos++; /* skip closing quote */

    if (out != NULL) *out = str;
    return (out == NULL) ? create_item(cJSON_String) : NULL;
}

static cJSON *parse_number(parse_buffer *b)
{
    const char *start = b->pos;
    if (*b->pos == '-') b->pos++;
    if (b->pos >= b->end) return NULL;
    while (b->pos < b->end && *b->pos >= '0' && *b->pos <= '9') b->pos++;
    int is_float = 0;
    if (b->pos < b->end && *b->pos == '.') { is_float = 1; b->pos++;
        while (b->pos < b->end && *b->pos >= '0' && *b->pos <= '9') b->pos++; }
    if (b->pos < b->end && (*b->pos == 'e' || *b->pos == 'E')) { is_float = 1; b->pos++;
        if (b->pos < b->end && (*b->pos == '+' || *b->pos == '-')) b->pos++;
        while (b->pos < b->end && *b->pos >= '0' && *b->pos <= '9') b->pos++; }

    size_t len = (size_t)(b->pos - start);
    char tmp[64];
    if (len >= sizeof(tmp)) return NULL;
    memcpy(tmp, start, len);
    tmp[len] = '\0';

    cJSON *item = create_item(cJSON_Number);
    if (item == NULL) return NULL;
    if (is_float) item->valuedouble = atof(tmp);
    else { item->valueint = atoi(tmp); item->valuedouble = (double)item->valueint; }
    return item;
}

static cJSON *parse_array(parse_buffer *b)
{
    b->pos++; /* skip [ */
    cJSON *arr = create_item(cJSON_Array);
    if (arr == NULL) return NULL;

    skip_spaces(b);
    if (b->pos < b->end && *b->pos == ']') { b->pos++; return arr; }

    while (b->pos < b->end) {
        skip_spaces(b);
        cJSON *child = parse_value(b);
        if (child == NULL) { cJSON_Delete(arr); return NULL; }
        add_to_list(arr, child);
        skip_spaces(b);
        if (b->pos >= b->end) { cJSON_Delete(arr); return NULL; }
        if (*b->pos == ',') b->pos++;
        else if (*b->pos == ']') break;
        else { cJSON_Delete(arr); return NULL; }
    }
    if (b->pos < b->end) b->pos++; /* skip ] */
    return arr;
}

static cJSON *parse_object(parse_buffer *b)
{
    b->pos++; /* skip { */
    cJSON *obj = create_item(cJSON_Object);
    if (obj == NULL) return NULL;

    skip_spaces(b);
    if (b->pos < b->end && *b->pos == '}') { b->pos++; return obj; }

    while (b->pos < b->end) {
        skip_spaces(b);
        if (*b->pos != '"') { cJSON_Delete(obj); return NULL; }
        char *key = NULL;
        parse_string(b, &key);
        if (key == NULL) { cJSON_Delete(obj); return NULL; }
        skip_spaces(b);
        if (b->pos >= b->end || *b->pos != ':') { cJSON_free(key); cJSON_Delete(obj); return NULL; }
        b->pos++; /* skip : */
        skip_spaces(b);
        cJSON *child = parse_value(b);
        if (child == NULL) { cJSON_free(key); cJSON_Delete(obj); return NULL; }
        child->string = key;
        add_to_list(obj, child);
        skip_spaces(b);
        if (b->pos >= b->end) { cJSON_Delete(obj); return NULL; }
        if (*b->pos == ',') b->pos++;
        else if (*b->pos == '}') break;
        else { cJSON_Delete(obj); return NULL; }
    }
    if (b->pos < b->end) b->pos++; /* skip } */
    return obj;
}

static cJSON *parse_value(parse_buffer *b)
{
    skip_spaces(b);
    if (b->pos >= b->end) return NULL;
    switch (*b->pos) {
        case '"': return parse_string(b, NULL);
        case '{': return parse_object(b);
        case '[': return parse_array(b);
        case 't': if (b->end - b->pos >= 4 && memcmp(b->pos, "true", 4) == 0) { b->pos += 4; return create_item(cJSON_True); } return NULL;
        case 'f': if (b->end - b->pos >= 5 && memcmp(b->pos, "false", 5) == 0) { b->pos += 5; return create_item(cJSON_False); } return NULL;
        case 'n': if (b->end - b->pos >= 4 && memcmp(b->pos, "null", 4) == 0) { b->pos += 4; return create_item(cJSON_NULL); } return NULL;
        default: if (*b->pos == '-' || (*b->pos >= '0' && *b->pos <= '9')) return parse_number(b);
    }
    return NULL;
}

cJSON *cJSON_Parse(const char *value)
{
    if (value == NULL) return NULL;
    parse_buffer b;
    b.pos = value;
    b.end = value + strlen(value);
    cJSON *root = parse_value(&b);
    return root;
}

void cJSON_Delete(cJSON *item)
{
    if (item == NULL) return;

    /* Delete all children first (handle circular list) */
    cJSON *child = item->child;
    if (child != NULL) {
        cJSON *first = child;
        do {
            cJSON *next = child->next;
            cJSON_Delete(child);
            child = next;
        } while (child != first);
    }

    if (item->string) cJSON_free(item->string);
    if (item->valuestring) cJSON_free(item->valuestring);
    cJSON_free(item);
}

int cJSON_GetArraySize(const cJSON *array)
{
    if (array == NULL || !(array->type & cJSON_Array)) return 0;
    int count = 0;
    cJSON *child = array->child;
    if (child != NULL) {
        do {
            count++;
            child = child->next;
        } while (child != array->child);
    }
    return count;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (array == NULL || !(array->type & cJSON_Array)) return NULL;
    cJSON *child = array->child;
    if (child == NULL) return NULL;
    int i = 0;
    do {
        if (i == index) return child;
        child = child->next;
        i++;
    } while (child != array->child);
    return NULL;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    if (object == NULL || !(object->type & cJSON_Object) || string == NULL) return NULL;
    cJSON *child = object->child;
    if (child == NULL) return NULL;
    do {
        if (child->string != NULL && strcmp(child->string, string) == 0) return child;
        child = child->next;
    } while (child != object->child);
    return NULL;
}

int cJSON_IsObject(const cJSON *item)
{
    return (item != NULL) && (item->type & cJSON_Object);
}

int cJSON_IsArray(const cJSON *item)
{
    return (item != NULL) && (item->type & cJSON_Array);
}
