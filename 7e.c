#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int nflag, pflag, bflag;
Ref nproc;

void
dump(void)
{
	int i;
	
	for(i = 0; i < 32; i++) {
		print("R%2d %.16lux", i, P->R[i]);
		if((i % 2) == 1) print("\n");
		else print("\t");
	}
}

static void
adjustns(void)
{
	if(bind("/arm64/bin", "/bin", MREPL) < 0)
		sysfatal("bind: %r");
	if(bind("/rc/bin", "/bin", MAFTER) < 0)
		sysfatal("bind: %r");
	putenv("cputype", "arm64");
	putenv("objtype", "arm64");
}

void
cleanup(void)
{
	if(P == nil)
		return;

	remproc(P);
	decref(&nproc);
	freesegs();
	fddecref(P->fd);
	if(P->path != nil && decref(P->path) == 0)
		free(P->path);
	free(P);
}

static void
usage(void)
{
	fprint(2, "usage: 7e [-npbf] text [...]\n");
	exits(nil);
}

void
suicide(char *fmt, ...)
{
	va_list va;
	char buf[1024];
	
	va_start(va, fmt);
	vsnprint(buf, sizeof(buf), fmt, va);
	va_end(va);
	fprint(2, "%s\n", buf);
	if(!bflag)
		exits(buf);
	abort();
}

int
notehandler(void *, char *note)
{
	if(strncmp(note, "sys:", 4) == 0)
		return 0;
	
	if(strncmp(note, "emu:", 4) == 0)
		exits(note);

	addnote(note);
	return 1;
}

static void
dotext(int argc, char **argv)
{
	char *file;
	
	if(**argv == '/' || **argv == '.' || **argv == '#') {
		if(loadtext(*argv, argc, argv) < 0)
			sysfatal("loadtext: %r");
		return;
	}
	file = smprint("/bin/%s", *argv);
	if(loadtext(file, argc, argv) < 0)
		sysfatal("loadtext: %r");
	free(file);
}

void
main(int argc, char **argv)
{
	ARGBEGIN {
	case 'n': nflag++; break;
	case 'p': pflag++; break;
	case 'b': bflag++; break;
	case 'f': vfp = 1; break;
	default: usage();
	} ARGEND;
	if(argc < 1)
		usage();
	if(_nprivates < 1)
		sysfatal("we don't have privates");
	if(rfork(RFREND | RFNAMEG | RFENVG) < 0)
		sysfatal("rfork: %r");
	atexit(cleanup);
	if(!nflag)
		adjustns();
	if(pflag)
		initfs("arm64proc", "/proc");
	initproc();
	dotext(argc, argv);
	atnotify(notehandler, 1);
	for(;;) {
		if(ultraverbose)
			dump();
		step();
		while((P->notein - P->noteout) % NNOTE) {
			donote(P->notes[P->noteout % NNOTE], 0);
			ainc(&P->noteout);
		}
	}
}
