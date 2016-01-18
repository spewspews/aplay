#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

char volstr[] = "volume";
char mutestr[] = "muted";
int volume, muted;
Image *back;

void
eresized(int new)
{
	Rectangle r1, r2;
	
	if(new && getwindow(display, Refnone) < 0)
		sysfatal("Cannot get window: %r");
	r1 = screen->r;
	r2 = r1;
	r1.min.y = r1.max.y - ((vlong)Dy(r2) * volume) / 100;
	r2.max.y = r1.min.y;
	draw(screen, r1, back, nil, ZP);
	draw(screen, r2, display->white, nil, ZP);
	r2.min = ZP;
	r2.max = stringsize(display->defaultfont, muted ? mutestr : volstr);
	r1 = rectsubpt(screen->r, screen->r.min);
	r2 = rectaddpt(r2, subpt(divpt(r1.max, 2), divpt(r2.max, 2)));
	r2 = rectaddpt(r2, screen->r.min);
	r1 = insetrect(r2, -4);
	draw(screen, r1, display->white, nil, ZP);
	border(screen, insetrect(r1, 1), 2, display->black, ZP);
	string(screen, r2.min, display->black, ZP, display->defaultfont, muted ? mutestr : volstr);
	flushimage(display, 1);
}

void
mute(int fd)
{
	static int oldvol, t;

	fprint(fd, "%d\n", oldvol);
	t = oldvol;
	oldvol = volume;
	volume = t;
	muted ^= 1;
}

void
main()
{
	int f;
	Mouse m;
	Event e;
	char buf[256], *toks[2];
	vlong o;

	f = open("/dev/volume", ORDWR);
	if(f < 0)
		sysfatal ("open volume failed");
	read(f, buf, sizeof(buf));
	gettokens(buf, toks, 2, " ");
	volume = atoi(toks[1]);
	if(initdraw(0, 0, "volume") < 0)
		sysfatal("initdraw failed: %r");
	einit(Emouse|Ekeyboard);
	back = allocimagemix(display, DPalebluegreen, DWhite);
	eresized(0);
	for(;;) switch(event(&e)){
	default:
		break;
	case Emouse:
		if(muted)
			break;
		m = e.mouse;
		if(m.buttons & 1) {
			o = screen->r.max.y - m.xy.y;
			if(o < 0) o = 0LL;
			o *= 100;
			o /= Dy(screen->r);
			volume = o;
			eresized(0);
			fprint(f, "%d\n", volume);
		}
		break;
	case Ekeyboard:
		switch(e.kbdc){
		default:
			break;
		case 'm':
			mute(f);
			eresized(0);
			break;
		case 'q':
		case Kdel:
			exits(0);
			break;
		}
		break;
	}
}
