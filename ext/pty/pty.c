#include	"config.h"
#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/file.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<pwd.h>
#if !defined(HAVE_OPENPTY) && !defined(HAVE__GETPTY)
#include	<sys/ioctl.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#else
#define WIFSTOPPED(status)    (((status) & 0xff) == 0x7f)
#endif
#include <ctype.h>

#include "ruby.h"
#include "rubyio.h"

#include <signal.h>
#ifdef HAVE_SYS_STROPTS_H
#include <sys/stropts.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define	DEVICELEN	16

#if !defined(HAVE_OPENPTY)
#ifdef __hpux
char	*MasterDevice = "/dev/ptym/pty%s",
	*SlaveDevice =  "/dev/pty/tty%s",
	*deviceNo[] = {
		"p0","p1","p2","p3","p4","p5","p6","p7",
		"p8","p9","pa","pb","pc","pd","pe","pf",
		"q0","q1","q2","q3","q4","q5","q6","q7",
		"q8","q9","qa","qb","qc","qd","qe","qf",
		"r0","r1","r2","r3","r4","r5","r6","r7",
		"r8","r9","ra","rb","rc","rd","re","rf",
		"s0","s1","s2","s3","s4","s5","s6","s7",
		"s8","s9","sa","sb","sc","sd","se","sf",
		"t0","t1","t2","t3","t4","t5","t6","t7",
		"t8","t9","ta","tb","tc","td","te","tf",
		"u0","u1","u2","u3","u4","u5","u6","u7",
		"u8","u9","ua","ub","uc","ud","ue","uf",
		"v0","v1","v2","v3","v4","v5","v6","v7",
		"v8","v9","va","vb","vc","vd","ve","vf",
		"w0","w1","w2","w3","w4","w5","w6","w7",
		"w8","w9","wa","wb","wc","wd","we","wf",
		0,
	};
#else  /* NOT HPUX */
#ifdef _IBMESA  /* AIX/ESA */
char	*MasterDevice = "/dev/ptyp%s",
  	*SlaveDevice = "/dev/ttyp%s",
	*deviceNo[] = {
"00","01","02","03","04","05","06","07","08","09","0a","0b","0c","0d","0e","0f",
"10","11","12","13","14","15","16","17","18","19","1a","1b","1c","1d","1e","1f",
"20","21","22","23","24","25","26","27","28","29","2a","2b","2c","2d","2e","2f",
"30","31","32","33","34","35","36","37","38","39","3a","3b","3c","3d","3e","3f",
"40","41","42","43","44","45","46","47","48","49","4a","4b","4c","4d","4e","4f",
"50","51","52","53","54","55","56","57","58","59","5a","5b","5c","5d","5e","5f",
"60","61","62","63","64","65","66","67","68","69","6a","6b","6c","6d","6e","6f",
"70","71","72","73","74","75","76","77","78","79","7a","7b","7c","7d","7e","7f",
"80","81","82","83","84","85","86","87","88","89","8a","8b","8c","8d","8e","8f",
"90","91","92","93","94","95","96","97","98","99","9a","9b","9c","9d","9e","9f",
"a0","a1","a2","a3","a4","a5","a6","a7","a8","a9","aa","ab","ac","ad","ae","af",
"b0","b1","b2","b3","b4","b5","b6","b7","b8","b9","ba","bb","bc","bd","be","bf",
"c0","c1","c2","c3","c4","c5","c6","c7","c8","c9","ca","cb","cc","cd","ce","cf",
"d0","d1","d2","d3","d4","d5","d6","d7","d8","d9","da","db","dc","dd","de","df",
"e0","e1","e2","e3","e4","e5","e6","e7","e8","e9","ea","eb","ec","ed","ee","ef",
"f0","f1","f2","f3","f4","f5","f6","f7","f8","f9","fa","fb","fc","fd","fe","ff",
		};
#else
char	*MasterDevice = "/dev/pty%s",
	*SlaveDevice = "/dev/tty%s",
	*deviceNo[] = {
		"p0","p1","p2","p3","p4","p5","p6","p7",
		"p8","p9","pa","pb","pc","pd","pe","pf",
		"q0","q1","q2","q3","q4","q5","q6","q7",
		"q8","q9","qa","qb","qc","qd","qe","qf",
		"r0","r1","r2","r3","r4","r5","r6","r7",
		"r8","r9","ra","rb","rc","rd","re","rf",
		0,
	};
#endif /* _IBMESA */
#endif /* HPUX */
#endif /* !defined(HAVE_OPENPTY) */

char	SlaveName[DEVICELEN];

extern int errno;

#ifndef HAVE_SETEUID
# ifdef HAVE_SETREUID
#  define seteuid(e)	setreuid(-1, (e))
# else /* NOT HAVE_SETREUID */
#  ifdef HAVE_SETRESUID
#   define seteuid(e)	setresuid(-1, (e), -1)
#  else /* NOT HAVE_SETRESUID */
    /* I can't set euid. (;_;) */
#  endif /* HAVE_SETRESUID */
# endif /* HAVE_SETREUID */
#endif /* NO_SETEUID */

struct pty_info {
  int fd;
  pid_t child_pid;
};

static void
pty_raise(cpid)
    int cpid;
{
    char buf[1024];

    snprintf(buf, sizeof(buf),
	     "eval %%Q{Thread.main.raise 'pty - stopped: %d'}, nil, \"%s\", %d",
	     cpid, ruby_sourcefile, ruby_sourceline);
    rb_eval_string(buf);
}

static VALUE
pty_syswait(pid)
    int pid;
{
    int cpid, status;

    cpid = rb_waitpid(pid, &status, WUNTRACED);

    printf("PTY command (%d) finished (%d:%d)\n", pid, cpid, status);
    if (cpid == 0 || cpid == -1)
	return Qnil;

#ifdef IF_STOPPED
    if (IF_STOPPED(status)) { /* suspend */
	pty_raise(cpid);
    }
#else
#ifdef WIFSTOPPED
    if (WIFSTOPPED(status)) { /* suspend */
	pty_raise(cpid);
    }
#else
---->> Either IF_STOPPED or WIFSTOPPED is needed <<----
#endif /* WIFSTOPPED */
#endif /* IF_STOPPED */
    
    return Qnil;
}

static void getDevice _((int*, int*));

static void
establishShell(shellname, info)
    char *shellname;
    struct pty_info *info;
{	
    static int		i,j,master,slave,currentPid;
    char		*p,*getenv();
    struct passwd	*pwent;
    RETSIGTYPE		chld_changed();
    
    if (shellname[0] == '\0') {
	if ((p = getenv("SHELL")) != NULL) {
	    shellname = p;
	}
	else {
	    pwent = getpwuid(getuid());
	    if (pwent && pwent->pw_shell)
		shellname = pwent->pw_shell;
	    else
		shellname = "/bin/sh";
	}
    }
    getDevice(&master,&slave);

    currentPid = getpid();
    if((i = vfork()) < 0) {
	rb_sys_fail("fork failed");
    }

    if(i == 0) {	/* child */
	int argc;
	char *argv[1024];
	currentPid = getpid();	

	/*
	 * Set free from process group and controlling terminal
	 */
#ifdef HAVE_SETSID
	(void) setsid();
#else /* HAS_SETSID */
# ifdef HAVE_SETPGRP
#  ifdef SETGRP_VOID
	if (setpgrp() == -1)
	    perror("setpgrp()");
#  else /* SETGRP_VOID */
	if (setpgrp(0, currentPid) == -1)
	    rb_sys_fail("setpgrp()");
	if ((i = open("/dev/tty", O_RDONLY)) < 0)
	    rb_sys_fail("/dev/tty");
	else {
	    if (ioctl(i, TIOCNOTTY, (char *)0))
		perror("ioctl(TIOCNOTTY)");
	    close(i);
	}
#  endif /* SETGRP_VOID */
# endif /* HAVE_SETPGRP */
#endif /* HAS_SETSID */

	/*
	 * obtain new controlling terminal
	 */
#if defined(TIOCSCTTY)
	close(master);
	(void) ioctl(slave, TIOCSCTTY, (char *)0);
	/* errors ignored for sun */
#else
	close(slave);
	slave = open(SlaveName, O_RDWR);
	if (slave < 0) {
	    perror("open: pty slave");
	    _exit(1);
	}
	close(master);
#endif
	dup2(slave,0);
	dup2(slave,1);
	dup2(slave,2);
	close(slave);

#if defined(HAVE_SETEUID) || defined(HAVE_SETREUID) || defined(HAVE_SETRESUID)
	seteuid(getuid());
#endif

	argc = 0;
	for (i = 0; shellname[i];) {
	    while (isspace(shellname[i])) i++;
	    for (j = i; shellname[j] && !isspace(shellname[j]); j++);
	    argv[argc] = (char*)xmalloc(j-i+1);
	    strncpy(argv[argc],&shellname[i],j-i);
	    argv[argc][j-i] = 0;
	    i = j;
	    argc++;
	}
	argv[argc] = NULL;
	execvp(argv[0],argv);
	sleep(1);
	_exit(1);
    }

    close(slave);

    info->child_pid = i;
    info->fd = master;
}

#ifdef HAVE_OPENPTY
/*
 * Use openpty(3) of 4.3BSD Reno and later,
 * or the same interface function.
 */
static void
getDevice(master,slave)
    int	*master,*slave;
{
    if (openpty(master, slave, SlaveName,
		(struct termios *)0, (struct winsize *)0) == -1) {
	rb_raise(rb_eRuntimeError, "openpty() failed");
    }
}
#else /* HAVE_OPENPTY */
#ifdef HAVE__GETPTY
static void
getDevice(master,slave)
    int	*master,*slave;
{
    char *name;

    if (!(name = _getpty(master, O_RDWR, 0622, 0))) {
	rb_raise(rb_eRuntimeError, "_getpty() failed");
    }

    *slave = open(name, O_RDWR);
    strcpy(SlaveName, name);
}
#else /* HAVE__GETPTY */
static void
getDevice(master,slave)
    int	*master,*slave;
{
    char **p;
    int	 i,j;
    char MasterName[DEVICELEN];

#ifdef HAVE_DEV_PTMX
    char *pn;
    void (*s)();

    extern char *ptsname(int);
    extern int unlockpt(int);
    extern int grantpt(int);

    if((i = open("/dev/ptmx", O_RDWR, 0)) != -1) {
	s = signal(SIGCHLD, SIG_DFL);
	if(grantpt(i) != -1) {
	    signal(SIGCHLD, s);
	    if(unlockpt(i) != -1) {
		if((pn = ptsname(i)) != NULL) {
		    if((j = open(pn, O_RDWR, 0)) != -1) {
#if defined I_PUSH
			if(ioctl(j, I_PUSH, "ptem") != -1) {
			    if(ioctl(j, I_PUSH, "ldterm") != -1) {
#endif
				*master = i;
				*slave = j;
				strcpy(SlaveName, pn);
				return;
#if defined I_PUSH
			    }
			}
#endif
		    }
		}
	    }
	}
	close(i);
    }
    rb_raise(rb_eRuntimeError, "Cannot get Master/Slave device");
#else
    for (p = deviceNo; *p != NULL; p++) {
	sprintf(MasterName ,MasterDevice,*p);
	if ((i = open(MasterName,O_RDWR,0)) >= 0) {
	    *master = i;
	    sprintf(SlaveName ,SlaveDevice,*p);
	    if ((j = open(SlaveName,O_RDWR,0)) >= 0) {
		*slave = j;
		chown(SlaveName, getuid(), getgid());
		chmod(SlaveName, 0622);
		return;
	    }
	    close(i);
	}
    }
    rb_raise(rb_eRuntimeError, "Cannot get %s\n", SlaveDevice);
#endif
}
#endif /* HAVE__GETPTY */
#endif /* HAVE_OPENPTY */

static void
freeDevice()
{
    chmod(SlaveName, 0666);
    chown(SlaveName, 0, 0);
}

/* ruby function: getpty */
static VALUE
pty_getpty(self, command)
    VALUE self, command;
{
    VALUE res, th;
    struct pty_info info;
    OpenFile *wfptr,*rfptr;
    VALUE rport = rb_obj_alloc(rb_cFile);
    VALUE wport = rb_obj_alloc(rb_cFile);
  
    MakeOpenFile(rport, rfptr);
    MakeOpenFile(wport, wfptr);

    if (TYPE(command) == T_ARRAY)
	command = rb_ary_join(command,rb_str_new2(" "));
    Check_SafeStr(command);

    establishShell(RSTRING(command)->ptr,&info);

    rfptr->mode = rb_io_mode_flags("r");
    rfptr->f = fdopen(info.fd, "r");
    rfptr->path = strdup(RSTRING(command)->ptr);

    wfptr->mode = rb_io_mode_flags("w");
    wfptr->f = fdopen(dup(info.fd), "w");
    wfptr->path = strdup(RSTRING(command)->ptr);

    res = rb_ary_new2(3);
    rb_ary_store(res,0,(VALUE)rport);
    rb_ary_store(res,1,(VALUE)wport);
    rb_ary_store(res,2,INT2FIX(info.child_pid));

    printf("start watching PTY command (%d)\n", info.child_pid);
    th = rb_thread_create(pty_syswait, (void*)info.child_pid);
    if (rb_block_given_p()) {
	res = rb_yield((VALUE)res);
	rb_funcall(th, rb_intern("kill"), 0, 0);
	return res;
    }
    else {
	return res;
    }
}

/* ruby function: protect_signal - obsolete */
static VALUE
pty_protect(self)
    VALUE self;
{
    rb_warn("PTY::protect_signal is no longer needed");
    rb_yield(Qnil);
    return self;
}

/* ruby function: reset_signal - obsolete */
static VALUE
pty_reset_signal(self)
    VALUE self;
{
    rb_warn("PTY::reset_signal is no longer needed");
    return self;
}

static VALUE cPTY;

void
Init_pty()
{
    cPTY = rb_define_module("PTY");
    rb_define_module_function(cPTY,"getpty",pty_getpty,1);
    rb_define_module_function(cPTY,"spawn",pty_getpty,1);
    rb_define_module_function(cPTY,"protect_signal",pty_protect,0);
    rb_define_module_function(cPTY,"reset_signal",pty_reset_signal,0);
}
