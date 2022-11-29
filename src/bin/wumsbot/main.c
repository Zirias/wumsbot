#include <ircbot/hashtable.h>
#include <ircbot/ircbot.h>
#include <ircbot/ircchannel.h>
#include <ircbot/ircserver.h>
#include <ircbot/list.h>
#include <ircbot/log.h>
#include <ircbot/stringbuilder.h>
#include <ircbot/util.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "infodb.h"

#define IRCNET "libera"
#define SERVER "irc.libera.chat"
#define PORT 6697
#define NICK "wumsbot"
#define CHANNEL "#bsd-de"
#define UID 999
#define PIDFILE "/var/run/wumsbot/wumsbot.pid"
#define DBFILE "/var/db/wumsbot/wumsbot.db"
#define LOGIDENT "wumsbot"

static const char *beer[] = {
    "Prost!",
    "Feierabend?",
    "Beer is the answer, but I can't remember the question ...",
    "gmBh heißt, geh mal Bier holen!",
    "Bierpreisbremse jetzt!"
};

static InfoDb *infoDb;

static void bier(IrcBotEvent *event)
{
    const IrcChannel *channel = IrcBotEvent_channel(event);
    if (!channel) return;

    const char *arg = IrcBotEvent_arg(event);
    IBList *beerfor;
    IrcBotResponse *response = IrcBotEvent_response(event);
    if (arg && (beerfor = IBList_fromString(arg, " \t")))
    {
	char buf[256];
	IBListIterator *i = IBList_iterator(beerfor);
	while (IBListIterator_moveNext(i))
	{
	    const char *nick = IBListIterator_current(i);
	    if (IBHashTable_get(IrcChannel_nicks(channel), nick))
	    {
		snprintf(buf, 256, "wird %s mit Bier abfüllen!", nick);
		IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
			buf, 1);
	    }
	}
	IBListIterator_destroy(i);
	IBList_destroy(beerfor);
    }
    else
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		beer[rand() % (sizeof beer / sizeof *beer)], 0);
    }
}

static void kaffee(IrcBotEvent *event)
{
    const IrcChannel *channel = IrcBotEvent_channel(event);
    if (!channel) return;

    const char *arg = IrcBotEvent_arg(event);
    const char *from = IrcBotEvent_from(event);
    IBList *coffeefor;
    IrcBotResponse *response = IrcBotEvent_response(event);
    if ((arg && (coffeefor = IBList_fromString(arg, " \t")))
	    || (from && (coffeefor = IBList_fromString(from, " \t"))))
    {
	char buf[256];
	IBListIterator *i = IBList_iterator(coffeefor);
	while (IBListIterator_moveNext(i))
	{
	    const char *nick = IBListIterator_current(i);
	    if (IBHashTable_get(IrcChannel_nicks(channel), nick))
	    {
		snprintf(buf, 256, "reicht %s eine Tasse Kaffee...", nick);
		IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
			buf, 1);
	    }
	}
	IBListIterator_destroy(i);
	IBList_destroy(coffeefor);
    }
}

static char *normalizeWs(const char *input, size_t len)
{
    if (!len) len = strlen(input);
    char *output = IB_xmalloc(len+1);
    const char *r = input;
    char *w = output;
    while (isspace(*r)) { ++r; --len; }
    while (len)
    {
	if (isspace(*r))
	{
	    *w++ = ' ';
	    while (isspace(*r)) { ++r; --len; }
	}
	else
	{
	    *w++ = *r++;
	    --len;
	}
    }
    if (w != output && w[-1] == ' ') --w;
    *w = 0;
    if (w == output)
    {
	free(output);
	output = 0;
    }
    return output;
}

static void info(IrcBotEvent *event)
{
    const char *arg = IrcBotEvent_arg(event);
    IrcBotResponse *response = IrcBotEvent_response(event);
    char *key = 0;
    InfoDbRow *row = 0;
    if (arg && (key = normalizeWs(arg, 0)))
    {
	row = InfoDb_get(infoDb, key);
	free(key);
    }
    else
    {
	row = InfoDb_getRandom(infoDb);
    }
    if (row)
    {
	char date[11];
	struct tm tm;
	IBStringBuilder *sb = IBStringBuilder_create();
	IBStringBuilder_append(sb, InfoDbRow_key(row));
	IBStringBuilder_append(sb, " = ");
	IBListIterator *i = IBList_iterator(InfoDbRow_entries(row));
	int first = 1;
	while (IBListIterator_moveNext(i))
	{
	    const InfoDbEntry *entry = IBListIterator_current(i);
	    if (!first) IBStringBuilder_append(sb, " | ");
	    else first = 0;
	    IBStringBuilder_append(sb, InfoDbEntry_description(entry));
	    IBStringBuilder_append(sb, " [");
	    IBStringBuilder_append(sb, InfoDbEntry_author(entry));
	    IBStringBuilder_append(sb, ", ");
	    time_t time = InfoDbEntry_time(entry);
	    gmtime_r(&time, &tm);
	    strftime(date, 11, "%d.%m.%Y", &tm);
	    IBStringBuilder_append(sb, date);
	    IBStringBuilder_append(sb, "]");
	}
	IBListIterator_destroy(i);
	InfoDbRow_destroy(row);
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		IBStringBuilder_str(sb), 0);
	IBStringBuilder_destroy(sb);
    }
    else
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		"hat keine Ahnung...", 1);
    }
}

static void lerne(IrcBotEvent *event)
{
    const char *arg = IrcBotEvent_arg(event);
    size_t eqpos;
    IrcBotResponse *response = IrcBotEvent_response(event);
    if (!arg || !arg[(eqpos = strcspn(arg, "="))]) goto invalid;
    char *key = normalizeWs(arg, eqpos);
    char *val = normalizeWs(arg+eqpos+1, 0);
    if (!key || !val)
    {
	free(val);
	free(key);
	goto invalid;
    }
    const char *author = IrcBotEvent_from(event);
    if (!author) author = "<anonymous>";
    InfoDbEntry *entry = InfoDbEntry_create(val, author);
    if (InfoDb_add(infoDb, key, entry) < 0)
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		"hat ein Datenbankproblem :o", 1);
    }
    else
    {
	IBStringBuilder *sb = IBStringBuilder_create();
	IBStringBuilder_append(sb, "Ok, ");
	IBStringBuilder_append(sb, key);
	IBStringBuilder_append(sb, " = ");
	IBStringBuilder_append(sb, val);
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		IBStringBuilder_str(sb), 0);
	IBStringBuilder_destroy(sb);
    }
    InfoDbEntry_destroy(entry);
    free(val);
    free(key);
    return;

invalid:
    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
	    "hat nicht verstanden (?)", 1);
}

static void vergiss(IrcBotEvent *event)
{
    const char *arg = IrcBotEvent_arg(event);
    size_t eqpos;
    IrcBotResponse *response = IrcBotEvent_response(event);
    if (!arg || !arg[(eqpos = strcspn(arg, "="))]) goto invalid;
    char *key = normalizeWs(arg, eqpos);
    char *val = normalizeWs(arg+eqpos+1, 0);
    if (!key || !val)
    {
	free(val);
	free(key);
	goto invalid;
    }
    InfoDbRow *row = InfoDb_get(infoDb, key);
    if (!row) goto unknown;
    IBListIterator *i = IBList_iterator(InfoDbRow_entries(row));
    while (IBListIterator_moveNext(i))
    {
	InfoDbEntry *entry = IBListIterator_current(i);
	if (!strcmp(val, InfoDbEntry_description(entry)))
	{
	    IBList_remove(InfoDbRow_entries(row), entry);
	    InfoDbEntry_destroy(entry);
	    if (InfoDb_put(infoDb, row) < 0)
	    {
		IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
			"hat ein Datenbankproblem :o", 1);
	    }
	    else
	    {
		IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
			"Ok, vergessen!", 0);
	    }
	    IBListIterator_destroy(i);
	    InfoDbRow_destroy(row);
	    free(val);
	    free(key);
	    return;
	}
    }
    IBListIterator_destroy(i);
unknown:
    InfoDbRow_destroy(row);
    free(val);
    free(key);
    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
	    "wusste davon nichts...", 1);
    return;

invalid:
    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
	    "hat nicht verstanden (?)", 1);
}

static void started(void)
{
    IBLog_setSyslogLogger(LOGIDENT, LOG_DAEMON, 0);
}

static int startup(void)
{
    infoDb = InfoDb_create(DBFILE);
    return infoDb ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void shutdown(void)
{
    InfoDb_destroy(infoDb);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "-f"))
    {
	IBLog_setFileLogger(stderr);
    }
    else
    {
	IBLog_setSyslogLogger(LOGIDENT, LOG_DAEMON, 1);
	IrcBot_daemonize(UID, -1, PIDFILE, started);
    }

    IrcBot_startup(startup);
    IrcBot_shutdown(shutdown);

    IrcServer *server = IrcServer_create(IRCNET, SERVER, PORT, 1, NICK, 0, 0);
    IrcServer_join(server, CHANNEL);
    IrcBot_addServer(server);

    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "bier", bier);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "kaffee", kaffee);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "info", info);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "lerne", lerne);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "lern", lerne);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "learn", lerne);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "vergiss", vergiss);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "forget", vergiss);

    srand(time(0));

    return IrcBot_run();
}

