#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Private-to-build POD stream: measure/write, validate, then apply.
 * Pointers and immutable ROM-derived artwork never enter the file. */
typedef struct S3StateIO { uint8_t *data; size_t size, pos; int mode, ok; } S3StateIO;
static void s3_state_bytes(S3StateIO *s, void *data, size_t size)
{
    if (s->data) {
        if (s->pos > s->size || size > s->size - s->pos) { s->ok = 0; return; }
        if (!s->mode) memcpy(s->data + s->pos, data, size);
        if (s->mode == 2) memcpy(data, s->data + s->pos, size);
    }
    s->pos += size;
}
static unsigned s3_state_peek_unsigned(S3StateIO *s, unsigned current)
{
    if (s->mode) {
        if (!s->data || s->pos > s->size || sizeof current > s->size - s->pos) s->ok = 0;
        else memcpy(&current, s->data + s->pos, sizeof current);
    }
    return current;
}
static void s3_state_peek(S3StateIO *s, size_t offset, void *value, size_t size)
{
    if (!s->mode) return;
    if (!s->data || s->pos > s->size || offset > s->size - s->pos ||
        size > s->size - s->pos - offset) { s->ok = 0; return; }
    memcpy(value, s->data + s->pos + offset, size);
}
#define S3_STATE(s,v) s3_state_bytes(s, &(v), sizeof(v))
