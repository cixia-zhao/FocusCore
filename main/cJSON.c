/*
 Minimal cJSON implementation for ESP32-S3 FocusCore project
 Fixed version with correct circular list traversal
*/

#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

void cJSON_InitHooks(cJSON_Hooks *hooks)
{
    if (hooks) {
        cJSON_malloc = hooks->malloc_fn ? hooks->malloc_fn : malloc;
        cJSON_free = hooks->free_fn ? hooks->free_fn : free;
    }
}

static cJSON *create_item(int type)
{
    cJSON *item = (cJSON *)cJSON_malloc(sizeof(cJSON));
    if (item) {
        memset(item, 0, sizeof(cJSON));
        item->type = type;
        item->next = item;
        item->prev = item;
    }
    return item;
}

typedef struct {
    const char *pos;
    const char *end;
} parse_buffer;

static void skip_whitespace(parse_buffer *b)
{
    while (b->pos < b->end && (*b->pos == ' ' || *b->pos == '\t' || *b->pos == '\r' || *b->pos == '\n')) {
        b->pos++;
    }
}

static cJSON *parse_value(parse_buffer *b);

static cJSON *parse_string(parse_buffer *b)
{
    if (*b->pos != '"') return NULL;
    b->pos++;
    const char *start = b->pos;
    while (b->pos < b->end && *b->pos != '"') {
        if (*b->pos == '\\') b->pos++;
        b->pos++;
    }
    if (b->pos >= b->end) return NULL;
    size_t len = b->pos - start;
    char *str = (char *)cJSON_malloc(len + 1);
    if (!str) return NULL;
    memcpy(str, start, len);
    str[len] = '\0';
    b->pos++;
    cJSON *item = create_item(cJSON_String);
    if (item) item->valuestring = str;
    else cJSON_free(str);
    return item;
}

static cJSON *parse_number(parse_buffer *b)
{
    const char *start = b->pos;
    if (*b->pos == '-') b->pos++;
    while (b->pos < b->end && *b->pos >= '0' && *b->pos <= '9') b->pos++;
    if (b->pos < b->end && *b->pos == '.') {
        b->pos++;
        while (b->pos < b->end && *b->pos >= '0' && *b->pos <= '9') b->pos++;
    }
    cJSON *item = create_item(cJSON_Number);
    if (item) {
        item->valuedouble = atof(start);
        item->valueint = (int)item->valuedouble;
    }
    return item;
}

static cJSON *parse_array(parse_buffer *b)
{
    if (*b->pos != '[') return NULL;
    b->pos++;
    skip_whitespace(b);
    cJSON *array = create_item(cJSON_Array);
    if (!array) return NULL;
    
    if (*b->pos == ']') {
        b->pos++;
        return array;
    }
    
    cJSON *head = NULL, *tail = NULL;
    while (b->pos < b->end) {
        cJSON *child = parse_value(b);
        if (!child) { cJSON_Delete(array); return NULL; }
        
        if (!head) {
            head = tail = child;
            child->next = child->prev = child;
        } else {
            child->prev = tail;
            child->next = head;
            tail->next = child;
            head->prev = child;
            tail = child;
        }
        
        skip_whitespace(b);
        if (*b->pos == ',') {
            b->pos++;
            skip_whitespace(b);
        } else if (*b->pos == ']') {
            break;
        }
    }
    
    if (*b->pos != ']') { cJSON_Delete(array); return NULL; }
    b->pos++;
    array->child = head;
    return array;
}

static cJSON *parse_object(parse_buffer *b)
{
    if (*b->pos != '{') return NULL;
    b->pos++;
    skip_whitespace(b);
    cJSON *object = create_item(cJSON_Object);
    if (!object) return NULL;
    
    if (*b->pos == '}') {
        b->pos++;
        return object;
    }
    
    cJSON *head = NULL, *tail = NULL;
    while (b->pos < b->end) {
        if (*b->pos != '"') { cJSON_Delete(object); return NULL; }
        cJSON *key_item = parse_string(b);
        if (!key_item) { cJSON_Delete(object); return NULL; }
        
        skip_whitespace(b);
        if (*b->pos != ':') { cJSON_Delete(key_item); cJSON_Delete(object); return NULL; }
        b->pos++;
        skip_whitespace(b);
        
        cJSON *value = parse_value(b);
        if (!value) { cJSON_Delete(key_item); cJSON_Delete(object); return NULL; }
        
        value->string = key_item->valuestring;
        key_item->valuestring = NULL;
        cJSON_Delete(key_item);
        
        if (!head) {
            head = tail = value;
            value->next = value->prev = value;
        } else {
            value->prev = tail;
            value->next = head;
            tail->next = value;
            head->prev = value;
            tail = value;
        }
        
        skip_whitespace(b);
        if (*b->pos == ',') {
            b->pos++;
            skip_whitespace(b);
        } else if (*b->pos == '}') {
            break;
        }
    }
    
    if (*b->pos != '}') { cJSON_Delete(object); return NULL; }
    b->pos++;
    object->child = head;
    return object;
}

static cJSON *parse_value(parse_buffer *b)
{
    skip_whitespace(b);
    if (b->pos >= b->end) return NULL;
    
    switch (*b->pos) {
        case '{': return parse_object(b);
        case '[': return parse_array(b);
        case '"': return parse_string(b);
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
    
    cJSON *child = item->child;
    if (child != NULL) {
        cJSON *first = child;
        cJSON *current = first;
        do {
            cJSON *next = current->next;
            cJSON_Delete(current);
            current = next;
        } while (current != first);
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
        cJSON *first = child;
        do {
            count++;
            child = child->next;
        } while (child != first);
    }
    return count;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (array == NULL || !(array->type & cJSON_Array)) return NULL;
    cJSON *child = array->child;
    if (child == NULL) return NULL;
    cJSON *first = child;
    int i = 0;
    do {
        if (i == index) return child;
        child = child->next;
        i++;
    } while (child != first);
    return NULL;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    if (object == NULL || !(object->type & cJSON_Object) || string == NULL) return NULL;
    cJSON *child = object->child;
    if (child == NULL) return NULL;
    
    /* CRITICAL FIX: Must traverse the entire circular list */
    cJSON *first = child;
    do {
        if (child->string != NULL && strcmp(child->string, string) == 0) {
            return child;
        }
        child = child->next;
    } while (child != first);
    
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
