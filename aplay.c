#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>

enum
{
	STACK = 8192
};

typedef struct Codecargs Codecargs;
typedef struct Kbdargs Kbdargs;

struct Codecargs
{
	Channel *pidchan;
	char *codec;
	int infd[2];
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

void codecproc(void*);
void inproc(void*);
void kbdthread(void*);
void mousethread(void*);
void outproc(void*);
void resized(int new);
void shutdown(char*);
void start(Codecargs*);
void startstop(Codecargs*);
void threadsfatal(void);
void timerproc(void*);
void usage(void);
void waitforaudio(void);

void
threadmain(int argc, char **argv)
{
	Codecargs codecargs;
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
	start(&codecargs);
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
//				curoff -= 327680;
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
waitforaudio(void)
{
	Dir *d;

	for(;;){
		d = dirstat(devaudio);
		if(d->length == 0)
			break;
		free(d);
		sleep(100);
	}
	free(d);
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

Channel *inchan;
int inpid;

void
start(Codecargs *codecargs)
{
	inchan = chancreate(sizeof(int), 0);
	assert(codecpid == 0 && inpid == 0);
	if(pipe(codecargs->infd) < 0)
		threadsfatal();
	proccreate(codecproc, codecargs, STACK);
	codecpid = recvul(codecargs->pidchan);
	inpid = proccreate(inproc, inchan, STACK);
	send(inchan, &codecargs->infd[1]);
	close(codecargs->infd[0]);
	close(codecargs->infd[1]);
}

void
startstop(Codecargs *codecargs)
{
	if(pause == 0){
		assert(codecpid != 0 && inpid != 0);
		postnote(PNPROC, codecpid, "die yankee pig dog");
		threadkill(inpid);
		codecpid = inpid = 0;
	}else{
		waitforaudio();
		assert(codecpid == 0 && inpid == 0);
		if(pipe(codecargs->infd) < 0)
			threadsfatal();
		proccreate(codecproc, codecargs, STACK);
		codecpid = recvul(codecargs->pidchan);
		inpid = proccreate(inproc, inchan, STACK);
		send(inchan, &codecargs->infd[1]);
		close(codecargs->infd[0]);
		close(codecargs->infd[1]);
	}
	pause ^= 1;
}
