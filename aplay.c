#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>

enum
{
	DEBUG = 0,
	STACK = 8192
};

typedef struct Codecargs Codecargs;
typedef struct Kbdargs Kbdargs;

struct Codecargs
{
	Channel *pidchan;
	char *codec;
	int infd[2];
	int outfd[2];
};

struct Kbdargs
{
	Channel *exit;
	Codecargs *codecargs;
};

char *devaudio = "/dev/audio";
Channel *waitchan;
vlong maxoff, curoff;
int codecpid, pause;
Image *back;

void resized(int new);
void mousethread(void*);
void kbdthread(void*);
void inproc(void*);
void outproc(void*);
void timerproc(void*);
void codecproc(void*);
void usage(void);
void shutdown(char*);
void startstop(Codecargs*);
void start(Codecargs*);
void threadsfatal(void);

void
threadmain(int argc, char **argv)
{
	static Codecargs codecargs;
	Kbdargs kbdargs;
	int timer;

	threadsetname("mainproc");
	codecargs.codec = "/bin/audio/mp3dec";
	ARGBEGIN{
	case 'C':
		codecargs.codec = ARGF();
		if(codecargs.codec == nil)
			usage();
		break;
	case 'a':
		devaudio = ARGF();
		if(devaudio == nil)
			usage();
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc > 1)
		usage();
	if(argc == 1){
		close(0);
		if(open(argv[0], OREAD) != 0)
			threadsfatal();
	}
	close(1);
	if(open(devaudio, OWRITE) != 1)
		threadsfatal();
	maxoff = seek(0, 0, 2);
	codecargs.pidchan = chancreate(sizeof(ulong), 0);
	waitchan = threadwaitchan();
	initdraw(nil, nil, argv0);
	back = allocimagemix(display, DPalebluegreen, DWhite);
	threadcreate(mousethread, &codecargs, STACK);
	kbdargs.exit = chancreate(sizeof(char*), 0);
	kbdargs.codecargs = &codecargs;
	threadcreate(kbdthread, &kbdargs, STACK);
	timer = 500;
	proccreate(timerproc, &timer, STACK);
	pause = 1;
	startstop(&codecargs);
	resized(0);
	shutdown(recvp(kbdargs.exit));
}

void
kbdthread(void *a)
{
	Kbdargs *args;
	Codecargs *cargs;
	Channel *exit;
	Keyboardctl *kc;
	Rune r;

	threadsetname("kbdthread");
	args = a;
	exit = args->exit;
	cargs = args->codecargs;
	kc = initkeyboard(nil);
	for(;;){
		recv(kc->c, &r);
		switch(r){
		default:
			break;
		case 'q':
		case Kdel:
			sendp(exit, nil);
			break;
		case ' ':
			startstop(cargs);
//			if(pause)
//				curoff -= 16384;
//			resized(0);
			break;
		}
	}
}

void
mousethread(void *a)
{
	Codecargs *cargs;
	Mousectl *mc = initmouse(nil, screen);
	vlong o;
	enum
	{
		MOUSE,
		RESIZE,
		NALT
	};
	Alt alts[] = {
		[MOUSE] {mc->c, &mc->Mouse, CHANRCV},
		[RESIZE] {mc->resizec, nil, CHANRCV},
		[NALT] {nil, nil, CHANEND}
	};

	threadsetname("mousethread");
	cargs = a;
	for(;;) switch(alt(alts)){
	default:
		break;
	case MOUSE: 
		if(mc->buttons == 0)
			break;
		do{
			if(pause == 0)
				startstop(cargs);
			o = mc->xy.x - screen->r.min.x;
			if(o < 0) o = 0LL;
			o *= maxoff;
			o /= Dx(screen->r);
			o &= ~0xFLL;
			curoff = o;
			resized(0);
			recv(mc->c, &mc->Mouse);
		}while(mc->buttons != 0);
		startstop(cargs);
		break;
	case RESIZE:
		resized(1);
		break;
	}
}

void
startstop(Codecargs *codecargs)
{
	static int inpid, outpid;
	Channel *c;

	if(pause == 0){
		assert(codecpid != 0 && inpid != 0 && outpid != 0);
		postnote(PNPROC, codecpid, "die yankee pig dog");
		threadkill(inpid);
		threadkill(outpid);
		codecpid = inpid = outpid = 0;
	}else{
		assert(codecpid == 0 && inpid == 0 && outpid == 0);

		if(pipe(codecargs->infd) < 0)
			threadsfatal();
		if(pipe(codecargs->outfd) < 0)
			threadsfatal();

/*
 *		I'm leaking fd's here.
 */
if(DEBUG){
		fprint(2, "inpipes: %d %d\n", codecargs->infd[0], codecargs->infd[1]);
		fprint(2, "outpipes: %d %d\n", codecargs->outfd[0], codecargs->outfd[1]);
}
		proccreate(codecproc, codecargs, STACK);
		codecpid = recvul(codecargs->pidchan);

		c = chancreate(sizeof(int), 0);

		inpid = proccreate(inproc, c, STACK);
		send(c, &codecargs->infd[1]);

		outpid = proccreate(outproc, c, STACK);
		send(c, &codecargs->outfd[0]);

		chanfree(c);

		close(codecargs->infd[0]);
		close(codecargs->infd[1]);
		close(codecargs->outfd[0]);
		close(codecargs->outfd[1]);
	}
	pause ^= 1;
}

void
inproc(void *a)
{
	Channel *c;
	int fd, i;
	char buf[1024];
	long n;

	threadsetname("inproc");
	rfork(RFFDG);
	c = a;
	recv(c, &fd);
	dup(fd, 1);
	close(fd);
	for(i = 2; i < 20; i++)
		close(i);
	for(;;){
		n = sizeof(buf);
		if(curoff < 0)
			curoff = 0;
		if(curoff >= maxoff)
			threadexits(0);
		else if(n > maxoff - curoff){
			n = maxoff - curoff;
		}
		n = pread(0, buf, n, curoff);
		if(n <= 0)
			threadexits(0);
		if(write(1, buf, n) != n)
			threadexits(0);
		curoff += n;
	}
}

void
outproc(void *a)
{
	Channel *c;
	int fd, i;
	char buf[1024];
	long n;

	threadsetname("outproc");
	rfork(RFFDG);
	c = a;
	recv(c, &fd);
	dup(fd, 0);
	close(fd);
	for(i = 2; i < 20; i++)
		close(i);
	for(;;){
		n = read(0, buf, sizeof(buf));
		if(n <= 0)
			threadexits(0);
		if(write(1, buf, n) != n)
			threadexits(0);
	}
}

void
codecproc(void *a)
{
	Codecargs *codecargs;
	char *args[2];
	int i;

	threadsetname("codecproc");
	rfork(RFFDG);
	codecargs = a;

	close(codecargs->infd[1]);
	if(dup(codecargs->infd[0], 0) < 0){
		threadsfatal();
	}
	close(codecargs->infd[0]);

	close(codecargs->outfd[0]);
	if(dup(codecargs->outfd[1], 1) < 0){
		threadsfatal();
	}
	close(codecargs->outfd[1]);

	for(i = 3; i < 20; i++)
		close(i);
	args[0] = codecargs->codec;
	args[1] = nil;
	procexec(codecargs->pidchan, codecargs->codec, args);
	threadsfatal();
}
	
void
timerproc(void *a)
{
	int t;

	threadsetname("timerproc");
	t = *(int*)a;
	for(;;){
		sleep(t);
		resized(0);
	}
}

void
resized(int new)
{
	Rectangle r1, r2;
	char buf[32];
	
	if(new && getwindow(display, Refnone) < 0)
		threadsfatal();
	r1 = screen->r;
	r2 = r1;
	if(maxoff<= 0)
		r1.max.x = r1.min.x + Dx(r2);
	else
		r1.max.x = r1.min.x + ((vlong)Dx(r2) * curoff) / maxoff;
	r2.min.x = r1.max.x;
	draw(screen, r1, back, nil, ZP);
	draw(screen, r2, display->white, nil, ZP);
	if(pause)
		strcpy(buf, "pause");
	else
		strcpy(buf, "play");
	r2.min = ZP;
	r2.max = stringsize(display->defaultfont, buf);
	r1 = rectsubpt(screen->r, screen->r.min);
	r2 = rectaddpt(r2, subpt(divpt(r1.max, 2), divpt(r2.max, 2)));
	r2 = rectaddpt(r2, screen->r.min);
	r1 = insetrect(r2, -4);
	draw(screen, r1, display->white, nil, ZP);
	border(screen, insetrect(r1, 1), 2, display->black, ZP);
	string(screen, r2.min, display->black, ZP, display->defaultfont, buf);
	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "%s: aplay [-C codec] [-a audiodevice] [file]\n", argv0);
	threadexitsall(0);
}

void
shutdown(char *s)
{
	if(codecpid != 0)
		postnote(PNPROC, codecpid, "die yankee pig dog");
	threadexitsall(s);
}

void
threadsfatal(void)
{
	fprint(2, "%r\n");
	shutdown("error");
}
