#include <CoreServices/CoreServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/mman.h>

typedef struct sub_entry {
	long modtime;
	char *path;
	char *dir;
	 LIST_ENTRY(sub_entry) entry;
	Byte isfile;
	int txid;
} SubEntry;
typedef LIST_HEAD(sub_registry, sub_entry) SubRegistry;

static SubRegistry *registry;

void usage()
{
	fprintf(stderr,
		"Usage: veil [-h] [-c CONF] WATCHDIR\n"
		"Monitor FS events from WATCHDIR down. Print changes to STDOUT\n"
		"-h display this help message\n"
		"-c CONF is path to a newline delimited list of paths\n"
		"to whitelist. If CONF is not specified, we watch for all\n"
		"changes.\n");
	exit(2);
}

int loadconf(SubRegistry * registry, char *conf)
{
	SubEntry *ent;
	int lines = 0;
	char *line = NULL;
	struct stat statbuf;
	size_t linecap = 0;
	FILE *conffile;
	if ((conffile = fopen(conf, "r")) == NULL) {
		fprintf(stderr, "file %s can't be opened\n", conf);
		return -1;
	}
	while (getline(&line, &linecap, conffile) > 0) {
		ent = malloc(sizeof(*ent));
		bzero(ent, sizeof(*ent));
		ent->path = strdup(line);
		char *tmp = strchr(ent->path, '\n');
		*tmp = '\0';
		if (stat(ent->path, &statbuf)) {
			fprintf(stderr, "can't stat %s, skipping\n", ent->path);
			goto DONE;
		}
		if (S_ISREG(statbuf.st_mode)) {
			ent->isfile = 0x01;
			ent->modtime = statbuf.st_mtime;
			ent->dir = strdup(dirname(ent->path));
		} else if (S_ISDIR(statbuf.st_mode)) {
			ent->dir = strdup(ent->path);
		} else {
			fprintf(stderr, "%s not file or directory, skipping\n",
				ent->path);
			goto DONE;
		}
		LIST_INSERT_HEAD(registry, ent, entry);
		lines++;
 DONE:
		continue;
//              free(line);
	}
	fclose(conffile);
	return lines;
}

void checkevent(SubRegistry * registry, int txid, char *epath,
		FSEventStreamEventFlags eflags)
{
	if (registry == NULL) {
		printf("%s\n", epath);
		return;
	}
	SubEntry *sub;
	sub = malloc(sizeof(*sub));
	LIST_FOREACH(sub, registry, entry) {
		if (strstr(epath, sub->dir)) {
			int doprint = 1;
			if (sub->isfile) {
				struct stat statbuf;
				stat(sub->path, &statbuf);
				if (sub->modtime >= statbuf.st_mtime) {
					doprint = 0;
				} else {
					sub->modtime = statbuf.st_mtime;
				}
			}
			if (eflags & kFSEventStreamEventFlagItemRemoved)
				doprint = 0;
			if (sub->txid == txid)
				doprint = 0;
			if (doprint) {
				printf("%s\n", sub->path);
				sub->txid = txid;
			}
		}
	}
}

void cbprint(ConstFSEventStreamRef streamRef,
	     void *cbctx,
	     size_t numEvents,
	     void *eventPaths,
	     const FSEventStreamEventFlags eventFlags[],
	     const FSEventStreamEventId eventIds[])
{
	int i;
	static int s = 0;
	SubEntry *sub;
	char **paths = eventPaths;
	for (i = 0; i < numEvents; i++)
		checkevent(registry, ++s, paths[i], eventFlags[i]);
}

int main(int argc, char *argv[])
{
	int lines;
	char opt;
	char *conf;
	char *watchdir;
	SubEntry *sub;
	while ((opt = getopt(argc, argv, ":hc:")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			break;
		case 'c':
			conf = strdup(optarg);
			break;
		}
	}
	if (optind < argc)
		watchdir = argv[optind];
	else
		usage();
	registry = malloc(sizeof(*registry));
	LIST_INIT(registry);
	if ((lines = loadconf(registry, conf)) < 1) {
		free(registry);
		registry = NULL;
	}		

	CFArrayRef cpaths;
	FSEventStreamRef stream;
	CFAbsoluteTime latency = 1.0;
	cpaths = CFArrayCreateMutable(NULL, lines, &kCFTypeArrayCallBacks);
	CFStringRef cfpath =
	    CFStringCreateWithCString(NULL, watchdir, kCFStringEncodingUTF8);
	CFArrayAppendValue(cpaths, cfpath);
	stream = FSEventStreamCreate(NULL,
				     &cbprint, NULL, cpaths,
				     kFSEventStreamEventIdSinceNow, latency,
				     kFSEventStreamCreateFlagNone);
	FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(),
					 kCFRunLoopDefaultMode);
	FSEventStreamStart(stream);
	CFRunLoopRun();
}
