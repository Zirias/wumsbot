#include "infodb.h"

#include <ircbot/list.h>
#include <ircbot/log.h>
#include <ircbot/util.h>

#include <db.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/types.h>
#include <threads.h>

struct InfoDb
{
    DB *db;
    size_t rowCapa;
    size_t rowUsed;
    pthread_mutex_t lock;
};

static thread_local InfoDb *lockedDb;
static thread_local int lockCount;

static void lock(InfoDb *db)
{
    if (db == lockedDb)
    {
	++lockCount;
	return;
    }
    if (!lockedDb)
    {
	lockedDb = db;
	lockCount = 1;
    }
    pthread_mutex_lock(&(db)->lock);
}

static void unlock(InfoDb *db)
{
    if (db == lockedDb)
    {
	if (--lockCount) return;
	lockedDb = 0;
    }
    pthread_mutex_unlock(&(db)->lock);
}

struct InfoDbRow
{
    char *key;
    IBList *entries;
};

struct InfoDbEntry
{
    size_t authorlen;
    time_t time;
    char content[];
};

static const uint8_t rowCapaKey[] = { 0, 0 };
static const uint8_t rowUsedKey[] = { 0, 1 };
static const uint8_t freeListKey[] = { 0, 2 };

static void uint64_ser(uint8_t *data, uint64_t val)
{
    data[0] = val >> 56;
    data[1] = (val >> 48) & 0xff;
    data[2] = (val >> 40) & 0xff;
    data[3] = (val >> 32) & 0xff;
    data[4] = (val >> 24) & 0xff;
    data[5] = (val >> 16) & 0xff;
    data[6] = (val >> 8) & 0xff;
    data[7] = val & 0xff;
}

static uint64_t uint64_deser(const uint8_t *data)
{
    return ((uint64_t)data[0]<<56)
	|((uint64_t)data[1]<<48)
	|((uint64_t)data[2]<<40)
	|((uint64_t)data[3]<<32)
	|((uint64_t)data[4]<<24)
	|((uint64_t)data[5]<<16)
	|((uint64_t)data[6]<<8)
	|(uint64_t)data[7];
}

static void time_ser(uint8_t *data, time_t time)
{
    struct tm tm;
    gmtime_r(&time, &tm);
    uint32_t year = (uint32_t)tm.tm_year;
    data[0] = year >> 24;
    data[1] = (year >> 16) & 0xff;
    data[2] = (year >> 8) & 0xff;
    data[3] = year & 0xff;
    data[4] = tm.tm_mon;
    data[5] = tm.tm_mday;
    data[6] = tm.tm_hour;
    data[7] = tm.tm_min;
    data[8] = tm.tm_sec;
}

static time_t time_deser(const uint8_t *data)
{
    struct tm tm = {0};
    uint32_t year = ((uint32_t)data[0]<<24)
	|((uint32_t)data[1]<<16)
	|((uint32_t)data[2]<<8)
	|(uint32_t)data[3];
    tm.tm_year = (int32_t)year;
    tm.tm_mon = data[4];
    tm.tm_mday = data[5];
    tm.tm_hour = data[6];
    tm.tm_min = data[7];
    tm.tm_sec = data[8];
    return timegm(&tm);
}

static uint8_t *row_ser(const InfoDbRow *row, size_t *size)
{
    size_t keysz = strlen(row->key) + 1;
    size_t sz = keysz;
    IBListIterator *i = IBList_iterator(row->entries);
    while (IBListIterator_moveNext(i))
    {
	InfoDbEntry *entry = IBListIterator_current(i);
	sz += 9 + entry->authorlen
	    + strlen(entry->content+entry->authorlen+1) + 2;
    }
    uint8_t *ser = IB_xmalloc(sz);
    memcpy(ser, row->key, keysz);
    uint8_t *p = ser + keysz;
    while (IBListIterator_moveNext(i))
    {
	InfoDbEntry *entry = IBListIterator_current(i);
	time_ser(p, entry->time);
	p += 9;
	size_t contentsz = entry->authorlen
	    + strlen(entry->content+entry->authorlen+1) + 2;
	memcpy(p, entry->content, contentsz);
	p += contentsz;
    }
    IBListIterator_destroy(i);
    *size = sz;
    return ser;
}

static InfoDbRow *row_deser(const uint8_t *data, size_t datasz)
{
    const char *key = (const char *)data;
    const char *keyend = memchr(data, 0, datasz);
    if (!keyend) return 0;
    InfoDbRow *row = IB_xmalloc(sizeof *row);
    row->key = IB_copystr(key);
    row->entries = IBList_create();
    size_t keylen = keyend - key;
    data += keylen + 1;
    datasz -= keylen + 1;
    while (datasz > 13)
    {
	time_t time = time_deser(data);
	data+=9; datasz-=9;
	const char *author = (const char *)data;
	const char *authorend = memchr(data, 0, datasz);
	if (authorend)
	{
	    size_t authorlen = authorend - author;
	    const char *descend = memchr(authorend+1, 0, datasz-authorlen-1);
	    if (descend)
	    {
		size_t desclen = descend - authorend - 1;
		InfoDbEntry *entry = IB_xmalloc(sizeof *entry
			+ authorlen + desclen + 2);
		entry->authorlen = authorlen;
		entry->time = time;
		memcpy(entry->content, data, authorlen+desclen+2);
		IBList_append(row->entries, entry, free);
		data = (const uint8_t *)descend+1;
		datasz -= authorlen+desclen+2;
	    }
	    else break;
	}
	else break;
    }
    if (datasz)
    {
	InfoDbRow_destroy(row);
	row = 0;
    }
    return row;
}

InfoDb *InfoDb_create(const char *filename)
{
    InfoDb *self = IB_xmalloc(sizeof *self);
    if (pthread_mutex_init(&self->lock, 0) != 0)
    {
	free(self);
	self = 0;
    }
    else if ((self->db = dbopen(filename, O_RDWR|O_CREAT, 0600, DB_BTREE, 0)))
    {
	IBLog_fmt(L_INFO, "database file `%s' opened", filename);
	DBT id = { (void *)rowCapaKey, sizeof rowCapaKey };
	DBT val = { 0 };
	int needsync = 0;
	if (self->db->get(self->db, &id, &val, 0) == 0 && val.size == 8)
	{
	    self->rowCapa = (size_t)uint64_deser(val.data);
	}
	else
	{
	    uint8_t sz[8] = { 0 };
	    val.data = sz;
	    val.size = 8;
	    self->db->put(self->db, &id, &val, 0);
	    self->rowCapa = 0;
	    needsync = 1;
	}
	id.data = (void *)rowUsedKey;
	if (self->db->get(self->db, &id, &val, 0) == 0 && val.size == 8)
	{
	    self->rowUsed = (size_t)uint64_deser(val.data);
	}
	else
	{
	    uint8_t sz[8] = { 0 };
	    val.data = sz;
	    val.size = 8;
	    self->db->put(self->db, &id, &val, 0);
	    self->rowUsed = 0;
	    needsync = 1;
	}
	if (self->rowUsed > self->rowCapa)
	{
	    self->db->close(self->db);
	    pthread_mutex_destroy(&self->lock);
	    free(self);
	    self = 0;
	    IBLog_fmt(L_FATAL, "corrupted database file `%s'", filename);
	}
	else if (needsync) self->db->sync(self->db, 0);
    }
    else
    {
	pthread_mutex_destroy(&self->lock);
	free(self);
	self = 0;
	IBLog_fmt(L_FATAL, "error opening database file `%s'", filename);
    }
    return self;
}

InfoDbRow *InfoDb_get(InfoDb *self, const char *key)
{
    char *lowerkey = IB_lowerstr(key);
    DBT id = { lowerkey, strlen(lowerkey) };
    DBT val = { 0 };
    InfoDbRow *row = 0;
    lock(self);
    if (self->db->get(self->db, &id, &val, 0) != 0) goto done;
    if (val.size != 8) goto done;
    id.data = val.data;
    id.size = 8;
    if (self->db->get(self->db, &id, &val, 0) != 0) goto done;
    row = row_deser(val.data, val.size);
done:
    free(lowerkey);
    unlock(self);
    return row;
}

int InfoDb_put(InfoDb *self, const InfoDbRow *row)
{
    char *lowerkey = IB_lowerstr(row->key);
    DBT id = { lowerkey, strlen(lowerkey) };
    DBT val = { 0 };
    int rc = -1;
    uint8_t nkey[10] = { 0, 2, 0 };
    uint8_t szval[8] = { 0 };
    lock(self);
    int drc = self->db->get(self->db, &id, &val, 0);
    if (drc < 0) goto done;
    if (drc > 0)
    {
	if (!IBList_size(row->entries))
	{
	    rc = 0;
	    goto done;
	}
	DBT nid = { (void *)freeListKey, 2 };
	DBT nval = { 0 };
	drc = self->db->get(self->db, &nid, &nval, 0);
	if (drc < 0) goto done;
	if (drc == 0)
	{
	    if (nval.size != 8) goto done;
	    memcpy(nkey+2, nval.data, 8);
	    nid.data = nkey;
	    nid.size = 10;
	    drc = self->db->get(self->db, &nid, &nval, 0);
	    if (drc < 0) goto done;
	    nid.data = (void *)freeListKey;
	    nid.size = 2;
	    if (drc == 0)
	    {
		if (self->db->put(self->db, &nid, &nval, 0) < 0) goto done;
		nid.data = nkey;
		nid.size = 10;
		if (self->db->del(self->db, &nid, 0) < 0) goto done;
	    }
	    else
	    {
		if (self->db->del(self->db, &nid, 0) < 0) goto done;
	    }
	}
	else
	{
	    uint64_t newkey = (uint64_t)self->rowCapa;
	    ++self->rowCapa;
	    uint64_ser(nkey+2, newkey);
	    uint64_ser(szval, (uint64_t)self->rowCapa);
	    nval.data = szval;
	    nval.size = 8;
	    nid.data = (void *)rowCapaKey;
	    nid.size = 2;
	    if (self->db->put(self->db, &nid, &nval, 0) < 0) goto done;
	}
	++self->rowUsed;
	uint64_ser(szval, (uint64_t)self->rowUsed);
	nval.data = szval;
	nval.size = 8;
	nid.data = (void *)rowUsedKey;
	nid.size = 2;
	if (self->db->put(self->db, &nid, &nval, 0) < 0) goto done;
	val.data = nkey+2;
	val.size = 8;
	if (self->db->put(self->db, &id, &val, 0) < 0) goto done;
    }
    else if (val.size != 8) goto done;
    else
    {
	memcpy(nkey+2, val.data, 8);
    }
    if (!IBList_size(row->entries))
    {
	if (self->db->del(self->db, &id, 0) < 0) goto done;
	id.data = (void *)freeListKey;
	id.size = 2;
	drc = self->db->get(self->db, &id, &val, 0);
	if (drc < 0) goto done;
	if (drc == 0)
	{
	    if (val.size != 8) goto done;
	    id.data = nkey;
	    id.size = 10;
	    if (self->db->put(self->db, &id, &val, 0) < 0) goto done;
	    id.data = (void *)freeListKey;
	    id.size = 2;
	}
	val.data = nkey+2;
	val.size = 8;
	if (self->db->put(self->db, &id, &val, 0) < 0) goto done;
	id.data = nkey+2;
	id.size = 8;
	if (self->db->del(self->db, &id, 0) < 0) goto done;
	--self->rowUsed;
	uint64_ser(szval, (uint64_t)self->rowUsed);
	val.data = szval;
	val.size = 8;
	id.data = (void *)rowUsedKey;
	id.size = 2;
	if (self->db->put(self->db, &id, &val, 0) < 0) goto done;
    }
    else
    {
	id.data = nkey+2;
	id.size = 8;
	uint8_t *serialized = row_ser(row, &val.size);
	val.data = serialized;
	drc = self->db->put(self->db, &id, &val, 0);
	free(serialized);
	if (drc < 0) goto done;
    }
    rc = self->db->sync(self->db, 0);
done:
    free(lowerkey);
    unlock(self);
    return rc;
}

int InfoDb_add(InfoDb *self, const char *key, const InfoDbEntry *entry)
{
    lock(self);
    InfoDbRow *row = InfoDb_get(self, key);
    if (!row) 
    {
	row = IB_xmalloc(sizeof *row);
	row->key = IB_copystr(key);
	row->entries = IBList_create();
    }
    IBList_append(row->entries, (InfoDbEntry *)entry, 0);
    int rc = InfoDb_put(self, row);
    InfoDbRow_destroy(row);
    unlock(self);
    return rc;
}

InfoDbRow *InfoDb_getRandom(InfoDb *self)
{
    if (self->rowUsed == 0) return 0;

    uint64_t rndid;
    uint8_t rndkey[8];
    DBT id = { rndkey, 8 };
    DBT val = { 0 };
    InfoDbRow *row = 0;
    lock(self);
    for (;;)
    {
	getrandom(&rndid, sizeof rndid, 0);
	rndid %= (uint64_t) self->rowCapa;
	uint64_ser(rndkey, rndid);
	int drc = self->db->get(self->db, &id, &val, 0);
	if (drc < 0) break;
	if (drc == 0)
	{
	    row = row_deser(val.data, val.size);
	    break;
	}
    }
    unlock(self);
    return row;
}

void InfoDb_destroy(InfoDb *self)
{
    if (!self) return;
    self->db->close(self->db);
    pthread_mutex_destroy(&self->lock);
    free(self);
}

const char *InfoDbRow_key(const InfoDbRow *self)
{
    return self->key;
}

IBList *InfoDbRow_entries(InfoDbRow *self)
{
    return self->entries;
}

void InfoDbRow_destroy(InfoDbRow *self)
{
    if (!self) return;
    IBList_destroy(self->entries);
    free(self->key);
    free(self);
}

InfoDbEntry *InfoDbEntry_create(const char *description, const char *author)
{
    size_t authorlen = strlen(author);
    InfoDbEntry *self = IB_xmalloc(sizeof *self
	    + authorlen + strlen(description) + 2);
    strcpy(self->content, author);
    strcpy(self->content + authorlen + 1, description);
    self->authorlen = authorlen;
    self->time = time(0);
    return self;
}

time_t InfoDbEntry_time(const InfoDbEntry *self)
{
    return self->time;
}

const char *InfoDbEntry_description(const InfoDbEntry *self)
{
    return self->content + self->authorlen + 1;
}

const char *InfoDbEntry_author(const InfoDbEntry *self)
{
    return self->content;
}

void InfoDbEntry_destroy(InfoDbEntry *self)
{
    free(self);
}

