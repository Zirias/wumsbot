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
#define PORT 6667
#define NICK "wumsbot"
#define USER "wumsbot"
#define REALNAME "wumsbot"
#define CHANNEL "#bsd-de"

static const char *beer[] = {
    "Prost!",
    "Feierabend?",
    "Beer is the answer, but I can't remember the question ...",
    "gmBh heißt, geh mal Bier holen!",
    "Bierpreisbremse jetzt!"
};

static InfoDb *infoDb;

static void joined(IrcBotEvent *event)
{
    IrcBotResponse *response = IrcBotEvent_response(event);
    char buf[256];
    snprintf(buf, 256, "Hallo %s!", IrcBotEvent_arg(event));
    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event), buf, 0);
}

static void say(IrcBotEvent *event)
{
    IrcBotResponse *response = IrcBotEvent_response(event);
    const char *arg = IrcBotEvent_arg(event);

    if (!strcmp(arg, "my name"))
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		IrcBotEvent_from(event), 0);
    }
    else
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event), arg, 0);
    }
}

static void bier(IrcBotEvent *event)
{
    const IrcChannel *channel = IrcBotEvent_channel(event);
    if (!channel) return;

    const char *arg = IrcBotEvent_arg(event);
    List *beerfor;
    IrcBotResponse *response = IrcBotEvent_response(event);
    if (arg && (beerfor = List_fromString(arg, " \t")))
    {
	char buf[256];
	ListIterator *i = List_iterator(beerfor);
	while (ListIterator_moveNext(i))
	{
	    const char *nick = ListIterator_current(i);
	    if (HashTable_get(IrcChannel_nicks(channel), nick))
	    {
		snprintf(buf, 256, "wird %s mit Bier abfüllen!", nick);
	    }
	    else
	    {
		snprintf(buf, 256, "sieht hier keine(n) %s ...", nick);
	    }
	    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event), buf, 1);
	}
	ListIterator_destroy(i);
	List_destroy(beerfor);
    }
    else
    {
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		beer[rand() % (sizeof beer / sizeof *beer)], 0);
    }
}

static char *normalizeWs(const char *input, size_t len)
{
    if (!len) len = strlen(input);
    char *output = xmalloc(len+1);
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
	StringBuilder *sb = StringBuilder_create();
	StringBuilder_append(sb, InfoDbRow_key(row));
	StringBuilder_append(sb, " = ");
	ListIterator *i = List_iterator(InfoDbRow_entries(row));
	int first = 1;
	while (ListIterator_moveNext(i))
	{
	    const InfoDbEntry *entry = ListIterator_current(i);
	    if (!first) StringBuilder_append(sb, " | ");
	    else first = 0;
	    StringBuilder_append(sb, InfoDbEntry_description(entry));
	    StringBuilder_append(sb, " [");
	    StringBuilder_append(sb, InfoDbEntry_author(entry));
	    StringBuilder_append(sb, ", ");
	    time_t time = InfoDbEntry_time(entry);
	    gmtime_r(&time, &tm);
	    strftime(date, 11, "%d.%m.%Y", &tm);
	    StringBuilder_append(sb, date);
	    StringBuilder_append(sb, "]");
	}
	ListIterator_destroy(i);
	InfoDbRow_destroy(row);
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		StringBuilder_str(sb), 0);
	StringBuilder_destroy(sb);
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
	StringBuilder *sb = StringBuilder_create();
	StringBuilder_append(sb, "Ok, ");
	StringBuilder_append(sb, key);
	StringBuilder_append(sb, " = ");
	StringBuilder_append(sb, val);
	IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
		StringBuilder_str(sb), 0);
	StringBuilder_destroy(sb);
    }
    InfoDbEntry_destroy(entry);
    free(val);
    free(key);
    return;

invalid:
    IrcBotResponse_addMsg(response, IrcBotEvent_origin(event),
	    "hat nicht verstanden (?)", 1);
}

static void started(void)
{
    setSyslogLogger("wumsbot", LOG_DAEMON, 0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "-f"))
    {
	setFileLogger(stderr);
    }
    else
    {
	setSyslogLogger("wumsbot", LOG_DAEMON, 1);
	IrcBot_daemonize(-1, -1, "/tmp/wumsbot.pid", started);
    }

    infoDb = InfoDb_create("/tmp/wumsbot.db");
    if (!infoDb) return EXIT_FAILURE;

    IrcServer *server = IrcServer_create(IRCNET, SERVER, PORT,
	    NICK, USER, REALNAME);
    IrcServer_join(server, CHANNEL);
    IrcBot_addServer(server);

    IrcBot_addHandler(IBET_JOINED, 0, 0, 0, joined);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "say", say);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "bier", bier);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "info", info);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "lerne", lerne);
    IrcBot_addHandler(IBET_BOTCOMMAND, 0, ORIGIN_CHANNEL, "lern", lerne);

    srand(time(0));

    int rc = IrcBot_run();
    InfoDb_destroy(infoDb);
    return rc;
}

