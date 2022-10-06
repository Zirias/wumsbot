#ifndef WUMSBOT_INFODB_H
#define WUMSBOT_INFODB_H

#include <ircbot/decl.h>

#include <stddef.h>
#include <time.h>

C_CLASS_DECL(InfoDb);
C_CLASS_DECL(InfoDbRow);
C_CLASS_DECL(InfoDbEntry);
C_CLASS_DECL(List);

InfoDb *InfoDb_create(const char *filename) ATTR_NONNULL((1));
size_t InfoDb_rowCount(const InfoDb *self) CMETHOD ATTR_PURE;
InfoDbRow *InfoDb_get(InfoDb *self, const char *key) CMETHOD ATTR_NONNULL((2));
int InfoDb_put(InfoDb *self, const InfoDbRow *row) CMETHOD ATTR_NONNULL((2));
int InfoDb_add(InfoDb *self, const char *key, const InfoDbEntry *entry)
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((3));
InfoDbRow *InfoDb_getRandom(InfoDb *self) CMETHOD;
void InfoDb_destroy(InfoDb *self);

const char *InfoDbRow_key(const InfoDbRow *self) CMETHOD ATTR_RETNONNULL;
List *InfoDbRow_entries(InfoDbRow *self) CMETHOD ATTR_RETNONNULL;
void InfoDbRow_destroy(InfoDbRow *self);

InfoDbEntry *InfoDbEntry_create(const char *description, const char *author)
    ATTR_RETNONNULL ATTR_NONNULL((1)) ATTR_NONNULL((2));
time_t InfoDbEntry_time(const InfoDbEntry *self) CMETHOD;
const char *InfoDbEntry_description(const InfoDbEntry *self)
    CMETHOD ATTR_RETNONNULL;
const char *InfoDbEntry_author(const InfoDbEntry *self)
    CMETHOD ATTR_RETNONNULL;
void InfoDbEntry_destroy(InfoDbEntry *self);

#endif
