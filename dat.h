typedef struct Process Process;
typedef struct Segment Segment;
typedef struct Fdtable Fdtable;
typedef struct Fd Fd;

enum {
	STACKSIZE = 0x100000,
	NAMEMAX = 27,
	NNOTE = 5,
	SEGNUM = 8,
	Nfpregs = 32,
};

enum {
	SEGTEXT,
	SEGDATA,
	SEGBSS,
	SEGSTACK,
};

struct Process {
	Process *prev, *next;	/* linked list (for fs) */
	int pid;
	char name[NAMEMAX+1];	/* name for status file */
	Ref *path;		/* Ref + string data */

	Segment *S[SEGNUM];	/* memory */
	u64int PC;			/* program counter */
	u64int R[32];		/* general purpose registers, R31 is stack/zero */
	u64int N, Z, C, V;	/* flags */
	
	u32int FPSR;
	long double F[Nfpregs];

	char errbuf[ERRMAX];
	Fd *fd;			/* bitmap of OCEXEC files */
	
	/* note handling */
	u32int notehandler;
	int innote;
	jmp_buf notejmp;
	char notes[ERRMAX][NNOTE];
	long notein, noteout;
};

int vfp;

extern void **_privates;
extern int _nprivates;
#define P (*(Process**)_privates)
extern Ref nproc;
extern Process plist;
extern Lock plistlock;

enum {
	SEGFLLOCK = 1,
};

struct Segment {
	Ref;
	int flags;
	RWLock rw; /* lock for SEGFLLOCK segments */
	Lock lock; /* atomic accesses */
	u64int start, size;
	void *data;
	Ref *dref;
};

struct Fd {
	RWLock;
	Ref;
	u8int *fds;
	int nfds;
};

#define fulltrace 0
#define havesymbols 0
#define ultraverbose 0
#define systrace 0
